#pragma once
#include "net/connection.h"

EXTERN_C_BEGIN
#define LAUNCHER_BACKGROUND_SPRITE 18130
#include <stdbool.h>

typedef struct {
    bool show_first_confirmation;
    bool show_second_confirmation;
    char file_path[512];
    bool new_character;
} SwitchCharacterConfirmState;

typedef struct {
    bool display;
    char text[250];
} SimplePopoverState;

typedef struct {
    int current_class;
    int current_gender;
    int current_gamemode; /* 0 Normal, 1 Hard Mode, 2 Roguelike */
    int current_season;   /* 0 Legacy, 1 Current */
    char password[15];
    SwitchCharacterConfirmState switch_character_confirm_state;
    SimplePopoverState simple_popover_state;
    char connection_status_text[300];
    ConnectionStatus connection_status;
    bool is_connecting;
    bool show_clear_map_confirmation;
} LauncherState;

extern LauncherState launcher_state;

void launcher_init(void);
void launcher_shutdown(void);
void launcher_tick(void);
void launcher_connect(void);
void launcher_set_fyi(const char *text);
const char *launcher_server_label(void);
void launcher_clear_saved_map(void);
EXTERN_C_END