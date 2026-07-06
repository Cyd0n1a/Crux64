#include "rumble.h"
#include <libdragon.h>
#include <stdbool.h>

static float level;      /* current strength 0..1 */
static float fade;       /* strength lost per second */
static float pwm_acc;
static bool  motor_on;

void rumble_init(void) {
    level = fade = pwm_acc = 0.f;
    motor_on = false;
}

void rumble_kick(float strength, float duration) {
    if (strength <= level) return;
    if (strength > 1.f) strength = 1.f;
    if (duration < 0.05f) duration = 0.05f;
    level = strength;
    fade  = strength / duration;
}

void rumble_update(float dt) {
    level -= fade * dt;
    if (level < 0.f) level = 0.f;

    /* Bresenham PWM: at level 1 the motor stays on; at 0.5 it toggles
     * every step. The pak's mass smooths this into felt intensity. */
    bool want;
    if (level <= 0.01f) {
        want = false;
        pwm_acc = 0.f;
    } else {
        pwm_acc += level;
        want = pwm_acc >= 1.f;
        if (want) pwm_acc -= 1.f;
    }

    if (want != motor_on) {
        motor_on = want;
        if (joypad_get_rumble_supported(JOYPAD_PORT_1))
            joypad_set_rumble_active(JOYPAD_PORT_1, motor_on);
    }
}
