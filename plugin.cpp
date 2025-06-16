#include <Windows.h>       // For MAX_PATH, HMODULE, GetModuleHandleExA, GetModuleFileNameA
#include <libloaderapi.h>  // Often included by Windows.h, but explicit for clarity

#include <array>   // For std::array if needed
#include <atomic>  // For std::atomic for thread safety
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>       // For std::map to track light states
#include <optional>  // For std::optional
#include <string>    // For std::string
#include <thread>
#include <vector>  // For std::vector

// Include cpr for HTTP requests
#include <cpr/cpr.h>
// For JSON serialization (nlohmann/json is included and used here)
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "RE/Skyrim.h"
#include "SKSE/API.h"
#include "SKSE/SKSE.h"

// Explicitly include spdlog for our custom logger
#include <spdlog/sinks/basic_file_sink.h>  // For writing to a basic file
#include <spdlog/spdlog.h>

// === CONFIGURATION GLOBALS ===
std::string g_HA_URL;
std::string g_HA_TOKEN;
std::vector<std::string> g_LIGHT_ENTITY_IDS;
bool g_DebugMode = false;  // Default to false (off)

// Define the name and path for the configuration file
const std::string PLUGIN_NAME_STR = "HomeAssistantLink";  // Use a string constant for the plugin name
const std::string CONFIG_FILE_NAME = PLUGIN_NAME_STR + ".json";
const std::string LOG_FILE_NAME = PLUGIN_NAME_STR + ".log";  // New dedicated log file name

// Global custom spdlog logger for our plugin
std::shared_ptr<spdlog::logger> g_plugin_logger;

// Global atomic flag to track if the data export thread is already running
std::atomic<bool> g_threadRunning = false;

// --- NEW STRUCT DEFINITIONS ---

// Structure to hold the properties for a single light's desired state
struct LightState {
    std::string entity_id;
    std::array<int, 3> rgb_color;  // Using std::array for fixed-size RGB
    int brightness_pct;
    std::optional<std::string> effect;
    std::optional<std::string> scene;

    // ADDED: Equality operator to compare LightState objects
    bool operator==(const LightState &other) const {
        return (entity_id == other.entity_id && rgb_color == other.rgb_color &&
                brightness_pct == other.brightness_pct && effect == other.effect && scene == other.scene);
    }

    bool operator!=(const LightState &other) const { return !(*this == other); }
};

// Structure to hold trigger information
struct ScenarioTrigger {
    std::string type;
    // Optional fields based on type, using std::optional or checking for existence in JSON
    std::optional<int> min_hour;
    std::optional<int> max_hour;
};

// Structure for a full scenario
struct Scenario {
    std::string name;
    int priority;
    ScenarioTrigger trigger;
    std::vector<LightState> outcome;  // A vector of LightState objects for individual control
};

// Global vector to store all loaded scenarios
std::vector<Scenario> g_SCENARIOS;

// ADDED: Global map to store the last commanded state for each light (for state tracking)
std::map<std::string, LightState> g_LastCommandedLightStates;

// --- END NEW STRUCT DEFINITIONS ---

// --- Utility functions for logging using our custom spdlog logger ---

// These functions will now use 'g_plugin_logger' which directs output to our dedicated file.
void LogToFile_Info(const std::string &message) {
    if (g_plugin_logger) {
        g_plugin_logger->info("INFO: {}", message);
    }
}

void LogToFile_Warn(const std::string &message) {
    if (g_plugin_logger) {
        g_plugin_logger->warn("WARN: {}", message);
    }
}

void LogToFile_Error(const std::string &message) {
    if (g_plugin_logger) {
        g_plugin_logger->error("ERROR: {}", message);
    }
}

void LogToFile_Debug(const std::string &message) {
    if (g_DebugMode && g_plugin_logger) {
        g_plugin_logger->debug("DEBUG: {}", message);
    }
}

