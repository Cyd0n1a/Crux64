#pragma once

#include <stdbool.h>
#include <stdint.h>

/* GDD 2.4: each C button selects one limb while held; the analog stick
 * then moves that limb. LIMB_NONE = no C button held (stick idle). */
typedef enum {
    LIMB_NONE = -1,
    LIMB_ARM_R,   /* C-Up    */
    LIMB_ARM_L,   /* C-Left  */
    LIMB_LEG_R,   /* C-Right */
    LIMB_LEG_L,   /* C-Down  */
    LIMB_COUNT,
} limb_id_t;

typedef struct {
    bool  pad_present;     /* controller in port 1 */
    bool  rumble_present;  /* Rumble Pak inserted (GDD 1.3: mandatory) */

    limb_id_t limb;        /* limb selected this frame (C button held) */
    bool  limb_released;   /* C button let go this frame -> snap to grip */

    float stick_x;         /* -1..1, deadzoned */
    float stick_y;

    bool  piton;           /* Z pressed this frame */
    bool  rest;            /* R pressed this frame */
    bool  chalk;           /* A pressed this frame */

    int8_t cam_x;          /* D-pad free-look: -1/0/1 per axis */
    int8_t cam_y;
} input_state_t;

void input_init(void);
void input_poll(void);
const input_state_t *input_state(void);
const char *limb_name(limb_id_t limb);
