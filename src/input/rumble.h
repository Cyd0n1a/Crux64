#pragma once

/* Rumble Pak driver (GDD 2.2): the pak motor is binary, so perceived
 * intensity comes from PWM-toggling it. Kicks decay linearly, letting
 * fatigue read as a rising pulse and grip failure as a sustained buzz. */

void rumble_init(void);
void rumble_kick(float strength, float duration);
void rumble_update(float dt);
