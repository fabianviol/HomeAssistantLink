#pragma once
#include <array>
#include <string>
#include <unordered_map>

#include "LightManager.h"  // For LightState

// Structure for tracking the transition for each lamp
struct SmoothLampState {
    std::array<int, 3> rgb_color;
    int brightness_pct;
    std::string effect;
    bool inherit;
};

// Smoother class for all lamps
class LightSmoother {
public:
    // Smoothly transitions from previous state to target state for all lamps
    std::vector<LightState> SmoothStates(const std::vector<LightState>& newStates, float smoothingFactor = 0.2f);

private:
    std::unordered_map<std::string, SmoothLampState> previousStates;
};