// Skyrim In-Game Console Logger (for runtime messages visible in-game)
// This remains separate from file logging and uses SKSE's console interface.
void LogToConsole(const std::string &message) {
    // Only log to in-game console if debug mode is on, or for specific critical errors (ERROR/WARNING)
    if (g_DebugMode || message.find("ERROR:") != std::string::npos || message.find("WARNING:") != std::string::npos) {
        if (auto console = RE::ConsoleLog::GetSingleton()) {
            console->Print(message.c_str());
        }
    }
}

// Function to get the path of the current DLL (your plugin)
std::filesystem::path GetCurrentModulePath() {
    char path[MAX_PATH];  // MAX_PATH is defined in Windows.h
    HMODULE hm = NULL;

    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&GetCurrentModulePath, &hm) == 0) {
        // Fallback to simpler error if plugin logger isn't initialized yet or for this specific error
        if (g_plugin_logger)
            g_plugin_logger->error("GetCurrentModulePath failed to get module handle.");
        else
            OutputDebugStringA("HomeAssistantLink: ERROR - GetCurrentModulePath failed to get module handle.\n");
        return "";
    }
    if (GetModuleFileNameA(hm, path, sizeof(path)) == 0) {
        if (g_plugin_logger)
            g_plugin_logger->error("GetCurrentModulePath failed to get module file name.");
        else
            OutputDebugStringA("HomeAssistantLink: ERROR - GetCurrentModulePath failed to get module file name.\n");
        return "";
    }
    return std::filesystem::path(path);
}

