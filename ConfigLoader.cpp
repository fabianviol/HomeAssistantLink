#include "ConfigLoader.h"

#include <Windows.h>  // for MAX_PATH and HMODULE

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "Logger.h"
#include "SkyrimLightsDB.h"
using json = nlohmann::json;

const std::string CONFIG_FILE_NAME = "HomeAssistantLink.json";
float g_DirectionSharpness = 2.0f;

// Define globals
std::string g_HA_URL;
std::string g_HA_TOKEN;
std::vector<std::string> g_LIGHT_ENTITY_IDS;
std::vector<Scenario> g_SCENARIOS;
std::vector<RealLamp> g_RealLamps;
bool g_DebugMode = false;
std::vector<DayNightKeyframe> g_DayNightCycle;  // New for day/night curve!

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
        LogToConsole("ERROR: Could not determine plugin path. Cannot load configuration file.");
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

        // --- Lighting Options (Direction Sharpness etc) ---
        if (config.contains("LightingOptions") && config["LightingOptions"].is_object()) {
            auto &lo = config["LightingOptions"];
            if (lo.contains("directionSharpness") && lo["directionSharpness"].is_number()) {
                g_DirectionSharpness = lo["directionSharpness"].get<float>();
                LogToFile_Info("Loaded directionSharpness: " + std::to_string(g_DirectionSharpness));
                NotifyIngame("Loaded directionSharpness: " + std::to_string(g_DirectionSharpness));
            } else {
                g_DirectionSharpness = 2.0f;
                LogToFile_Info("directionSharpness not set, using default 2.0");
            }
        } else {
            g_DirectionSharpness = 2.0f;
            LogToFile_Info("No LightingOptions section, using default directionSharpness 2.0");
        }

        // --- NEW: Parse DayNightCycle for dynamic ambient ---
        g_DayNightCycle.clear();
        if (config.contains("DayNightCycle") && config["DayNightCycle"].is_array()) {
            for (const auto &kf : config["DayNightCycle"]) {
                DayNightKeyframe key;
                key.hour = kf.at("hour").get<int>();
                key.rgb_color = kf.at("rgb_color").get<std::array<int, 3>>();
                key.brightness_pct = kf.at("brightness_pct").get<int>();
                g_DayNightCycle.push_back(key);
            }
            std::sort(g_DayNightCycle.begin(), g_DayNightCycle.end(),
                      [](const DayNightKeyframe &a, const DayNightKeyframe &b) { return a.hour < b.hour; });
            LogToFile_Info("Loaded DayNightCycle with " + std::to_string(g_DayNightCycle.size()) + " keyframes.");
        } else {
            LogToFile_Warn("No DayNightCycle found in config; ambient lighting will be static!");
        }

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
                g_DebugMode = false;
            }

        } else {
            LogToFile_Warn("'HomeAssistant' section not found or not an object in config.");
            g_HA_URL = "";
            g_HA_TOKEN = "";
            g_DebugMode = false;
        }

        // --- Parse real lamp positions for directional lighting ---
        g_RealLamps.clear();
        if (config.contains("Lights") && config["Lights"].is_array()) {
            for (const auto &lampJson : config["Lights"]) {
                if (!lampJson.contains("entity_id") || !lampJson.contains("position")) continue;
                const auto &pos = lampJson["position"];
                RealLamp lamp;
                lamp.entity_id = lampJson["entity_id"].get<std::string>();
                lamp.position.x = pos.value("x", 0.0f);
                lamp.position.y = pos.value("y", 0.0f);
                lamp.position.z = pos.value("z", 0.0f);
                g_RealLamps.push_back(lamp);
            }
            LogToFile_Info("Loaded " + std::to_string(g_RealLamps.size()) +
                           " lamp positions for directional lighting.");
            NotifyIngame("Loaded " + std::to_string(g_RealLamps.size()) + " lamp positions for directional lighting.");
        } else {
            LogToFile_Warn(
                "'Lights' array not found or not an array for lamp positions. Directional lighting will be disabled.");
            NotifyIngame(
                "'Lights' array not found or not an array for lamp positions. Directional lighting will be disabled.");
            g_RealLamps.clear();
        }

        // --- Read Scenarios array with 'inherit' support (for combat, torch, etc) ---
        g_SCENARIOS.clear();
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

                        // --- Inherit support ---
                        if (light_state_json.contains("inherit") && light_state_json["inherit"].is_boolean() &&
                            light_state_json["inherit"].get<bool>() == true) {
                            light_state.inherit = true;
                            light_state.entity_id = light_state_json.at("entity_id").get<std::string>();
                            scenario.outcome.push_back(light_state);
                            continue;
                        }

                        // Regular outcome parsing
                        light_state.entity_id = light_state_json.at("entity_id").get<std::string>();
                        light_state.rgb_color = light_state_json.at("rgb_color").get<std::array<int, 3>>();
                        light_state.brightness_pct = light_state_json.at("brightness_pct").get<int>();

                        // effect is optional
                        if (light_state_json.contains("effect")) {
                            if (light_state_json["effect"].is_string()) {
                                light_state.effect = light_state_json["effect"].get<std::string>();
                            } else if (light_state_json["effect"].is_null()) {
                                light_state.effect = std::nullopt;
                            }
                        } else {
                            light_state.effect = std::nullopt;
                        }

                        // Flicker config (optional)
                        if (light_state_json.contains("flicker") && light_state_json["flicker"].is_object()) {
                            FlickerConfig flickerConf;
                            auto &f = light_state_json["flicker"];
                            if (f.contains("r")) flickerConf.r = f["r"].get<int>();
                            if (f.contains("g")) flickerConf.g = f["g"].get<int>();
                            if (f.contains("b")) flickerConf.b = f["b"].get<int>();
                            if (f.contains("brightness")) flickerConf.brightness = f["brightness"].get<int>();
                            light_state.flicker = flickerConf;
                        }

                        // scene is optional
                        if (light_state_json.contains("scene") && light_state_json["scene"].is_string()) {
                            light_state.scene = light_state_json["scene"].get<std::string>();
                        } else {
                            light_state.scene = std::nullopt;
                        }

                        // Warn if effect is 'scene' but no scene name provided
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
            NotifyIngame("Loaded " + std::to_string(g_SCENARIOS.size()) + " scenarios.");
        } else {
            LogToFile_Warn("'Scenarios' array not found or not an array in config. No scenarios configured.");
            g_SCENARIOS.clear();
        }

        // --- END NEW SECTION ---

        // Final config check
        if (g_HA_URL.empty() || g_HA_TOKEN.empty() || g_RealLamps.empty()) {
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
