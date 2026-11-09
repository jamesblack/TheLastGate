#pragma once

#include "common.h"
#include "game/attributes.h"
#include "game/stat_raise.h"
EXTERN_C_BEGIN

extern int noshop;
extern int pskip, pidle;

extern int st_learned_skill(int st_val, int v);

extern int stat_points_used;
extern struct cplayer pl;
extern struct skilltab *skilltab;
extern struct skilltab *raw_skilltab;
extern int skill_needed(int n, int v);
extern int attrib_needed(int n, int v);
extern int stat_raised[STAT_RAISE_SLOT_COUNT];
extern void cmd_options(void);
extern int points2rank(int v);
extern int rank2points(int v);
extern int points_tolevel(int curr_exp);
void update_skill_tab_from_raw(void);
void xlog(char font, char *format, ...);
int at_score(int n);
int sk_score(int n);
void engine_player_tile(int *tile_x, int *tile_y);
int points2rank(int v);

#ifndef GUI_SHOP_X
#define GUI_SHOP_X ((1280 / 2) - (320 / 2))
#define GUI_SHOP_Y ((736 / 2) - (320 / 2) + 72)
#endif

EXTERN_C_END