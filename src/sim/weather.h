#pragma once

#include <stdbool.h>

typedef struct {
    float time_of_day;    /* 0.0 to 24.0 hours */
    float rain_intensity; /* 0.0 to 1.0 */
    float snow_intensity; /* 0.0 to 1.0 */
    float fog_density;    /* 0.0 to 1.0 */
    float wind_strength;  /* 0.0 to 1.0 */
} weather_state_t;

void weather_init(void);
void weather_update(float dt);
const weather_state_t* weather_current(void);
