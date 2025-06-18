#include "GameState.h"

#include <RE/Skyrim.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

#include "ConfigLoader.h"
#include "LampMapping.h"
#include "LightManager.h"
#include "LightSmoother.h"
#include "Logger.h"
#include "SkyrimLightsDB.h"

// --- Helper: Get player camera yaw in radians (true view direction, 0 = world X+), normalized [0, 2pi) ---
float GetPlayerCameraYawRadians() {
    auto camera = RE::PlayerCamera::GetSingleton();
    if (camera && camera->cameraRoot) {
        auto node = camera->cameraRoot.get();
        if (node) {
            auto& rot = node->world.rotate;
            float fx = rot.entry[0][0];
            float fy = rot.entry[1][0];
            float yaw = std::atan2(fy, fx);
            if (yaw < 0.0f) yaw += 2.0f * static_cast<float>(3.14159265358979323846f);
            return yaw;
        }
    }
    // Fallback: player yaw
    auto player = RE::PlayerCharacter::GetSingleton();
    if (player) {
        float yawDegrees = player->GetAngleZ();
        float yawRadians = std::fmod(yawDegrees, 360.0f) * static_cast<float>(3.14159265358979323846f) / 180.0f;
        if (yawRadians < 0.0f) yawRadians += 2.0f * static_cast<float>(3.14159265358979323846f);
        return yawRadians;
    }
    return 0.0f;
}

// --- Helper: Is the player in an interior cell? ---
bool IsPlayerInInterior() {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return false;
    auto cell = player->GetParentCell();
    if (!cell) return false;
    return cell->IsInteriorCell();
}

