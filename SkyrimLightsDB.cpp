// SkyrimLightsDB.cpp
#include "SkyrimLightsDB.h"
#include "Logger.h"
#include <fstream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include <sstream>
#include <unordered_map>
#include <filesystem>

std::unordered_map<uint32_t, SkyrimLightDefinition> g_SkyrimLightDefs;
extern std::filesystem::path GetCurrentModulePath();

bool LoadSkyrimLightsDatabase() {
    // Resolve the path to the plugin DLL and look for lights.json next to it
    std::filesystem::path pluginPath = GetCurrentModulePath();
    if (pluginPath.empty()) {
        LogToFile_Error("Could not determine plugin DLL path for loading lights.json");
        return false;
    }
    std::filesystem::path lightsJsonPath = pluginPath.parent_path() / "lights.json";
    LogToFile_Info("Attempting to load Skyrim lights database from: " + lightsJsonPath.string());

    std::ifstream file(lightsJsonPath);
    if (!file.is_open()) {
        LogToFile_Error("Failed to open lights.json at " + lightsJsonPath.string());
        return false;
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (std::exception& e) {
        LogToFile_Error("Failed to parse lights.json: " + std::string(e.what()));
        return false;
    }

    g_SkyrimLightDefs.clear();
    for (const auto& entry : j) {
        SkyrimLightDefinition def;
        // Read formID as string, parse as hex
        if (entry.contains("formID")) {
            std::string formIDstr = entry["formID"];
            def.form_id = std::stoul(formIDstr, nullptr, 16);
        } else {
            continue;
        }
        def.editor_id = entry.value("editorID", "");
        def.name = entry.value("name", "");
        // Parse color as object
        if (entry.contains("color") && entry["color"].is_object()) {
            def.color_r = entry["color"].value("r", 255);
            def.color_g = entry["color"].value("g", 255);
            def.color_b = entry["color"].value("b", 255);
        } else {
            def.color_r = 255;
            def.color_g = 255;
            def.color_b = 255;
        }
        def.radius = entry.value("radius", 256);
        def.duration = entry.value("duration", 0.0f);
        def.fade = entry.value("fade", 0.0f);
        // ...add more as needed
        g_SkyrimLightDefs[def.form_id] = def;
    }

    LogToFile_Info("Successfully loaded " + std::to_string(g_SkyrimLightDefs.size()) +
                   " Skyrim lights from lights.json");
    return true;
}

const SkyrimLightDefinition* GetLightDefinitionByFormID(uint32_t formID) {
    auto it = g_SkyrimLightDefs.find(formID);
    if (it != g_SkyrimLightDefs.end()) return &(it->second);
    return nullptr;
}

