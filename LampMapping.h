#pragma once
#include <string>
#include <vector>

#include "GameState.h"     // For Vec3, RealLamp, etc.
#include "LightManager.h"  // For LightState, FlickerConfig

// Represents an in-game light source relevant to mapping
struct InGameLight {
    Vec3 skyrim_pos;   // In-game position (world coords)
    std::string type;  // e.g. "fire", "magic", etc.
    int color_r, color_g, color_b;
    float intensity;
};

std::vector<LightState> MapInGameLightsToRealLamps(const std::vector<RealLamp>& realLamps,
                                                   const std::vector<InGameLight>& gameLights,
                                                   float maxDistance = 400.0f);
