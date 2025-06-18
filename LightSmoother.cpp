#include "LightSmoother.h"

#include <algorithm>

// Helper to linearly interpolate between a and b by t
inline int lerp(int a, int b, float t) { return static_cast<int>(a + (b - a) * t); }

std::vector<LightState> LightSmoother::SmoothStates(const std::vector<LightState>& newStates, float smoothingFactor) {
    std::vector<LightState> result;

    for (const auto& state : newStates) {
        SmoothLampState& prev = previousStates[state.entity_id];

        // If first run or inherit is true, jump instantly
        if (prev.effect.empty() || state.inherit) {
            prev.rgb_color = state.rgb_color;
            prev.brightness_pct = state.brightness_pct;
            prev.effect = state.effect.value_or("");
            prev.inherit = state.inherit;
        } else {
            for (size_t i = 0; i < 3; ++i) {
                prev.rgb_color[i] = lerp(prev.rgb_color[i], state.rgb_color[i], smoothingFactor);
            }
            prev.brightness_pct = lerp(prev.brightness_pct, state.brightness_pct, smoothingFactor);
            prev.effect = state.effect.value_or(prev.effect);
            prev.inherit = state.inherit;
        }

        LightState smoothed = state;
        smoothed.rgb_color = prev.rgb_color;
        smoothed.brightness_pct = prev.brightness_pct;

        result.push_back(smoothed);
    }
    return result;
}
