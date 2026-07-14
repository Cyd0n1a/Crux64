#include "weather.h"
#include "../gen/noise.h"
#include <math.h>

static weather_state_t g_weather;
static float g_weather_time;

void weather_init(void) {
    /* Start at dawn */
    g_weather.time_of_day = 6.0f;
    g_weather.rain_intensity = 0.0f;
    g_weather.snow_intensity = 0.0f;
    g_weather.fog_density = 0.0f;
    g_weather.wind_strength = 0.0f;
    g_weather_time = 0.0f;
}

void weather_update(float dt) {
    /* 10 minutes real time = 24 hours game time.
     * 1 hour = 600 / 24 = 25 seconds.
     * 1 real second = 24 / 600 = 0.04 game hours. */
    float hours_per_sec = 24.0f / 600.0f;
    g_weather.time_of_day += dt * hours_per_sec;
    while (g_weather.time_of_day >= 24.0f) {
        g_weather.time_of_day -= 24.0f;
    }

    g_weather_time += dt;

    /* Use slow-moving 1D noise for weather events */
    float w_noise = noise_simplex2(g_weather_time * 0.01f, 0.0f); /* -1.0 to 1.0 */
    float temp_noise = noise_simplex2(g_weather_time * 0.005f, 100.0f); 
    
    /* Wind is always fluctuating slightly */
    g_weather.wind_strength = 0.2f + 0.8f * (0.5f * (noise_simplex2(g_weather_time * 0.05f, 50.0f) + 1.0f));

    if (w_noise > 0.4f) {
        /* Precipitation */
        float intensity = (w_noise - 0.4f) / 0.6f;
        if (temp_noise < 0.0f) {
            /* Snow */
            g_weather.snow_intensity += (intensity - g_weather.snow_intensity) * dt * 0.5f;
            g_weather.rain_intensity += (0.0f - g_weather.rain_intensity) * dt * 0.5f;
            g_weather.fog_density += (intensity * 0.8f - g_weather.fog_density) * dt * 0.2f;
        } else {
            /* Rain */
            g_weather.rain_intensity += (intensity - g_weather.rain_intensity) * dt * 0.5f;
            g_weather.snow_intensity += (0.0f - g_weather.snow_intensity) * dt * 0.5f;
            g_weather.fog_density += (intensity * 0.5f - g_weather.fog_density) * dt * 0.2f;
        }
    } else {
        /* Clear */
        g_weather.rain_intensity += (0.0f - g_weather.rain_intensity) * dt * 0.5f;
        g_weather.snow_intensity += (0.0f - g_weather.snow_intensity) * dt * 0.5f;
        
        /* Fog might still exist early morning or randomly */
        float target_fog = 0.0f;
        if (g_weather.time_of_day > 4.0f && g_weather.time_of_day < 8.0f) {
            target_fog = 0.3f; /* Morning mist */
        }
        g_weather.fog_density += (target_fog - g_weather.fog_density) * dt * 0.1f;
    }
}

const weather_state_t* weather_current(void) {
    return &g_weather;
}
