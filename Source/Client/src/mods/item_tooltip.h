#pragma once

#include "common.h"
#include "mods/item_details.h"

EXTERN_C_BEGIN

/*
 * Item tooltips for inventory and equipped gear.
 *
 * The server only describes items through chat: a look request produces a
 * stream of log lines that starts with a font-6 "<name>:" header. Each slot
 * is fetched in the background with a look request followed by an identical
 * sentinel request. Both replies are captured out of the chat stream, and
 * the longest common prefix of the two becomes the tooltip. Anything that
 * does not match (messages from the rest of the tick) is released back to
 * the chat box.
 */

typedef enum {
    ITEM_TOOLTIP_INVENTORY = 0,
    ITEM_TOOLTIP_WORN = 1,
} ItemTooltipKind;

/* Forget every cached description and in-flight request (login). */
void item_tooltip_reset(void);

/* The server sent a new state for a slot; sprite 0 means the slot is empty. */
void item_tooltip_slot_changed(ItemTooltipKind kind, int slot, int sprite);

/* Send a player-initiated look whose reply must reach the chat box. */
void item_tooltip_manual_look(ItemTooltipKind kind, int slot);

/* Offer a finished chat line; returns 1 if it was captured and must not be logged. */
int item_tooltip_capture_line(const char *text, int font);

/* Called after the server data of one tick has been processed. */
void item_tooltip_end_of_tick(void);

/* Report the slot under the mouse for this frame. */
void item_tooltip_hover(ItemTooltipKind kind, int slot);

/* The hovered slot's captured look text, or NULL when no tooltip should show. */
const TooltipBlock *item_tooltip_hovered_block(void);

/* The hovered slot's parsed details, or NULL when no tooltip should show. */
const ItemDetails *item_tooltip_hovered_details(void);

/* The server sent a new cursor item; sprite 0 means the cursor is empty. */
void item_tooltip_cursor_changed(int sprite);

/* The held item's parsed details, carried over from the slot it came from; NULL if unknown. */
const ItemDetails *item_tooltip_cursor_details(void);

/* The hovered slot once the hover delay has passed, even if it is empty; returns 0 otherwise. */
int item_tooltip_hovered_slot(ItemTooltipKind *kind, int *slot);

/* Clear the hover state; call once per frame after rendering. */
void item_tooltip_frame_end(void);

EXTERN_C_END
