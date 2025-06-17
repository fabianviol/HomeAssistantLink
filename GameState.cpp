#include "GameState.h"
#include "Logger.h"
#include "SkyrimLightsDB.h"
#include <RE/Skyrim.h>
#include <vector>
#include <string>
#include "LightManager.h"
#include <fstream>
#include <cmath>
#include "LampMapping.h" // ADD THIS for mapping logic
#include <array>


// Get all light references near the player within a given radius.
// Returns a vector of structs with position, FormID, EditorID, and maybe RGB/brightness.
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


bool IsTorchEquipped() {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return false;

    auto right = player->GetEquippedObject(false);  // false = right hand
    auto left = player->GetEquippedObject(true);    // true = left hand

    // Torch FormID (Skyrim.esm: 0001D4EC)
    constexpr uint32_t TORCH_FORMID = 0x0001D4EC;

    auto isTorch = [](RE::TESForm *obj) -> bool { return obj && obj->GetFormID() == TORCH_FORMID; };

    return isTorch(right) || isTorch(left);
}

void ExportGameData() {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        LogToFile_Debug("Player not found, skipping data export.");
        return;
    }

    float gameHour = RE::Calendar::GetSingleton()->gameHour->value;
    bool inCombat = player->IsInCombat();

    // Find the highest priority active scenario
    const Scenario* activeScenario = nullptr;
    int highestPriority = -1;

    // Track if we handled via dynamic lamp logic
    bool ran_dynamic_lighting = false;

    for (const auto& scenario : g_SCENARIOS) {
        bool triggerMet = false;

        if (scenario.trigger.type == "always") {
            triggerMet = true;
        } else if (scenario.trigger.type == "player_in_combat") {
            if (inCombat) triggerMet = true;
        } else if (scenario.trigger.type == "game_hour_range") {
            if (scenario.trigger.min_hour.has_value() && scenario.trigger.max_hour.has_value()) {
                int minH = scenario.trigger.min_hour.value();
                int maxH = scenario.trigger.max_hour.value();
                int currentHour = static_cast<int>(gameHour);

                if (minH <= maxH) {
                    if (currentHour >= minH && currentHour < maxH) triggerMet = true;
                } else {  // Over midnight (e.g. 20-6)
                    if (currentHour >= minH || currentHour < maxH) triggerMet = true;
                }
            }
        } else if (scenario.trigger.type == "torch_equipped") {
            if (IsTorchEquipped()) triggerMet = true;
        }
        // --- Dynamic: near_object trigger ---
        else if (scenario.trigger.type == "near_object" && scenario.trigger.object_type == "fire" &&
                 scenario.trigger.radius > 0) {
            // Check if any fires are near enough
            auto fires = GetNearbyLights(scenario.trigger.radius.value_or(400));
            if (!fires.empty()) triggerMet = true;
        }

        // Find the highest-priority matching scenario
        if (triggerMet && scenario.priority > highestPriority) {
            highestPriority = scenario.priority;
            activeScenario = &scenario;
        }
    }

    // --- Handle dynamic scenario ---
    if (activeScenario && activeScenario->trigger.type == "near_object" &&
        activeScenario->trigger.object_type == "fire" && activeScenario->trigger.radius > 0) {
        // Get nearby fire lights
        auto fires = GetNearbyLights(activeScenario->trigger.radius.value_or(400));
        std::vector<InGameLight> ingameLights;
        auto playerPos = player->GetPosition();

        for (const auto& l : fires) {
            // Relative position from player
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

        // Map in-game fire lights to real-world lamps (dynamic lighting!)
        auto dynamicLampStates =
            MapInGameLightsToRealLamps(g_RealLamps, ingameLights, activeScenario->trigger.radius.value_or(400));

        LogToFile_Debug("Dynamic lamp mapping for fires: " + std::to_string(dynamicLampStates.size()) + " lamps.");
        ApplyLightStates(dynamicLampStates);
        ran_dynamic_lighting = true;
    }

    // --- Otherwise: fallback to standard scenario logic ---
    if (!ran_dynamic_lighting) {
        if (activeScenario) {
            LogToFile_Debug("Active Scenario: " + activeScenario->name +
                            " (Priority: " + std::to_string(activeScenario->priority) + ")");
            ApplyLightStates(activeScenario->outcome);
        } else {
            LogToFile_Debug("No active scenario found. Lights remain in last state or default.");
        }
    }
}