// Function to load configuration from JSON file
bool LoadConfiguration() {
    std::filesystem::path pluginPath = GetCurrentModulePath();
    if (pluginPath.empty()) {
        LogToConsole("ERROR: Could not determine plugin path. Cannot load configuration file.");  // Fallback console
        return false;
    }
    std::filesystem::path configFilePath = pluginPath.parent_path() / CONFIG_FILE_NAME;

    LogToFile_Info("Attempting to load configuration from: " + configFilePath.string() + ".");

    std::ifstream configFile(configFilePath);
    if (!configFile.is_open()) {
        LogToFile_Error("Failed to open configuration file: " + configFilePath.string() +
                        ". Home Assistant Link will not function.");
        LogToConsole("ERROR: Failed to open config: " + CONFIG_FILE_NAME);
        return false;
    }

    try {
        json config = json::parse(configFile);

        // Read HomeAssistant section
        if (config.contains("HomeAssistant") && config["HomeAssistant"].is_object()) {
            if (config["HomeAssistant"].contains("Url") && config["HomeAssistant"]["Url"].is_string()) {
                g_HA_URL = config["HomeAssistant"]["Url"].get<std::string>();
            } else {
                LogToFile_Warn(
                    "'Url' not found or not a string in 'HomeAssistant' section of config. Using empty URL.");
                g_HA_URL = "";
            }
            if (config["HomeAssistant"].contains("Token") && config["HomeAssistant"]["Token"].is_string()) {
                g_HA_TOKEN = config["HomeAssistant"]["Token"].get<std::string>();
            } else {
                LogToFile_Warn(
                    "'Token' not found or not a string in 'HomeAssistant' section of config. Using empty Token.");
                g_HA_TOKEN = "";
            }
            // Read DebugMode
            if (config["HomeAssistant"].contains("DebugMode") && config["HomeAssistant"]["DebugMode"].is_boolean()) {
                g_DebugMode = config["HomeAssistant"]["DebugMode"].get<bool>();
                LogToFile_Info("Debug Mode: " + std::string(g_DebugMode ? "Enabled" : "Disabled") + ".");
            } else {
                LogToFile_Warn(
                    "'DebugMode' not found or not a boolean in 'HomeAssistant' section of config. Defaulting to "
                    "disabled.");
                g_DebugMode = false;  // Ensure it's false if not specified or wrong type
            }

        } else {
            LogToFile_Warn("'HomeAssistant' section not found or not an object in config.");
            g_HA_URL = "";
            g_HA_TOKEN = "";
            g_DebugMode = false;  // Also reset debug mode if the section is missing
        }

        // Read Lights array (existing code)
        if (config.contains("Lights") && config["Lights"].is_array()) {
            g_LIGHT_ENTITY_IDS = config["Lights"].get<std::vector<std::string>>();
            LogToFile_Info("Loaded " + std::to_string(g_LIGHT_ENTITY_IDS.size()) + " light entity IDs.");
        } else {
            LogToFile_Warn("'Lights' array not found or not an array in config. No lights configured.");
            g_LIGHT_ENTITY_IDS.clear();
        }

        // --- NEW SECTION: Read Scenarios array ---
        if (config.contains("Scenarios") && config["Scenarios"].is_array()) {
            for (const auto &scenario_json : config["Scenarios"]) {
                Scenario scenario;
                scenario.name = scenario_json.at("name").get<std::string>();
                scenario.priority = scenario_json.at("priority").get<int>();

                // Parse trigger
                const auto &trigger_json = scenario_json.at("trigger");
                scenario.trigger.type = trigger_json.at("type").get<std::string>();
                if (trigger_json.contains("min_hour")) {
                    scenario.trigger.min_hour = trigger_json.at("min_hour").get<int>();
                }
                if (trigger_json.contains("max_hour")) {
                    scenario.trigger.max_hour = trigger_json.at("max_hour").get<int>();
                }

                // Parse outcome (array of LightState)
                const auto &outcome_array_json = scenario_json.at("outcome");
                if (outcome_array_json.is_array()) {
                    for (const auto &light_state_json : outcome_array_json) {
                        LightState light_state;
                        light_state.entity_id = light_state_json.at("entity_id").get<std::string>();
                        light_state.rgb_color = light_state_json.at("rgb_color").get<std::array<int, 3>>();
                        light_state.brightness_pct = light_state_json.at("brightness_pct").get<int>();

                        // GEÄNDERT: Robustes Parsen für optionalen 'effect'
                        if (light_state_json.contains("effect")) {
                            if (light_state_json["effect"].is_string()) {
                                light_state.effect = light_state_json["effect"].get<std::string>();
                            } else if (light_state_json["effect"].is_null()) {
                                light_state.effect = std::nullopt;  // Explizit nullopt, wenn JSON null ist
                            }
                            // Wenn 'effect' vorhanden, aber weder String noch null, bleibt es nullopt
                        } else {
                            light_state.effect = std::nullopt;  // Wenn Schlüssel komplett fehlt
                        }

                        // GEÄNDERT: Robustes Parsen für optionalen 'scene'
                        if (light_state_json.contains("scene") && light_state_json["scene"].is_string()) {
                            light_state.scene = light_state_json["scene"].get<std::string>();
                        } else {
                            light_state.scene = std::nullopt;  // nullopt, wenn fehlt oder kein String
                        }

                        // Warnung, wenn effect "scene" ist, aber kein Szenenname vorhanden
                        if (light_state.effect.has_value() && light_state.effect.value() == "scene" &&
                            !light_state.scene.has_value()) {
                            LogToFile_Warn("Light " + light_state.entity_id +
                                           ": 'effect' is 'scene' but no 'scene' name provided. This scenario may not "
                                           "function correctly.");
                        }

                        scenario.outcome.push_back(light_state);
                    }
                } else {
                    LogToFile_Warn("Scenario '" + scenario.name +
                                   "' has 'outcome' that is not an array. Skipping light states for this scenario.");
                }
                g_SCENARIOS.push_back(scenario);
            }
            LogToFile_Info("Loaded " + std::to_string(g_SCENARIOS.size()) + " scenarios.");
        } else {
            LogToFile_Warn("'Scenarios' array not found or not an array in config. No scenarios configured.");
            g_SCENARIOS.clear();
        }
        // --- END NEW SECTION ---

        if (g_HA_URL.empty() || g_HA_TOKEN.empty() || g_LIGHT_ENTITY_IDS.empty()) {
            LogToFile_Warn(
                "Configuration is incomplete (missing URL, Token, or Lights). Home Assistant Link might not function "
                "correctly.");
        }

        LogToFile_Info("Configuration loaded successfully.");
        return true;

    } catch (const json::parse_error &e) {
        LogToFile_Error("JSON parse error in config file: " + std::string(e.what()));
        LogToConsole("ERROR: JSON parse error in config file.");
        return false;
    } catch (const std::exception &e) {
        LogToFile_Error("Unexpected error while parsing config file: " + std::string(e.what()));
        LogToConsole("ERROR: Unexpected error parsing config file.");
        return false;
    }
}

