#include "LampMapping.h"

#include <algorithm>
#include <cmath>

#include "GameState.h"

// Helper: Calculate normalized dot product (direction similarity)
static float DirDot(const Vec3& a, const Vec3& b) {
    float la = a.length(), lb = b.length();
    if (la < 1e-6f || lb < 1e-6f) return 0.0f;
    return std::max(0.0f, a.dot(b) / (la * lb));
}

// Weighted blend from all in-game lights, using player yaw for alignment.
// realLamps: Your room lamps with .position relative to player (in cm).
// gameLights: Active in-game lights with world positions (in Skyrim units).
// playerYawRadians: Player's current view direction (in radians).
// maxDistance: Only lights within this distance influence the lamps.
std::vector<LightState> MapInGameLightsToRealLamps(const std::vector<RealLamp>& realLamps,
                                                   const std::vector<InGameLight>& gameLights, float playerYawRadians,
                                                   float maxDistance) {
    std::vector<LightState> result;

    // You can tweak this sharpness value for stronger focus; higher = more spotlight-like
    constexpr float DIRECTION_SHARPNESS = 2.0f;  // try 1.5–3.0 for your setup

    for (const auto& lamp : realLamps) {
        Vec3 lampDir = lamp.position.normalized();  // Direction from player to lamp

        float sumWeight = 0.0f;
        float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f, sumBrightness = 0.0f;

        for (const auto& gameLight : gameLights) {
            // Vector from player to in-game light (in Skyrim coordinates)
            Vec3 lightVec = gameLight.skyrim_pos;  // assumes player is at (0,0,0)
            // Rotate by -playerYaw so Skyrim "forward" matches real room "forward"
            Vec3 relVec = RotateVectorByYaw(lightVec, playerYawRadians);

            float dist = relVec.length();
            if (dist > maxDistance) continue;

            Vec3 lightDir = relVec.normalized();
            float dirRaw = std::max(0.0f, lampDir.dot(lightDir));        // [0..1]
            float dirAlignment = std::pow(dirRaw, DIRECTION_SHARPNESS);  // Sharper spotlight effect

            float distanceFade = 1.0f - std::clamp(dist / maxDistance, 0.0f, 1.0f);
            float weight = dirAlignment * distanceFade * gameLight.intensity;

            sumWeight += weight;
            sumR += weight * gameLight.color_r;
            sumG += weight * gameLight.color_g;
            sumB += weight * gameLight.color_b;
            sumBrightness += weight * 100.0f;  // You can adapt brightness mapping if needed
        }

        LightState ls;
        ls.entity_id = lamp.entity_id;

        if (sumWeight > 0.01f) {
            ls.rgb_color = {static_cast<int>(sumR / sumWeight), static_cast<int>(sumG / sumWeight),
                            static_cast<int>(sumB / sumWeight)};
            ls.brightness_pct = static_cast<int>(std::clamp(sumBrightness / sumWeight, 10.0f, 100.0f));
            ls.effect = "flicker";  // Or other effect if you want
            FlickerConfig flickConf;
            flickConf.r = 60;
            flickConf.g = 40;
            flickConf.b = 20;
            flickConf.brightness = 20;
            ls.flicker = flickConf;
        } else {
            // No relevant lights: fall back to inherit or off
            ls.inherit = true;
        }

        result.push_back(ls);
    }
    return result;
}

// Rotates a 2D vector (x, y) in the horizontal plane by yawRadians.
// Positive yaw rotates counter-clockwise.
// The Z coordinate is unchanged.
Vec3 RotateVectorByYaw(const Vec3& vec, float yawRadians) {
    float cosA = std::cos(-yawRadians);  // Negative to match Skyrim's rotation direction
    float sinA = std::sin(-yawRadians);

    float x_new = vec.x * cosA - vec.y * sinA;
    float y_new = vec.x * sinA + vec.y * cosA;

    return Vec3{x_new, y_new, vec.z};
}
