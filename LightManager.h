#pragma once
#include <array>
#include <string>
#include <vector>

#include "ConfigLoader.h"

// Optionally, include other headers if you use cpr/json directly here

void ApplyLightStates(const std::vector<LightState>& light_states_to_apply);
void ApplyFlicker(std::array<int, 3>& rgb, int& brightness, const std::array<int, 3>& base_rgb, int base_brightness);
