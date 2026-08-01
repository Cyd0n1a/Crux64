#include "menu_nav.h"

/**
 * Initializes menu navigation state for the specified number of items.
 * @param n Navigation state to initialize.
 * @param count Number of items available for navigation.
 */
void menu_nav_reset(menu_nav_t *n, int count) {
    n->cursor   = 0;
    n->count    = count;
    n->repeat_t = 0.f;
    n->last_dir = 0;
    n->lock     = true;
    n->h_repeat_t = 0.f;
    n->h_last_dir = 0;
}

/**
 * Acquires the navigation lock when it is available.
 *
 * @param n Navigation state whose lock is acquired.
 * @return `true` if the lock was acquired, `false` if it was already inactive.
 */
bool menu_nav_take_lock(menu_nav_t *n) {
    if (!n->lock) return false;
    n->lock = false;
    return true;
}

/**
 * Advances vertical menu navigation with directional key-repeat behavior.
 *
 * @param n Navigation state to update.
 * @param dir Vertical movement direction; zero releases the direction.
 * @param dt Elapsed time since the previous update.
 * @return `true` if the cursor moved, `false` otherwise.
 */
bool menu_nav_step(menu_nav_t *n, int dir, float dt) {
    if (n->count <= 0) {          /* nothing to point at */
        n->cursor = 0;
        return false;
    }
    if (n->cursor < 0)         n->cursor = 0;
    if (n->cursor >= n->count) n->cursor = n->count - 1;

    if (dir == 0) {               /* released: re-arm the immediate move */
        n->last_dir = 0;
        n->repeat_t = 0.f;
        return false;
    }

    bool move;
    if (dir != n->last_dir) {
        n->last_dir = dir;
        n->repeat_t = MENU_NAV_DELAY;
        move = true;
    } else {
        n->repeat_t -= dt;
        move = (n->repeat_t <= 0.f);
        if (move) n->repeat_t += MENU_NAV_REPEAT;
    }

    if (move) {
        n->cursor += dir;
        if (n->cursor < 0)         n->cursor = n->count - 1;
        if (n->cursor >= n->count) n->cursor = 0;
    }
    return move;
}

/**
 * Processes horizontal navigation with an initial delay and repeated movement while a direction is held.
 * @param n Navigation state to update.
 * @param dir Horizontal navigation direction, or zero when released.
 * @param dt Elapsed time since the previous update.
 * @returns The navigation direction when movement occurs, or zero otherwise.
 */
int menu_nav_step_h(menu_nav_t *n, int dir, float dt) {
    if (dir == 0) {               /* released: re-arm the immediate move */
        n->h_last_dir = 0;
        n->h_repeat_t = 0.f;
        return 0;
    }

    bool move;
    if (dir != n->h_last_dir) {
        n->h_last_dir = dir;
        n->h_repeat_t = MENU_NAV_DELAY;
        move = true;
    } else {
        n->h_repeat_t -= dt;
        move = (n->h_repeat_t <= 0.f);
        if (move) n->h_repeat_t += MENU_NAV_REPEAT;
    }

    return move ? dir : 0;
}
