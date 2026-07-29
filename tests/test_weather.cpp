#include <doctest/doctest.h>
#include "weather.h"

TEST_CASE("weather: every preset and precip type has a name") {
    for (int p = 0; p <= (int)WeatherParams::Custom; ++p)
        CHECK(weatherPresetName(p) != nullptr);
    for (int t = 0; t <= (int)WeatherParams::PrecipSnow; ++t)
        CHECK(precipName(t) != nullptr);
}

TEST_CASE("weather: presets carry their signature parameters") {
    WeatherParams clear = weatherPreset(WeatherParams::Clear);
    CHECK(clear.precip == WeatherParams::PrecipNone);
    CHECK(clear.cloudiness == 0.0f);
    CHECK(clear.snowCover == 0.0f);

    WeatherParams rain = weatherPreset(WeatherParams::Rain);
    CHECK(rain.precip == WeatherParams::PrecipRain);
    CHECK(rain.cloudiness > clear.cloudiness);
    CHECK(rain.fogDensity > clear.fogDensity);

    WeatherParams snow = weatherPreset(WeatherParams::Snow);
    CHECK(snow.precip == WeatherParams::PrecipSnow);
    CHECK(snow.snowCover > 0.0f);

    WeatherParams fog = weatherPreset(WeatherParams::Fog);
    CHECK(fog.fogDensity > rain.fogDensity);

    WeatherParams overcast = weatherPreset(WeatherParams::Overcast);
    CHECK(overcast.precip == WeatherParams::PrecipNone);
    CHECK(overcast.cloudiness > 0.0f);

    // The preset tag is preserved for the UI.
    CHECK(rain.preset == WeatherParams::Rain);
}

TEST_CASE("weather: derived values behave") {
    WeatherParams w;
    w.cloudiness = 0.0f;
    CHECK(w.lightScale() == doctest::Approx(1.0f));
    CHECK(w.skyScale() == doctest::Approx(1.0f));
    w.cloudiness = 1.0f;
    CHECK(w.lightScale() > 0.0f);
    CHECK(w.lightScale() < 1.0f);
    CHECK(w.skyScale() < 1.0f);

    w.windAngle = 0.0f;
    w.windStrength = 3.0f;
    glm::vec2 v = w.windXZ();
    CHECK(glm::length(v) == doctest::Approx(3.0f).epsilon(1e-4));
    CHECK(v.x == doctest::Approx(0.0f).epsilon(1e-6));
    CHECK(v.y == doctest::Approx(3.0f).epsilon(1e-4));
}