// Function to set light color via Home Assistant API
void ApplyLightStates(const std::vector<LightState> &light_states_to_apply) {
    if (g_HA_URL.empty() || g_HA_TOKEN.empty()) {
        LogToFile_Error("Cannot send light command. Home Assistant URL or Token not loaded.");
        LogToConsole("ERROR: Cannot send light command. HA config incomplete.");
        return;
    }

    cpr::Header headers;
    headers["Authorization"] = "Bearer " + g_HA_TOKEN;
    headers["Content-Type"] = "application/json";

    for (const auto &light_state : light_states_to_apply) {
        // --- Check previous state to determine if we're coming from a scene effect ---
        bool was_scene_effect = false;
        if (g_LastCommandedLightStates.count(light_state.entity_id)) {
            const auto &last_state = g_LastCommandedLightStates[light_state.entity_id];
            if (last_state.effect.has_value() && last_state.effect.value() == "scene") {
                was_scene_effect = true;
            }
        }
        // --- END CHECK ---

        // Check if the desired state is different from the last commanded state
        if (g_LastCommandedLightStates.count(light_state.entity_id) &&
            g_LastCommandedLightStates[light_state.entity_id] == light_state) {
            LogToFile_Debug("Light " + light_state.entity_id + " is already in the desired state. Skipping command.");
            continue;
        }

        json payload_json;
        std::string service_url;
        std::string log_target_entity_id;
        bool success = false;  // To track if the state was successfully applied

        if (light_state.effect.has_value() && light_state.effect.value() == "scene") {
            // SCENE MODE: Two-step process (light.turn_on effect=scene, then select.select_option)
            if (light_state.scene.has_value()) {
                // PART 1: light.turn_on with effect="scene" to switch mode
                service_url = g_HA_URL + "/api/services/light/turn_on";
                log_target_entity_id = light_state.entity_id;

                payload_json = json::object();  // Clear payload for this call
                payload_json["entity_id"] = light_state.entity_id;
                payload_json["effect"] = light_state.effect.value();  // This should be "scene"

                std::string payload_part1 = payload_json.dump();
                LogToFile_Debug("Sending PART 1 (turn_on effect=scene) request to HA for " + log_target_entity_id +
                                ": " + payload_part1);
                cpr::Response r_cpr_part1 = cpr::Post(cpr::Url{service_url}, headers, cpr::Body{payload_part1});

                if (r_cpr_part1.status_code == 200) {
                    LogToFile_Debug("Successfully sent PART 1 command for " + log_target_entity_id);
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));  // Small delay

                    // PART 2: select.select_option with the specific scene
                    service_url = g_HA_URL + "/api/services/select/select_option";

                    std::string light_object_id = light_state.entity_id;
                    if (light_object_id.rfind("light.", 0) == 0) {
                        light_object_id = light_object_id.substr(6);
                    }
                    std::string select_entity_id = "select." + light_object_id + "_scene";

                    payload_json = json::object();  // Clear payload for this call
                    payload_json["entity_id"] = select_entity_id;
                    payload_json["option"] = light_state.scene.value();

                    std::string payload_part2 = payload_json.dump();
                    LogToFile_Debug("Sending PART 2 (select_option) request to HA for " + select_entity_id + ": " +
                                    payload_part2);
                    cpr::Response r_cpr_part2 = cpr::Post(cpr::Url{service_url}, headers, cpr::Body{payload_part2});

                    if (r_cpr_part2.status_code == 200) {
                        LogToFile_Debug("Successfully sent PART 2 command for " + select_entity_id);
                        success = true;  // Both parts successful
                    } else {
                        LogToFile_Error("Error PART 2 (select_option) setting state for " + select_entity_id +
                                        ": Status Code " + std::to_string(r_cpr_part2.status_code) + " - " +
                                        r_cpr_part2.error.message);
                        LogToFile_Error("HA Response Text PART 2 for " + select_entity_id + ": " + r_cpr_part2.text);
                        LogToConsole("ERROR: HA PART 2 for " + select_entity_id + ": Status Code " +
                                     std::to_string(r_cpr_part2.status_code));
                    }
                } else {
                    LogToFile_Error("Error PART 1 (turn_on effect=scene) setting state for " + log_target_entity_id +
                                    ": Status Code " + std::to_string(r_cpr_part1.status_code) + " - " +
                                    r_cpr_part1.error.message);
                    LogToFile_Error("HA Response Text PART 1 for " + log_target_entity_id + ": " + r_cpr_part1.text);
                    LogToConsole("ERROR: HA PART 1 for " + log_target_entity_id + ": Status Code " +
                                 std::to_string(r_cpr_part1.status_code));
                }
            } else {
                LogToFile_Warn("Light " + light_state.entity_id +
                               ": 'effect' is 'scene' but no 'scene' name provided. Skipping command.");
            }
        } else {
            // STANDARD MODE: light.turn_on for color/brightness/other effects or no effect
            service_url = g_HA_URL + "/api/services/light/turn_on";
            log_target_entity_id = light_state.entity_id;

            // --- NEUE LOGIK: Two-step transition from scene to static color ---
            if (was_scene_effect) {
                // PART 1: Explicitly send effect: "off" to clear the scene mode
                json part1_payload;
                part1_payload["entity_id"] = light_state.entity_id;
                part1_payload["effect"] = "off";  // Use "off" as it was found to work

                std::string payload_part1_str = part1_payload.dump();
                LogToFile_Debug("Sending PART 1 (turn_on effect=\"off\" to clear scene) request to HA for " +
                                log_target_entity_id + ": " + payload_part1_str);
                cpr::Response r_cpr_part1 = cpr::Post(cpr::Url{service_url}, headers, cpr::Body{payload_part1_str});

                if (r_cpr_part1.status_code == 200) {
                    LogToFile_Debug("Successfully sent PART 1 (clear scene) command for " + log_target_entity_id);
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));  // Small delay for mode change
                } else {
                    LogToFile_Error("Error PART 1 (clear scene) setting state for " + log_target_entity_id +
                                    ": Status Code " + std::to_string(r_cpr_part1.status_code) + " - " +
                                    r_cpr_part1.error.message);
                    LogToFile_Error("HA Response Text PART 1 for " + log_target_entity_id + ": " + r_cpr_part1.text);
                    LogToConsole("ERROR: HA PART 1 for " + log_target_entity_id + ": Status Code " +
                                 std::to_string(r_cpr_part1.status_code));
                    // If Part 1 failed, we might not want to send Part 2, but for robustnes, we try anyway.
                }
            }
            // --- END NEW LOGIC ---

            // PART 2 (or standard call): Set the actual color/brightness/effect
            payload_json = json::object();  // Reset payload for this call
            payload_json["entity_id"] = light_state.entity_id;
            payload_json["rgb_color"] = light_state.rgb_color;
            payload_json["brightness_pct"] = light_state.brightness_pct;

            if (light_state.effect.has_value()) {  // If there's an explicit effect for this state (e.g. "colorloop")
                payload_json["effect"] = light_state.effect.value();
            }
            // If was_scene_effect was true, we explicitly set effect:"off" in Part 1.
            // We do NOT add "effect": "off" here again if it wasn't specified in light_state.effect,
            // as this part is about setting the *desired* final state, not the transition.

            std::string payload_final = payload_json.dump();
            LogToFile_Debug("Sending FINAL request to HA for " + log_target_entity_id + ": " + payload_final);
            cpr::Response r_cpr_final = cpr::Post(cpr::Url{service_url}, headers, cpr::Body{payload_final});

            if (r_cpr_final.status_code == 200) {
                LogToFile_Debug("Successfully set FINAL state for " + log_target_entity_id);
                success = true;  // Final command successful
            } else {
                LogToFile_Error("Error FINAL setting state for " + log_target_entity_id + ": Status Code " +
                                std::to_string(r_cpr_final.status_code) + " - " + r_cpr_final.error.message);
                LogToFile_Error("HA Response Text FINAL for " + log_target_entity_id + ": " + r_cpr_final.text);
                LogToConsole("ERROR: HA FINAL for " + log_target_entity_id + ": Status Code " +
                             std::to_string(r_cpr_final.status_code));
            }
        }

        // Update last commanded state ONLY if the command (or both parts of scene command) was successful
        if (success) {
            g_LastCommandedLightStates[light_state.entity_id] = light_state;
        }
    }
}

