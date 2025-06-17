#include "LightManager.h"
#include "Logger.h"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include <map>
#include <random>
#include <algorithm>
#include <thread>

// ADDED: Global map to store the last commanded state for each light (for state tracking)
std::map<std::string, LightState> g_LastCommandedLightStates;

// Helper for random flicker (call each update)
// Improved flicker: stays close to base color/brightness!
void ApplyFlicker(
    std::array<int, 3> &rgb, 
    int &brightness, 
    const std::array<int, 3> &base_rgb, 
    int base_brightness,
    const FlickerConfig &config = FlickerConfig{}
    ) {
    static std::random_device rd;
    static std::mt19937 rng(rd());
    std::uniform_int_distribution<int> r_dist(-config.r, config.r);
    std::uniform_int_distribution<int> g_dist(-config.g, config.g);
    std::uniform_int_distribution<int> b_dist(-config.b, config.b);

    rgb[0] = std::clamp(base_rgb[0] + r_dist(rng), 0, 255);
    rgb[1] = std::clamp(base_rgb[1] + g_dist(rng), 0, 255);
    rgb[2] = std::clamp(base_rgb[2] + b_dist(rng), 0, 255);

    std::uniform_int_distribution<int> bright_dist(-config.brightness, config.brightness);
    int flicker = bright_dist(rng);
    brightness = std::clamp(base_brightness + flicker, 10, 100);
}

