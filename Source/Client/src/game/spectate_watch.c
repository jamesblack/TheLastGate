#include "spectate_watch.h"

#include <string.h>

static const char START_PREFIX[] = "Now spectating ";
static const char STOP_PREFIX[] = "No longer spectating";
static const char LEFT_GAME_SUFFIX[] = " left the game";

static int starts_with(const char *line, const char *prefix) {
    return strncmp(line, prefix, strlen(prefix)) == 0;
}

static void copy_target_name(SpectateWatch *watch, const char *name_start) {
    size_t length = 0;

    while (name_start[length] != '\0' && name_start[length] != '.' && name_start[length] != ' ' &&
           length < sizeof(watch->target_name) - 1) {
        watch->target_name[length] = name_start[length];
        length++;
    }
    watch->target_name[length] = '\0';
}

static int target_left_game(const SpectateWatch *watch, const char *line) {
    const size_t name_length = strlen(watch->target_name);

    return name_length > 0 && strncmp(line, watch->target_name, name_length) == 0 &&
           starts_with(line + name_length, LEFT_GAME_SUFFIX);
}

void spectate_watch_reset(SpectateWatch *watch) {
    watch->phase = SPECTATE_NONE;
    watch->own_character = 0;
    watch->target_name[0] = '\0';
}

void spectate_watch_log_line(SpectateWatch *watch, const char *line, const int centre_character) {
    if (starts_with(line, START_PREFIX)) {
        // Switching straight to another target keeps the character recorded when spectating began
        if (watch->phase == SPECTATE_NONE) watch->own_character = centre_character;
        watch->phase = SPECTATE_STARTING;
        copy_target_name(watch, line + strlen(START_PREFIX));
    } else if (starts_with(line, STOP_PREFIX) || (watch->phase != SPECTATE_NONE && target_left_game(watch, line))) {
        spectate_watch_reset(watch);
    }
}

/*
 * Players hidden from the who list log out without an announcement, and the view simply returns to our own
 * character. The view has to move away from our character first, since the start message can arrive a frame before
 * the new view does.
 */
void spectate_watch_view_centre(SpectateWatch *watch, const int centre_character) {
    if (watch->phase == SPECTATE_NONE || centre_character == 0 || watch->own_character == 0) return;
    if (centre_character != watch->own_character) {
        watch->phase = SPECTATE_WATCHING;
    } else if (watch->phase == SPECTATE_WATCHING) {
        spectate_watch_reset(watch);
    }
}

int spectate_watch_active(const SpectateWatch *watch) {
    return watch->phase != SPECTATE_NONE;
}
