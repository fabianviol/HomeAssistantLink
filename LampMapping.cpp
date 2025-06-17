#include "LampMapping.h"
#include "GameState.h" 
#include <algorithm>
#include <cmath>

// Helper: Calculate normalized dot product (direction similarity)
static float DirDot(const Vec3& a, const Vec3& b) {
    float la = a.length(), lb = b.length();
    if (la < 1e-6f || lb < 1e-6f) return 0.0f;
    return std::max(0.0f, a.dot(b) / (la * lb));
}

std::vector<LightState> MapInGameLightsToRealLamps(const std::vector<RealLamp>& realLamps,
                                                   const std::vector<InGameLight>& gameLights, float maxDistance) {
    std::vector<LightState> result;
    // Player at (0,0,0) in both spaces
    for (const auto& lamp : realLamps) {
        Vec3 lampDir = lamp.position.normalized();
        float bestEffect = 0.0f;
        int out_r = 255, out_g = 140, out_b = 0;  // Fire orange default

        for (const auto& gameLight : gameLights) {
            // Direction from player to in-game light
            Vec3 toLight = (gameLight.skyrim_pos - Vec3{0, 0, 0});  // player is at (0,0,0)
            float dist = toLight.length();

            if (dist > maxDistance) continue;

            float directionEffect = DirDot(lampDir, toLight);
            // Distance fade: e.g. fade to 0 at maxDistance
            float distanceFade = 1.0f - std::clamp(dist / maxDistance, 0.0f, 1.0f);
            float effect = directionEffect * distanceFade * (gameLight.intensity);

            if (effect > bestEffect) {
                bestEffect = effect;
                out_r = gameLight.color_r;
                out_g = gameLight.color_g;
                out_b = gameLight.color_b;
            }
        }
        if (bestEffect > 0.05f) {  // Only if visible
            LightState ls;
            ls.entity_id = lamp.entity_id;
            ls.rgb_color = {out_r, out_g, out_b};
            ls.brightness_pct = static_cast<int>(std::clamp(bestEffect * 100, 10.0f, 100.0f));
            ls.effect = "flicker";
            FlickerConfig flickConf;
            flickConf.r = 120;
            flickConf.g = 80;
            flickConf.b = 20;
            flickConf.brightness = 40;
            ls.flicker = flickConf;
            result.push_back(ls);
        } else {
            LightState ls;
            ls.entity_id = lamp.entity_id;
            ls.inherit = true;
            result.push_back(ls);
        }
    }
    return result;
}
