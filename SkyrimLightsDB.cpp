// SkyrimLightsDB.cpp
#include "SkyrimLightsDB.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>

std::unordered_map<uint32_t, SkyrimLightDefinition> g_SkyrimLightDefs;

bool LoadSkyrimLightsDatabase(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        // Optional: Use your logger
        // LogToFile_Error("Failed to open lights.json at " + path);
        return false;
    }
    nlohmann::json j;
    try {
        file >> j;
    } catch (std::exception& e) {
        // LogToFile_Error("Failed to parse lights.json: " + std::string(e.what()));
        return false;
    }

    for (const auto& entry : j) {
        SkyrimLightDefinition def;
        // --- Handle form_id as hex string ("0001D4EC") or decimal (if ever needed)
        if (entry.contains("form_id")) {
            std::string formIDstr = entry["form_id"];
            def.form_id = std::stoul(formIDstr, nullptr, 16);
        } else {
            continue;  // skip if no form_id
        }

        def.editor_id = entry.value("editor_id", "");
        def.name = entry.value("name", "");
        def.color_r = entry.value("color_r", 255);
        def.color_g = entry.value("color_g", 255);
        def.color_b = entry.value("color_b", 255);
        def.radius = entry.value("radius", 256);
        def.duration = entry.value("duration", 0.0f);
        def.fade = entry.value("fade", 0.0f);
        // ...add more fields as your JSON grows

        g_SkyrimLightDefs[def.form_id] = def;
    }
    return true;
}

const SkyrimLightDefinition* GetLightDefinitionByFormID(uint32_t formID) {
    auto it = g_SkyrimLightDefs.find(formID);
    if (it != g_SkyrimLightDefs.end()) return &(it->second);
    return nullptr;
}
