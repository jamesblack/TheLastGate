#pragma once

#include "common.h"
#include "mods/buff_list.h"

EXTERN_C_BEGIN

/*
 * Keeps the power of the player's active Bless, Enhance and Protect.
 *
 * The client only sees buff icons. Whenever one of those three appears, goes,
 * is refreshed or recast, a "#buffs" command is sent in the background and
 * its reply is captured out of the chat stream and parsed. A reply nobody
 * asked for (the player typed the command) is left for the chat box.
 */

/* Forget every reading and in-flight request (login). */
void buff_watch_reset(void);

/* The server sent buff slot `slot`; old values are what the slot held before. */
void buff_watch_spell_packet(int slot, int old_sprite, int old_active, int new_sprite, int new_active);

/* Offer a finished chat line; returns 1 if it was captured and must not be logged. */
int buff_watch_capture_line(const char *text, int font);

void buff_watch_end_of_tick(void);

int buff_watch_is_active(BuffKind kind);

/* The buff's reading, or NULL unless it is active and was read since the last change. */
const BuffReading *buff_watch_reading(BuffKind kind);

/* 1 while a buff change hasn't been answered yet. */
int buff_watch_waiting(void);

EXTERN_C_END
