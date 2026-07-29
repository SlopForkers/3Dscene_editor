#pragma once
#include <glm/glm.hpp>
#include <cmath>

// ---------------------------------------------------------------------------
// Weather parameters (GL-free, unit-tested) + the particle system for
// precipitation. The scene file stores these; the game applies them.
// Fog is integrated into the terrain/prop/block shaders via uniforms
// (uFogColor/uFogDensity/uLightScale); precipitation is rendered by
// WeatherSystem as camera-wrapped particles (rain streaks / snow flakes).
// ---------------------------------------------------------------------------

struct WeatherParams {
    enum Preset : int { Clear = 0, Overcast, Rain, Snow, Fog, Custom };
    enum Precip : int { PrecipNone = 0, PrecipRain, PrecipSnow };

    Preset preset = Clear;
    Precip precip = PrecipNone;
    float precipIntensity = 0.6f;   // 0..1 -> visible particle count
    glm::vec3 fogColor = glm::vec3(0.55f, 0.62f, 0.70f);
    float fogDensity = 0.0012f;     // exp2 density (0 = off)
    float cloudiness = 0.0f;        // 0..1 -> directional light/sky dimming
    float windAngle = 0.6f;         // radians; XZ direction (sin, 0, cos)
    float windStrength = 0.0f;      // m/s: particle drift + vegetation sway
    float snowCover = 0.0f;         // 0..1 extra snow caps on the terrain

    // Wind as an XZ vector (matches the yaw convention: (sin, 0, cos)).
    glm::vec2 windXZ() const {
        return glm::vec2(std::sin(windAngle), std::cos(windAngle)) * windStrength;
    }
    // Directional-light multiplier from cloud cover.
    float lightScale() const { return 1.0f - 0.55f * cloudiness; }
    // Sky exposure multiplier from cloud cover.
    float skyScale() const { return 1.0f - 0.45f * cloudiness; }
};

// Inline (GL-free) so unit tests can use them without linking GL code.
inline const char* weatherPresetName(int preset) {
    switch (preset) {
        case WeatherParams::Clear:    return "Clear";
        case WeatherParams::Overcast: return "Overcast";
        case WeatherParams::Rain:     return "Rain";
        case WeatherParams::Snow:     return "Snow";
        case WeatherParams::Fog:      return "Fog";
        case WeatherParams::Custom:   return "Custom";
        default: return "?";
    }
}

inline const char* precipName(int precip) {
    switch (precip) {
        case WeatherParams::PrecipNone: return "None";
        case WeatherParams::PrecipRain: return "Rain";
        case WeatherParams::PrecipSnow: return "Snow";
        default: return "?";
    }
}

// Parameter set for a named preset (preset field itself is set to `preset`).
inline WeatherParams weatherPreset(int preset) {
    WeatherParams w;
    w.preset = (WeatherParams::Preset)preset;
    switch (preset) {
        case WeatherParams::Overcast:
            w.fogDensity = 0.0022f;
            w.fogColor = glm::vec3(0.50f, 0.55f, 0.62f);
            w.cloudiness = 0.7f;
            w.windStrength = 2.0f;
            break;
        case WeatherParams::Rain:
            w.fogDensity = 0.0035f;
            w.fogColor = glm::vec3(0.45f, 0.50f, 0.58f);
            w.cloudiness = 0.85f;
            w.precip = WeatherParams::PrecipRain;
            w.precipIntensity = 0.7f;
            w.windStrength = 3.0f;
            break;
        case WeatherParams::Snow:
            w.fogDensity = 0.0028f;
            w.fogColor = glm::vec3(0.75f, 0.78f, 0.85f);
            w.cloudiness = 0.55f;
            w.precip = WeatherParams::PrecipSnow;
            w.precipIntensity = 0.7f;
            w.windStrength = 1.0f;
            w.snowCover = 0.8f;
            break;
        case WeatherParams::Fog:
            w.fogDensity = 0.011f;
            w.fogColor = glm::vec3(0.60f, 0.63f, 0.68f);
            w.cloudiness = 0.9f;
            w.windStrength = 0.5f;
            break;
        default:   // Clear (and Custom starts from Clear values)
            break;
    }
    return w;
}
