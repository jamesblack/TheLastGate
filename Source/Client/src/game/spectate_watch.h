#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { SPECTATE_NONE, SPECTATE_STARTING, SPECTATE_WATCHING } SpectatePhase;

typedef struct {
    SpectatePhase phase;
    int own_character;
} SpectateWatch;

void spectate_watch_reset(SpectateWatch *watch);

void spectate_watch_log_line(SpectateWatch *watch, const char *line, int centre_character);

void spectate_watch_view_centre(SpectateWatch *watch, int centre_character);

int spectate_watch_active(const SpectateWatch *watch);

#ifdef __cplusplus
}
#endif
