#include "spectate_watch.h"

#include <string.h>

static const char START_PREFIX[] = "Now spectating ";
static const char STOP_PREFIX[] = "No longer spectating";

static int starts_with(const char *line, const char *prefix) {
    return strncmp(line, prefix, strlen(prefix)) == 0;
}

void spectate_watch_reset(SpectateWatch *watch) {
    watch->phase = SPECTATE_NONE;
    watch->own_character = 0;
}

void spectate_watch_log_line(SpectateWatch *watch, const char *line, const int centre_character) {
    if (starts_with(line, START_PREFIX)) {
        // Switching straight to another target keeps the character recorded when spectating began
        if (watch->phase == SPECTATE_NONE) watch->own_character = centre_character;
        watch->phase = SPECTATE_STARTING;
    } else if (starts_with(line, STOP_PREFIX)) {
        spectate_watch_reset(watch);
    }
}

/*
 * The server sends nothing when the watched player logs out; the view simply returns to our own character. The view
 * has to move away from our character first, since the start message can arrive a frame before the new view does.
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