void ExportGameData() {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        LogToFile_Debug("Player not found, skipping data export.");
        // LogToConsole("HomeAssistantLink: Player not found, skipping data export."); // Too spammy if player isn't
        // active
        return;
    }

    float gameHour = RE::Calendar::GetSingleton()->gameHour->value;
    bool inCombat = player->IsInCombat();

    // Find the highest priority active scenario
    const Scenario *activeScenario = nullptr;
    int highestPriority = -1;  // Scenarios start at priority 0

    for (const auto &scenario : g_SCENARIOS) {
        bool triggerMet = false;

        if (scenario.trigger.type == "always") {
            triggerMet = true;
        } else if (scenario.trigger.type == "player_in_combat") {
            if (inCombat) {
                triggerMet = true;
            }
        } else if (scenario.trigger.type == "game_hour_range") {
            if (scenario.trigger.min_hour.has_value() && scenario.trigger.max_hour.has_value()) {
                int minH = scenario.trigger.min_hour.value();
                int maxH = scenario.trigger.max_hour.value();
                int currentHour = static_cast<int>(gameHour);

                if (minH <= maxH) {                                   // Normal range (e.g., 8 to 18)
                    if (currentHour >= minH || currentHour < maxH) {  // Corrected logic for range
                        triggerMet = true;
                    }
                } else {  // Cross-midnight range (e.g., 20 to 6)
                    if (currentHour >= minH || currentHour < maxH) {
                        triggerMet = true;
                    }
                }
            }
        }

        // If this scenario's trigger is met and it has a higher priority
        if (triggerMet) {
            if (scenario.priority > highestPriority) {
                highestPriority = scenario.priority;
                activeScenario = &scenario;
            }
        }
    }

    if (activeScenario) {
        LogToFile_Debug("Active Scenario: " + activeScenario->name +
                        " (Priority: " + std::to_string(activeScenario->priority) + ")");
        ApplyLightStates(activeScenario->outcome);
    } else {
        // Fallback or default state if no scenario matches (or explicitly handle via an "always" scenario with lowest
        // priority)
        LogToFile_Debug("No active scenario found. Lights remain in last state or default.");
    }
}

