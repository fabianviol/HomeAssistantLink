#include <Windows.h>       // For MAX_PATH, HMODULE, GetModuleHandleExA, GetModuleFileNameA
#include <libloaderapi.h>  // Often included by Windows.h, but explicit for clarity

#include <array>   // For std::array if needed
#include <atomic>  // For std::atomic for thread safety
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>  // For std::string
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

// Global flag to track if the data export thread is already running
std::atomic<bool> g_threadRunning = false;

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

        // Read Lights array
        if (config.contains("Lights") && config["Lights"].is_array()) {
            g_LIGHT_ENTITY_IDS = config["Lights"].get<std::vector<std::string>>();
            LogToFile_Info("Loaded " + std::to_string(g_LIGHT_ENTITY_IDS.size()) + " light entity IDs.");
        } else {
            LogToFile_Warn("'Lights' array not found or not an array in config. No lights configured.");
            g_LIGHT_ENTITY_IDS.clear();
        }

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
void SetLightColor(int r, int g, int b) {
    if (g_HA_URL.empty() || g_HA_TOKEN.empty() || g_LIGHT_ENTITY_IDS.empty()) {
        LogToFile_Error("Cannot send light command. Configuration not loaded or incomplete.");
        LogToConsole("ERROR: Cannot send light command. Configuration not loaded or incomplete.");
        return;
    }
    std::string service_url = g_HA_URL + "/api/services/light/turn_on";

    cpr::Header headers;
    headers["Authorization"] = "Bearer " + g_HA_TOKEN;
    headers["Content-Type"] = "application/json";

    for (const auto &entity_id : g_LIGHT_ENTITY_IDS) {
        json payload_json;
        payload_json["entity_id"] = entity_id;
        payload_json["rgb_color"] = {r, g, b};
        std::string payload = payload_json.dump();

        LogToFile_Debug("Sending request to HA for " + entity_id + ": " + payload);
        LogToConsole("Sending request to HA for " + entity_id + ": " + payload);

        cpr::Response r_cpr = cpr::Post(cpr::Url{service_url}, headers, cpr::Body{payload});

        if (r_cpr.status_code == 200) {
            LogToFile_Debug("Light " + entity_id + " color set to RGB(" + std::to_string(r) + "," + std::to_string(g) +
                            "," + std::to_string(b) + "). Response: " + std::to_string(r_cpr.status_code));
            LogToConsole("Light " + entity_id + " color set to RGB(" + std::to_string(r) + "," + std::to_string(g) +
                         "," + std::to_string(b) + ").");
        } else {
            LogToFile_Error("Error setting light color for " + entity_id + ": Status Code " +
                            std::to_string(r_cpr.status_code) + " - " + r_cpr.error.message);
            LogToFile_Error("HA Response Text for " + entity_id + ": " + r_cpr.text);
            LogToConsole("ERROR: Error setting light color for " + entity_id + ": Status Code " +
                         std::to_string(r_cpr.status_code) + " - " + r_cpr.error.message);
            LogToConsole("ERROR: HA Response Text for " + entity_id + ": " + r_cpr.text);
        }
    }
}

void ExportGameData() {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        LogToFile_Debug("Player not found, skipping data export.");
        LogToConsole("HomeAssistantLink: Player not found, skipping data export.");
        return;
    }

    float gameHour = RE::Calendar::GetSingleton()->gameHour->value;
    bool inCombat = player->IsInCombat();
    std::string locationName = "Unknown";
    if (auto loc = player->GetCurrentLocation(); loc && loc->GetName()) {
        locationName = loc->GetName();
    }

    if (inCombat) {
        SetLightColor(255, 0, 0);  // Red
    } else {
        SetLightColor(0, 255, 0);  // Green
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
            g_plugin_logger = std::make_shared<spdlog::logger>(PLUGIN_NAME_STR, file_sink);

            // Set the log level. spdlog::level::trace will show everything.
            // You can change this to spdlog::level::debug, spdlog::level::info, etc.
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
                    "HomeAssistantLink: Game loaded, but Home Assistant communication thread is already running.");
            }
        }
    });

    LogToFile_Info("Plugin loaded successfully.");
    return true;
}