// --- Helper: Find all light sources in range ---
std::vector<NearbyLightInfo> GetNearbyLights(float radius) {
    std::vector<NearbyLightInfo> result;
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return result;
    auto playerPos = player->GetPosition();
    auto cell = player->GetParentCell();
    if (cell) {
        cell->ForEachReference([&](RE::TESObjectREFR& ref) {
            RE::TESObjectREFR* refPtr = std::addressof(ref);
            if (!refPtr) return RE::BSContainer::ForEachResult::kContinue;
            auto base = refPtr->GetBaseObject();
            if (base && base->Is(RE::FormType::Light)) {
                uint32_t formID = base->GetFormID();
                const SkyrimLightDefinition* lightDef = GetLightDefinitionByFormID(formID);
                std::string editorID = lightDef ? lightDef->editor_id : "";
                auto lightPos = refPtr->GetPosition();
                float dist = (playerPos - lightPos).Length();
                std::tuple<int, int, int> rgb =
                    lightDef ? std::make_tuple(lightDef->color_r, lightDef->color_g, lightDef->color_b)
                             : std::make_tuple(255, 255, 255);
                float brightness = lightDef ? static_cast<float>(lightDef->radius) : 256.0f;
                if (dist <= radius) {
                    result.push_back(NearbyLightInfo{formID, editorID, lightPos, dist, rgb, brightness});
                }
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
    }
    return result;
}

// --- File-scope smoother ---
static LightSmoother g_smoother;

// --- Torch detection ---
bool IsTorchEquipped() {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return false;
    auto right = player->GetEquippedObject(false);  // false = right hand
    auto left = player->GetEquippedObject(true);    // true = left hand
    constexpr uint32_t TORCH_FORMID = 0x0001D4EC;
    auto isTorch = [](RE::TESForm* obj) -> bool { return obj && obj->GetFormID() == TORCH_FORMID; };
    return isTorch(right) || isTorch(left);
}

// --- Ambient (day/night) lighting from keyframes ---
LightState GetAmbientStateForHour(float gameHour, const std::string& entity_id) {
    if (g_DayNightCycle.size() < 2)
        return LightState{entity_id, {128, 128, 128}, 50, std::nullopt, std::nullopt, false, std::nullopt};
    float hour = std::fmod(gameHour, 24.0f);
    if (hour < 0.0f) hour += 24.0f;
    const DayNightKeyframe* kfA = nullptr;
    const DayNightKeyframe* kfB = nullptr;
    for (size_t i = 0; i < g_DayNightCycle.size(); ++i) {
        size_t next = (i + 1) % g_DayNightCycle.size();
        float hourA = static_cast<float>(g_DayNightCycle[i].hour);
        float hourB = static_cast<float>(g_DayNightCycle[next].hour);
        bool inSegment = false;
        if (hourA < hourB)
            inSegment = (hour >= hourA && hour < hourB);
        else
            inSegment = (hour >= hourA || hour < hourB);
        if (inSegment) {
            kfA = &g_DayNightCycle[i];
            kfB = &g_DayNightCycle[next];
            break;
        }
    }
    if (!kfA || !kfB) {
        kfA = &g_DayNightCycle[0];
        kfB = &g_DayNightCycle[1];
    }
    float hourA = static_cast<float>(kfA->hour);
    float hourB = static_cast<float>(kfB->hour);
    float t;
    if (hourA == hourB)
        t = 0.0f;
    else if (hourA < hourB)
        t = (hour - hourA) / (hourB - hourA);
    else {
        float len = (24.0f - hourA) + hourB;
        t = (hour >= hourA) ? (hour - hourA) / len : (hour + 24.0f - hourA) / len;
    }
    std::array<int, 3> rgb;
    for (int c = 0; c < 3; ++c) rgb[c] = static_cast<int>((1.0f - t) * kfA->rgb_color[c] + t * kfB->rgb_color[c]);
    int brightness = static_cast<int>((1.0f - t) * kfA->brightness_pct + t * kfB->brightness_pct);
    return LightState{entity_id, rgb, brightness, std::nullopt, std::nullopt, false, std::nullopt};
}

// --- Main Export Function ---
void ExportGameData() {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        LogToFile_Debug("Player not found, skipping data export.");
        return;
    }

    float gameHour = RE::Calendar::GetSingleton()->gameHour->value;
    bool inCombat = player->IsInCombat();
    bool isInterior = IsPlayerInInterior();

    // STEP 1: Dynamic/Proximity Lighting
    float radius = 400.0f;
    auto fires = GetNearbyLights(radius);
    std::vector<InGameLight> ingameLights;
    auto playerPos = player->GetPosition();

    for (const auto& l : fires) {
        Vec3 relPos = {l.position.x - playerPos.x, l.position.y - playerPos.y, l.position.z - playerPos.z};
        int r = 255, g = 140, b = 0;
        if (std::get<0>(l.rgb) != 0 || std::get<1>(l.rgb) != 0 || std::get<2>(l.rgb) != 0) {
            r = std::get<0>(l.rgb);
            g = std::get<1>(l.rgb);
            b = std::get<2>(l.rgb);
        }
        float brightness = l.brightness;
        ingameLights.push_back(InGameLight{relPos, "fire", r, g, b, brightness});
    }

    float playerYaw = GetPlayerCameraYawRadians();
    auto dynamicLampStates = MapInGameLightsToRealLamps(g_RealLamps, ingameLights, playerYaw, radius);

    // STEP 2: Get active scenario for torch/combat (no default/always/night/day scenarios anymore)
    std::vector<LightState> scenarioLampStates;
    const Scenario* activeScenario = nullptr;
    int highestPriority = -1;
    for (const auto& scenario : g_SCENARIOS) {
        bool triggerMet = false;
        if (scenario.trigger.type == "player_in_combat") {
            if (inCombat) triggerMet = true;
        } else if (scenario.trigger.type == "torch_equipped") {
            if (IsTorchEquipped()) triggerMet = true;
        }
        if (triggerMet && scenario.priority > highestPriority) {
            highestPriority = scenario.priority;
            activeScenario = &scenario;
        }
    }
    if (activeScenario) {
        scenarioLampStates = activeScenario->outcome;
    }

    // --- Use ambient (day/night) only if no high-prio scenario is active ---
    if (!activeScenario) {
        scenarioLampStates.clear();
        for (const auto& lamp : g_RealLamps) {
            if (!isInterior) {
                scenarioLampStates.push_back(GetAmbientStateForHour(gameHour, lamp.entity_id));
            } else {
                // In interiors, only use dynamic/proximity (fire), ambient = inherit
                LightState s;
                s.entity_id = lamp.entity_id;
                s.inherit = true;
                scenarioLampStates.push_back(s);
            }
        }
    }

    // STEP 3: Blend dynamic and scenario/ambient per lamp, with fire dominance
    std::vector<LightState> finalLampStates;
    for (size_t i = 0; i < dynamicLampStates.size(); ++i) {
        LightState dyn = dynamicLampStates[i];
        LightState scen = (i < scenarioLampStates.size()) ? scenarioLampStates[i] : dyn;

        float fire_influence = std::clamp(static_cast<float>(dyn.brightness_pct) / 100.0f, 0.0f, 1.0f);
        fire_influence = std::pow(fire_influence, 0.4f);

        if (fire_influence < 0.05f) fire_influence = 0.0f;
        if (fire_influence > 0.95f) fire_influence = 1.0f;

        float scenario_weight = 1.0f - fire_influence;

        LightState out = dyn;
        for (int c = 0; c < 3; ++c) {
            out.rgb_color[c] =
                static_cast<int>(fire_influence * dyn.rgb_color[c] + scenario_weight * scen.rgb_color[c]);
        }
        out.brightness_pct =
            static_cast<int>(fire_influence * dyn.brightness_pct + scenario_weight * scen.brightness_pct);

        out.rgb_color[0] = std::clamp(out.rgb_color[0], 0, 255);
        out.rgb_color[1] = std::clamp(out.rgb_color[1], 0, 255);
        out.rgb_color[2] = std::clamp(out.rgb_color[2], 0, 255);
        out.brightness_pct = std::clamp(out.brightness_pct, 10, 100);

        if (scen.inherit) out = dyn;

        finalLampStates.push_back(out);
    }

    // STEP 4: Smoothing
    std::vector<LightState> smoothedStates = g_smoother.SmoothStates(finalLampStates, 0.2f);

    LogToFile_Debug("Blended dynamic+ambient/scenario mapping (per-lamp fire_influence): " +
                    std::to_string(smoothedStates.size()) + " lamps.");
    ApplyLightStates(smoothedStates);
}
