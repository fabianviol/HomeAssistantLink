#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

struct SkyrimLightDefinition {
    std::string editor_id;
    std::string name;
    uint32_t form_id;  
    int color_r = 255, color_g = 255, color_b = 255;
    int radius = 256;
    float duration = 0.0f;
    float fade = 0.0f;
    // Add more fields if your JSON includes them!
};

extern std::unordered_map<uint32_t, SkyrimLightDefinition> g_SkyrimLightDefs;

bool LoadSkyrimLightsDatabase();
const SkyrimLightDefinition* GetLightDefinitionByFormID(uint32_t formID);
