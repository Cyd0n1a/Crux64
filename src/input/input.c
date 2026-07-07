#include "input.h"
#include <libdragon.h>

static input_state_t st;

void input_init(void) {
    st = (input_state_t){ .limb = LIMB_NONE };
}

/* Analog stick: N64 sticks report roughly -80..80; deadzone then
 * normalize so gameplay code sees a clean -1..1. */
static float stick_norm(int8_t raw) {
    const int dead = 8;
    if (raw > -dead && raw < dead) return 0.f;
    float v = (float)raw / 80.f;
    if (v > 1.f)  v = 1.f;
    if (v < -1.f) v = -1.f;
    return v;
}

void input_poll(void) {
    joypad_poll();

    st.pad_present    = joypad_is_connected(JOYPAD_PORT_1);
    st.rumble_present = joypad_get_rumble_supported(JOYPAD_PORT_1);

    joypad_inputs_t  in   = joypad_get_inputs(JOYPAD_PORT_1);
    joypad_buttons_t btn  = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);

    /* GDD 2.4 limb mapping. If several C buttons are held, the first in
     * this order wins; a limb "release" fires the frame no C is held. */
    limb_id_t prev = st.limb;
    if      (held.c_up)    st.limb = LIMB_ARM_R;
    else if (held.c_left)  st.limb = LIMB_ARM_L;
    else if (held.c_right) st.limb = LIMB_LEG_R;
    else if (held.c_down)  st.limb = LIMB_LEG_L;
    else                   st.limb = LIMB_NONE;
    st.limb_released = (prev != LIMB_NONE && st.limb == LIMB_NONE);

    st.stick_x = stick_norm(in.stick_x);
    st.stick_y = stick_norm(in.stick_y);

    st.piton     = btn.z;
    st.z_held    = held.z;
    st.rest      = btn.r;
    st.chalk     = btn.a;
    st.start_btn = btn.start;

    st.cam_x = (held.d_right ? 1 : 0) - (held.d_left ? 1 : 0);
    st.cam_y = (held.d_up    ? 1 : 0) - (held.d_down ? 1 : 0);
}

const input_state_t *input_state(void) {
    return &st;
}

const char *limb_name(limb_id_t limb) {
    switch (limb) {
    case LIMB_ARM_R: return "RIGHT ARM";
    case LIMB_ARM_L: return "LEFT ARM";
    case LIMB_LEG_R: return "RIGHT LEG";
    case LIMB_LEG_L: return "LEFT LEG";
    default:         return "NONE";
    }
}