void PeriodicGameDataExportThread() {
    if (g_threadRunning.exchange(true)) {
        LogToFile_Warn("PeriodicGameDataExportThread attempted to start but was already running. Skipping.");
        LogToConsole("WARNING: PeriodicGameDataExportThread attempted to start but was already running. Skipping.");
        return;
    }

    LogToFile_Info("Periodic Game Data Export Thread started.");
    LogToConsole("HomeAssistantLink: Periodic Game Data Export Thread started.");  // This one should always appear

    std::this_thread::sleep_for(std::chrono::seconds(5));  // Initial delay

    while (true) {
        ExportGameData();
        std::this_thread::sleep_for(std::chrono::seconds(5));  // Adjust as needed
    }
    g_threadRunning.store(false);
}

// This is the SKSE plugin load function.
SKSEPluginLoad(const SKSE::LoadInterface *skse) {
    // 1. Initialize SKSE API first. This is still necessary for other SKSE services.
    SKSE::Init(skse);

    // 2. Set up our custom spdlog logger
    try {
        auto logsFolder = SKSE::log::log_directory();  // Get default SKSE log directory path
        if (logsFolder) {
            std::filesystem::path logFilePath = logsFolder->string();
            logFilePath /= LOG_FILE_NAME;  // Append our custom log file name

            // Create a file sink for our specific log file (true to truncate on start)
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);

            // Create our logger with the file sink. Use PLUGIN_NAME_STR as the logger name.
            // Set the log level. spdlog::level::trace will show everything.
            // You can change this to spdlog::level::debug, spdlog::level::info, etc.
            g_plugin_logger = std::make_shared<spdlog::logger>(PLUGIN_NAME_STR, file_sink);
            g_plugin_logger->set_level(spdlog::level::trace);

            // Flush messages immediately for easier debugging. Remove this in release builds for performance.
            g_plugin_logger->flush_on(spdlog::level::trace);

            // Optionally register it with spdlog's global registry (not strictly necessary if only you use it)
            spdlog::register_logger(g_plugin_logger);

            LogToConsole("HomeAssistantLink: Dedicated file logger initialized at: " + logFilePath.string());
        } else {
            // Fallback if log_directory isn't available
            LogToConsole(
                "HomeAssistantLink: WARNING: Could not determine SKSE log directory. Dedicated file logging disabled.");
            OutputDebugStringA(
                "HomeAssistantLink: WARNING - Could not determine SKSE log directory. Dedicated file logging "
                "disabled.\n");
        }
    } catch (const spdlog::spdlog_ex &ex) {
        LogToConsole("HomeAssistantLink: ERROR: Failed to set up custom file logger: " + std::string(ex.what()));
        OutputDebugStringA(
            ("HomeAssistantLink: ERROR - Failed to set up custom file logger: " + std::string(ex.what()) + "\n")
                .c_str());
    }

    LogToFile_Info("Plugin loading...");
    LogToFile_Info("SKSE API initialized.");

    // 3. Load Configuration
    if (!LoadConfiguration()) {
        LogToFile_Error("Failed to load configuration. Plugin will not function correctly.");
    }

    LogToFile_Info("Registering messaging listener.");

    // Register your data export thread to start when a save game is loaded (kPostLoadGame)
    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message *message) {
        if (message->type == SKSE::MessagingInterface::kPostLoadGame) {
            if (!g_threadRunning.load()) {  // Check if thread is NOT already running
                LogToFile_Info("Game loaded. Starting Home Assistant communication thread.");
                LogToConsole("HomeAssistantLink: Game loaded. Starting Home Assistant communication thread.");
                std::thread exportThread(PeriodicGameDataExportThread);
                exportThread.detach();  // Detach the thread so it runs independently
            } else {
                LogToFile_Info("Game loaded, but Home Assistant communication thread is already running.");
                LogToConsole(
                    "HomeAssistantLink: Game loaded, but HomeAssistantLink communication thread is already running.");
            }
        }
    });

    LogToFile_Info("Plugin loaded successfully.");
    return true;
}