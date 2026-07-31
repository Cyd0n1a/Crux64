#include "menu_nav.h"

void menu_nav_reset(menu_nav_t *n, int count) {
    n->cursor   = 0;
    n->count    = count;
    n->repeat_t = 0.f;
    n->last_dir = 0;
    n->lock     = true;
}

bool menu_nav_take_lock(menu_nav_t *n) {
    if (!n->lock) return false;
    n->lock = false;
    return true;
}

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