void ApplyLightStates(const std::vector<LightState> &light_states_to_apply) {
    if (g_HA_URL.empty() || g_HA_TOKEN.empty()) {
        LogToFile_Error("Cannot send light command. Home Assistant URL or Token not loaded.");
        LogToConsole("ERROR: Cannot send light command. HA config incomplete.");
        return;
    }

    cpr::Header headers;
    headers["Authorization"] = "Bearer " + g_HA_TOKEN;
    headers["Content-Type"] = "application/json";

    // Separate scene and non-scene lights, skip inherit lights here (handled in scenario resolution logic)
    std::vector<const LightState *> scene_lights;
    std::vector<const LightState *> normal_lights;

    for (const auto &light_state : light_states_to_apply) {
        if (light_state.inherit) {
            // Skip here; assume inherit logic is handled elsewhere
            continue;
        }
        if (light_state.effect.has_value() && light_state.effect.value() == "scene" && light_state.scene.has_value()) {
            scene_lights.push_back(&light_state);
        } else {
            normal_lights.push_back(&light_state);
        }
    }

    // --- SCENE LIGHTS ---

    // PART 1: Send effect=scene for all at once
    std::map<std::string, bool> scene_part1_success;  // Track success for each entity
    for (const auto *light_state : scene_lights) {
        std::string service_url = g_HA_URL + "/api/services/light/turn_on";
        json payload_json = {{"entity_id", light_state->entity_id}, {"effect", light_state->effect.value()}};

        LogToFile_Debug("Sending PART 1 (effect=scene) request to HA for " + light_state->entity_id + ": " +
                        payload_json.dump());
        cpr::Response r = cpr::Post(cpr::Url{service_url}, headers, cpr::Body{payload_json.dump()});
        if (r.status_code == 200) {
            LogToFile_Debug("Successfully sent PART 1 command for " + light_state->entity_id);
            scene_part1_success[light_state->entity_id] = true;
        } else {
            LogToFile_Error("Error PART 1 (effect=scene) for " + light_state->entity_id + ": Status Code " +
                            std::to_string(r.status_code) + " - " + r.error.message);
            LogToFile_Error("HA Response Text PART 1 for " + light_state->entity_id + ": " + r.text);
            LogToConsole("ERROR: HA PART 1 for " + light_state->entity_id + ": Status Code " +
                         std::to_string(r.status_code));
            scene_part1_success[light_state->entity_id] = false;
        }
    }

    // Wait 200ms ONCE for all
    if (!scene_lights.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // PART 2: Send select_option for all at once
    for (const auto *light_state : scene_lights) {
        if (!scene_part1_success[light_state->entity_id]) {
            LogToFile_Warn("Skipping PART 2 for " + light_state->entity_id + " due to failed PART 1.");
            continue;
        }
        std::string light_object_id = light_state->entity_id;
        if (light_object_id.rfind("light.", 0) == 0) {
            light_object_id = light_object_id.substr(6);
        }
        std::string select_entity_id = "select." + light_object_id + "_scene";
        std::string service_url = g_HA_URL + "/api/services/select/select_option";
        json payload_json = {{"entity_id", select_entity_id}, {"option", light_state->scene.value()}};

        LogToFile_Debug("Sending PART 2 (select_option) request to HA for " + select_entity_id + ": " +
                        payload_json.dump());
        cpr::Response r = cpr::Post(cpr::Url{service_url}, headers, cpr::Body{payload_json.dump()});
        if (r.status_code == 200) {
            LogToFile_Debug("Successfully sent PART 2 command for " + select_entity_id);
            g_LastCommandedLightStates[light_state->entity_id] = *light_state;
        } else {
            LogToFile_Error("Error PART 2 (select_option) for " + select_entity_id + ": Status Code " +
                            std::to_string(r.status_code) + " - " + r.error.message);
            LogToFile_Error("HA Response Text PART 2 for " + select_entity_id + ": " + r.text);
            LogToConsole("ERROR: HA PART 2 for " + select_entity_id + ": Status Code " + std::to_string(r.status_code));
        }
    }

    // --- NORMAL (non-scene) LIGHTS ---
    for (const auto *light_state : normal_lights) {
        // --- Check if previous state was a scene effect ---
        bool was_scene_effect = false;
        if (g_LastCommandedLightStates.count(light_state->entity_id)) {
            const auto &last_state = g_LastCommandedLightStates[light_state->entity_id];
            if (last_state.effect.has_value() && last_state.effect.value() == "scene") {
                was_scene_effect = true;
            }
        }
        std::string service_url = g_HA_URL + "/api/services/light/turn_on";
        bool success = false;

        // --- Clear scene effect if needed ---
        if (was_scene_effect) {
            json part1_payload = {{"entity_id", light_state->entity_id}, {"effect", "off"}};
            LogToFile_Debug("Sending PART 1 (clear scene, effect=\"off\") request to HA for " + light_state->entity_id +
                            ": " + part1_payload.dump());
            cpr::Response r = cpr::Post(cpr::Url{service_url}, headers, cpr::Body{part1_payload.dump()});
            if (r.status_code == 200) {
                LogToFile_Debug("Successfully sent PART 1 (clear scene) command for " + light_state->entity_id);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            } else {
                LogToFile_Error("Error PART 1 (clear scene) for " + light_state->entity_id + ": Status Code " +
                                std::to_string(r.status_code) + " - " + r.error.message);
                LogToFile_Error("HA Response Text PART 1 for " + light_state->entity_id + ": " + r.text);
                LogToConsole("ERROR: HA PART 1 (clear scene) for " + light_state->entity_id + ": Status Code " +
                             std::to_string(r.status_code));
            }
        }

        // --- Flicker/Animated effect (keeps color close to base) ---
        std::array<int, 3> rgb = light_state->rgb_color;
        int brightness = light_state->brightness_pct;

        bool isAnimated = light_state->effect.has_value() &&
                          (light_state->effect.value() == "flicker"
                           // || light_state->effect.value() == "pulse"   // Add more animation types here as needed!
                          );
        if (isAnimated) {
            ApplyFlicker(rgb, brightness, light_state->rgb_color, light_state->brightness_pct,
                         light_state->flicker.value_or(FlickerConfig{}));
        }

        // --- Only skip for non-animated/static states ---
        if (!isAnimated && g_LastCommandedLightStates.count(light_state->entity_id) &&
            g_LastCommandedLightStates[light_state->entity_id] == *light_state) {
            LogToFile_Debug("Light " + light_state->entity_id + " is already in the desired state. Skipping command.");
            continue;
        }

        // PART 2 (standard call): Set the actual color/brightness/effect
        json payload_json = {{"entity_id", light_state->entity_id}, {"rgb_color", rgb}, {"brightness_pct", brightness}};
        if (light_state->effect.has_value() && !isAnimated) {
            payload_json["effect"] = light_state->effect.value();
        }

        LogToFile_Debug("Sending FINAL request to HA for " + light_state->entity_id + ": " + payload_json.dump());
        cpr::Response r = cpr::Post(cpr::Url{service_url}, headers, cpr::Body{payload_json.dump()});
        if (r.status_code == 200) {
            LogToFile_Debug("Successfully set FINAL state for " + light_state->entity_id);
            success = true;
        } else {
            LogToFile_Error("Error FINAL setting state for " + light_state->entity_id + ": Status Code " +
                            std::to_string(r.status_code) + " - " + r.error.message);
            LogToFile_Error("HA Response Text FINAL for " + light_state->entity_id + ": " + r.text);
            LogToConsole("ERROR: HA FINAL for " + light_state->entity_id + ": Status Code " +
                         std::to_string(r.status_code));
        }

        if (success) {
            g_LastCommandedLightStates[light_state->entity_id] = *light_state;
        }
    }
}
