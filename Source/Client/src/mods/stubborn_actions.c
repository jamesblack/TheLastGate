//
// Created by james on 12/13/2025.
//

#include "stubborn_actions.h"

#include <stdbool.h>

#include "inter.h"
#include "main.h"
#include "use_queue.h"
#include "config/config.h"
#include "util/math_util.h"

#define MISC_STATUS_FIRST 160
#define MISC_STATUS_LAST 191
#define MISC_FRAMES_PER_DIRECTION 8
#define MAX_ATTEMPTS_WITHOUT_PROGRESS 10

enum MiscAnimation {
    MISC_ANIMATION_NONE = -1,
    MISC_ANIMATION_PICKUP = 1,
    MISC_ANIMATION_DROP = 2,
    MISC_ANIMATION_GIVE = 3,
    MISC_ANIMATION_USE = 4,
};

static StubbornActionState state = {0};
static int previous_player_status = 0;

static int center() {
    return (screen_renderdist / 2) + (screen_renderdist / 2) * screen_renderdist;
}

static bool is_misc_status(const int status) {
    return status >= MISC_STATUS_FIRST && status <= MISC_STATUS_LAST;
}

static int misc_direction(const int status) {
    return (status - MISC_STATUS_FIRST) / MISC_FRAMES_PER_DIRECTION;
}

static int misc_frame(const int status) {
    return (status - MISC_STATUS_FIRST) % MISC_FRAMES_PER_DIRECTION;
}

// Back-to-back actions can skip the idle state, so a rewound frame or a new direction also counts as a fresh start.
static bool misc_animation_started(const int status, const int previous_status) {
    if (!is_misc_status(status)) return false;
    if (!is_misc_status(previous_status)) return true;
    if (misc_direction(status) != misc_direction(previous_status)) return true;
    return misc_frame(status) < misc_frame(previous_status);
}

static bool is_facing_target(const int status) {
    int facing_x = map[center()].x;
    int facing_y = map[center()].y;
    switch (misc_direction(status)) {
        case 0: facing_y--; break;
        case 1: facing_y++; break;
        case 2: facing_x--; break;
        case 3: facing_x++; break;
        default: return false;
    }
    return facing_x == state.target_x && facing_y == state.target_y;
}

static int expected_animation(const int action) {
    switch (action) {
        case CL_CMD_USE: return MISC_ANIMATION_USE;
        case CL_CMD_PICKUP: return MISC_ANIMATION_PICKUP;
        case CL_CMD_DROP: return MISC_ANIMATION_DROP;
        case CL_CMD_GIVE: return MISC_ANIMATION_GIVE;
        default: return MISC_ANIMATION_NONE;
    }
}

static bool has_pending_action_completed() {
    const int status = map[center()].ch_status;
    const bool started = misc_animation_started(status, previous_player_status);
    previous_player_status = status;

    if (!started) return false;
    if (map[center()].ch_stat_off != expected_animation(state.pending_action)) return false;
    // Give targets a character, which may have stepped off the tile it was on when clicked.
    if (state.pending_action == CL_CMD_GIVE) return true;
    return is_facing_target(status);
}

static void complete_pending_action() {
    const bool was_use = state.pending_action == CL_CMD_USE;
    state.pending_action = 0;
    state.indicator = false;
    if (was_use && pl.misc_action == DR_IDLE) {
        pop_cmd_from_queue();
    }
}

static bool is_server_working_on_pending_action() {
    switch (state.pending_action) {
        case CL_CMD_USE:
            return pl.misc_action == DR_USE && pl.misc_target1 == state.target_x && pl.misc_target2 == state.target_y;
        case CL_CMD_DROP: return pl.misc_action == DR_DROP;
        case CL_CMD_PICKUP: return pl.misc_action == DR_PICKUP;
        case CL_CMD_GIVE: return pl.misc_action == DR_GIVE;
        case CL_CMD_MOVE: return pl.goto_x > 0 && pl.goto_y > 0;
        default: return false;
    }
}

static Uint32 get_next_attempt_time() {
    return max(1000, min(5000, 1000 * state.number_of_attempts));
}

static bool is_stubborn_action_enabled(int action) {
    switch (action) {
        case CL_CMD_USE: return g_config.gameplay.stubborn_use;
        case CL_CMD_DROP: return g_config.gameplay.stubborn_drop;
        case CL_CMD_PICKUP: return g_config.gameplay.stubborn_pickup;
        case CL_CMD_MOVE: return g_config.gameplay.stubborn_move;
        case CL_CMD_GIVE: return g_config.gameplay.stubborn_give;
        default: return false;
    }
}

static void start_pending_action(const int action, const int x, const int y) {
    state.pending_action = action;
    state.target_x = x;
    state.target_y = y;
    state.last_x = map[center()].x;
    state.last_y = map[center()].y;
    state.number_of_attempts = 0;
    state.next_attempt = get_next_attempt_time();
    state.indicator = false;
    state.last_combat_target = 0;
}

void mod_stubborn_actions_set_use_cmd(int x, int y) {
    if (!g_config.gameplay.stubborn_use) return;
    start_pending_action(CL_CMD_USE, x, y);
}

void mod_stubborn_actions_on_cmd(const int action, const int tile_id) {
    if (!is_stubborn_action_enabled(action)) return;
    start_pending_action(action, map[tile_id].x, map[tile_id].y);
    state.target_item_sprite = map[tile_id].it_sprite;
    state.target_character_id = map[tile_id].ch_nr;
}

bool mod_stubborn_actions_pending_use() {
    return state.pending_action == CL_CMD_USE;
}

void mod_stubborn_actions_clear() {
    state.indicator = false;
    state.pending_action = 0;
}

static void retry_pending_action() {
    const bool player_moved = map[center()].x != state.last_x || map[center()].y != state.last_y;
    state.last_x = map[center()].x;
    state.last_y = map[center()].y;
    state.number_of_attempts = player_moved ? 0 : state.number_of_attempts + 1;

    if (state.number_of_attempts > MAX_ATTEMPTS_WITHOUT_PROGRESS) {
        xlog(0, "Gave up trying to reach your target.");
        mod_stubborn_actions_clear();
        return;
    }

    cmdq(state.pending_action, state.target_x, state.target_y);
    state.next_attempt = get_next_attempt_time();
    state.indicator = true;
}

void mod_stubborn_actions_on_tick(const Uint32 delta_time) {
    const bool completed = has_pending_action_completed();
    if (!state.pending_action) return;
    if (completed) {
        complete_pending_action();
        return;
    }

    if (state.next_attempt > delta_time) {
        state.next_attempt -= delta_time;
        return;
    }

    if (pl.attack_cn > 0) {
        if (pl.attack_cn != state.last_combat_target) {
            state.last_combat_target = pl.attack_cn;
            state.number_of_attempts = 0;
        }
        state.next_attempt = get_next_attempt_time();
        return;
    }

    if (is_server_working_on_pending_action()) return;

    if (state.pending_action == CL_CMD_MOVE && map[center()].x == state.target_x && map[center()].y == state.target_y) {
        mod_stubborn_actions_clear();
        return;
    }

    retry_pending_action();
}

bool mod_stubborn_actions_is_cmd_pending(int cmd, int x, int y) {
    return is_stubborn_action_enabled(cmd) && state.indicator && cmd == state.pending_action && x == state.target_x && y == state.target_y;
}
