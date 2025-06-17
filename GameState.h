#pragma once
#include "ConfigLoader.h"
#include <nlohmann/json.hpp>
#include <unordered_map>

// Any extra includes (e.g., <vector>, <string>, etc.)

// Prototype for your main export function
void ExportGameData();

// Later: add functions like IsPlayerNearFire(), IsTorchEquipped(), etc.

bool IsTorchEquipped();

struct NearbyLightInfo {
    uint32_t formID;
    std::string editorID;
    RE::NiPoint3 position;
    float distance;
    std::tuple<int, int, int> rgb;
    float brightness;
};

extern std::vector<RealLamp> g_RealLamps;

// Utility to load all light defs from JSON (declaration)
bool LoadSkyrimLightsDatabase(const std::string& path);