#pragma once
#include <array>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

struct FlickerConfig {
    int r = 80;
    int g = 50;
    int b = 20;
    int brightness = 25;
};

struct LightState {
    std::string entity_id;
    std::array<int, 3> rgb_color;
    int brightness_pct;
    std::optional<std::string> effect;
    std::optional<std::string> scene;
    bool inherit = false;
    std::optional<FlickerConfig> flicker;

    // Comparison for state caching
    bool operator==(const LightState& other) const {
        return entity_id == other.entity_id && rgb_color == other.rgb_color && brightness_pct == other.brightness_pct &&
               effect == other.effect && scene == other.scene && inherit == other.inherit;
    }
};

struct ScenarioTrigger {
    std::string type;
    std::optional<std::string> condition;
    std::optional<int> min_hour;
    std::optional<int> max_hour;
    std::optional<std::string> range;
    std::optional<std::string> area;
    std::optional<std::string> item;
    std::optional<std::string> object_type;
    std::optional<int> radius;
};

struct Scenario {
    std::string name;
    int priority;
    ScenarioTrigger trigger;
    std::vector<LightState> outcome;
};

struct Vec3 {
    float x, y, z;
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const {
        float l = length();
        return (l > 0) ? Vec3{x / l, y / l, z / l} : Vec3{0, 0, 0};
    }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
};

struct RealLamp {
    std::string entity_id;
    Vec3 position;  // in your room, e.g. centimeters from center
};


struct DayNightKeyframe {
    int hour;
    std::array<int, 3> rgb_color;
    int brightness_pct;
};

// Config globals (extern!)
extern std::string g_HA_URL;
extern std::string g_HA_TOKEN;
extern std::vector<std::string> g_LIGHT_ENTITY_IDS;
extern std::vector<Scenario> g_SCENARIOS;
extern std::vector<RealLamp> g_RealLamps;
extern bool g_DebugMode; 
extern std::vector<DayNightKeyframe> g_DayNightCycle;

float GetPlayerCameraYawRadians();

extern float g_DirectionSharpness;  // Declaration only, NO initialization

// Config loader
bool LoadConfiguration();
std::filesystem::path GetCurrentModulePath();
