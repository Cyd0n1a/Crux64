/* Host unit tests for the pause menu's cursor and repeat-delay logic.
 * No libdragon and no N64 toolchain — run with tests/run_tests.sh. */
#include "../src/meta/menu_nav.h"

#include <stdio.h>

static int failures;

#define CHECK(cond) do {                                         \
    if (!(cond)) {                                               \
        printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        failures++;                                              \
    }                                                            \
} while (0)

#define FRAME (1.f / 60.f)

static void test_reset_starts_at_top(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 2);
    CHECK(n.cursor == 0);
    CHECK(n.count == 2);
}

static void test_lock_fires_exactly_once(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 2);
    CHECK(menu_nav_take_lock(&n) == true);
    CHECK(menu_nav_take_lock(&n) == false);
    CHECK(menu_nav_take_lock(&n) == false);
}

static void test_fresh_press_moves_immediately(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 3);
    CHECK(menu_nav_step(&n, 1, FRAME) == true);
    CHECK(n.cursor == 1);
}

static void test_hold_does_not_repeat_before_delay(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 3);
    menu_nav_step(&n, 1, FRAME);          /* the immediate move */
    CHECK(n.cursor == 1);

    float held = 0.f;
    while (held < MENU_NAV_DELAY - 0.03f) {
        menu_nav_step(&n, 1, FRAME);
        held += FRAME;
    }
    CHECK(n.cursor == 1);                 /* still parked */
}

static void test_hold_repeats_without_machine_gunning(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 64);               /* large, so nothing wraps */
    int moves = 0;
    for (int i = 0; i < 60; i++)          /* one second at 60 fps */
        if (menu_nav_step(&n, 1, FRAME)) moves++;

    /* One immediate move, then ~(1.0 - 0.30) / 0.12 repeats. Asserted as a
     * range, not an exact count: the frame grid never lands squarely on the
     * timer. Catches both "never repeats" (1) and "moves every frame" (60). */
    CHECK(moves >= 5);
    CHECK(moves <= 8);
    CHECK(n.cursor == moves);
}

static void test_release_then_press_moves_immediately(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 4);
    menu_nav_step(&n, 1, FRAME);          /* cursor 1 */
    menu_nav_step(&n, 0, FRAME);          /* released */
    CHECK(menu_nav_step(&n, 1, FRAME) == true);
    CHECK(n.cursor == 2);
}

static void test_wraps_at_both_ends(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 2);
    menu_nav_step(&n, -1, FRAME);         /* up from the top row */
    CHECK(n.cursor == 1);
    menu_nav_step(&n, 0, FRAME);
    menu_nav_step(&n, 1, FRAME);          /* down from the bottom row */
    CHECK(n.cursor == 0);
}

static void test_empty_screen_is_safe(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 0);
    CHECK(menu_nav_step(&n, 1, FRAME) == false);
    CHECK(n.cursor == 0);
}

static void test_h_first_press_moves_immediately(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 4);
    CHECK(menu_nav_step_h(&n, +1, FRAME) == +1);
    CHECK(menu_nav_step_h(&n, -1, FRAME) == -1);
}

static void test_h_held_waits_the_delay_then_repeats(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 4);
    CHECK(menu_nav_step_h(&n, +1, FRAME) == +1);   /* the initial step */

    /* Nothing more until MENU_NAV_DELAY has elapsed. */
    float t = 0.f;
    while (t < MENU_NAV_DELAY - 0.02f) {
        CHECK(menu_nav_step_h(&n, +1, FRAME) == 0);
        t += FRAME;
    }

    /* Then it fires, and again every MENU_NAV_REPEAT. */
    int fired = 0;
    for (int i = 0; i < 40; i++)
        if (menu_nav_step_h(&n, +1, FRAME)) fired++;
    CHECK(fired >= 3);
}

static void test_h_release_rearms_the_immediate_step(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 4);
    CHECK(menu_nav_step_h(&n, +1, FRAME) == +1);
    CHECK(menu_nav_step_h(&n, +1, FRAME) == 0);
    CHECK(menu_nav_step_h(&n,  0, FRAME) == 0);    /* released */
    CHECK(menu_nav_step_h(&n, +1, FRAME) == +1);   /* immediate again */
}

/* The axes must not share timer state: adjusting a value while stepping
 * rows would otherwise swallow one of the two. */
static void test_axes_are_independent(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 4);
    CHECK(menu_nav_step(&n, +1, FRAME) == true);
    CHECK(menu_nav_step_h(&n, +1, FRAME) == +1);
    CHECK(n.cursor == 1);

    /* Holding vertical must not re-arm horizontal. */
    CHECK(menu_nav_step(&n, +1, FRAME) == false);
    CHECK(menu_nav_step_h(&n, +1, FRAME) == 0);
}

static void test_reset_clears_the_horizontal_axis(void) {
    menu_nav_t n;
    menu_nav_reset(&n, 4);
    CHECK(menu_nav_step_h(&n, +1, FRAME) == +1);
    menu_nav_reset(&n, 4);
    CHECK(n.h_last_dir == 0);
    CHECK(menu_nav_step_h(&n, +1, FRAME) == +1);   /* immediate after reset */
}

int main(void) {
    test_reset_starts_at_top();
    test_lock_fires_exactly_once();
    test_fresh_press_moves_immediately();
    test_hold_does_not_repeat_before_delay();
    test_hold_repeats_without_machine_gunning();
    test_release_then_press_moves_immediately();
    test_wraps_at_both_ends();
    test_empty_screen_is_safe();
    test_h_first_press_moves_immediately();
    test_h_held_waits_the_delay_then_repeats();
    test_h_release_rearms_the_immediate_step();
    test_axes_are_independent();
    test_reset_clears_the_horizontal_axis();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all menu_nav checks passed\n");
    return 0;
}
