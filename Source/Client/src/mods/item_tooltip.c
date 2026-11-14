#include "item_tooltip.h"

#include <stdint.h>
#include <string.h>
#include <SDL2/SDL_timer.h>

#include "common.h"
#include "inter.h"
#include "config/config.h"

#define TT_FIFO_SIZE 16
#define TT_HEADERLESS_MAX 64

#define TT_DEBOUNCE_TICKS 4      /* let a burst of slot updates settle before fetching */
#define TT_REFETCH_TICKS 40      /* minimum gap between fetches of the same slot */
#define TT_TIMEOUT_TICKS 30      /* give up on a reply after this long */
#define TT_MANUAL_PAUSE_TICKS 20 /* keep background fetches out of the way of a player look */
#define TT_MAX_FAILURES 2
#define TT_HOVER_DELAY_MS 300

#define TT_WORN_SLOTS 20


#define INV_LOOK_EQUIPMENT 7
#define INV_LOOK_ITEM 8

#define LOOK_PACKET_SIZE 16
#define LOOK_ACTION_OFFSET 1
#define LOOK_SLOT_OFFSET 5

typedef enum {
    SLOT_EMPTY = 0,
    SLOT_DIRTY,
    SLOT_READY,
    SLOT_FAILED,
} SlotState;

typedef struct {
    SlotState state;
    int sprite;
    int has_text; /* block describes `sprite`, possibly stale while a refetch is pending */
    int dirty_tick;
    int last_fetch_tick;
    int failures;
    TooltipBlock block;
    ItemDetails details;
} SlotCache;

typedef enum {
    REQ_FETCH,
    REQ_SENTINEL,
    REQ_MANUAL,
} RequestRole;

typedef struct {
    RequestRole role;
    int sent_tick;
} Request;

typedef enum {
    CAPTURE_NONE,
    CAPTURE_FETCH,
    CAPTURE_SENTINEL,
    CAPTURE_MANUAL,
} CaptureTarget;

static SlotCache inventory_cache[MAXITEMS];
static SlotCache worn_cache[TT_WORN_SLOTS];

/* Requests whose reply has not started yet, in the order they were sent. */
static Request fifo[TT_FIFO_SIZE];
static int fifo_head = 0;
static int fifo_count = 0;

/* The single fetch/sentinel pair allowed in flight. */
static struct {
    int active;
    ItemTooltipKind kind;
    int slot;
    int stale;
    int sent_tick;
    int fetch_done;
    int sentinel_done;
    TooltipBlock fetch;
    TooltipBlock sentinel;
} flight;

static CaptureTarget capture = CAPTURE_NONE;

/* Sprites whose look reply carries no header (such as shrines); never fetched. */
static int headerless[TT_HEADERLESS_MAX];
static int headerless_count = 0;

/*
 * The item held on the cursor cannot be looked at, so descriptions are handed
 * between slots and the cursor. A pick-up, put-down or swap updates the slots
 * and then the cursor within one tick, so every slot update of the tick is
 * recorded and the cursor update pairs them up. Pairing by tick rather than by
 * a change of sprite keeps swaps of two look-alike items (same sprite,
 * different mods) from keeping the wrong descriptions.
 */
#define TT_HANDOFF_MAX 8 /* slot updates remembered per tick; a swap needs one or two */

static SlotCache cursor_cache;

/* What slots held before this tick's updates. */
static SlotCache vacated[TT_HANDOFF_MAX];
static int vacated_count = 0;

/* Slots that received an item this tick. */
static struct {
    ItemTooltipKind kind;
    int slot;
    int sprite;
} arrivals[TT_HANDOFF_MAX];

static int arrival_count = 0;

static int handoff_tick = -1;

static int tick_now = 0;
static int pause_until = 0;

static int hover_kind = -1;
static int hover_slot = -1;
static int last_hover_kind = -1;
static int last_hover_slot = -1;
static unsigned int hover_start_ms = 0;

static SlotCache *cache_for(const ItemTooltipKind kind, const int slot) {
    if (kind == ITEM_TOOLTIP_INVENTORY && slot >= 0 && slot < MAXITEMS) return &inventory_cache[slot];
    if (kind == ITEM_TOOLTIP_WORN && slot >= 0 && slot < TT_WORN_SLOTS) return &worn_cache[slot];
    return NULL;
}

static int is_headerless(const int sprite) {
    for (int entry = 0; entry < headerless_count; entry++) {
        if (headerless[entry] == sprite) return 1;
    }
    return 0;
}

static void remember_headerless(const int sprite) {
    if (!sprite || is_headerless(sprite) || headerless_count >= TT_HEADERLESS_MAX) return;
    headerless[headerless_count++] = sprite;
}

static void fifo_push(const RequestRole role) {
    if (fifo_count >= TT_FIFO_SIZE) return;
    fifo[(fifo_head + fifo_count) % TT_FIFO_SIZE] = (Request) {role, tick_now};
    fifo_count++;
}

static Request fifo_pop(void) {
    const Request request = fifo[fifo_head];
    fifo_head = (fifo_head + 1) % TT_FIFO_SIZE;
    fifo_count--;
    return request;
}

static int block_append(TooltipBlock *block, const char *text, const int font) {
    if (block->count >= TT_MAX_LINES) return 0;

    TooltipLine *line = &block->lines[block->count++];
    line->font = (char) font;
    strncpy(line->text, text, TT_LINE_LEN - 1);
    line->text[TT_LINE_LEN - 1] = 0;
    return 1;
}

static void block_release(TooltipBlock *block, const int from) {
    for (int line_index = from; line_index < block->count; line_index++) {
        tlog(block->lines[line_index].text, block->lines[line_index].font);
    }
    block->count = 0;
}

static int common_prefix(const TooltipBlock *first, const TooltipBlock *second) {
    int shared = 0;
    while (shared < first->count && shared < second->count && first->lines[shared].font == second->lines[shared].font &&
           strcmp(first->lines[shared].text, second->lines[shared].text) == 0) {
        shared++;
    }
    return shared;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void send_look(const ItemTooltipKind kind, const int slot) {
    unsigned char packet[LOOK_PACKET_SIZE] = {0};

    packet[0] = CL_CMD_INV;
    *(uint32_t *) (packet + LOOK_ACTION_OFFSET) = kind == ITEM_TOOLTIP_WORN ? INV_LOOK_EQUIPMENT : INV_LOOK_ITEM;
    *(uint32_t *) (packet + LOOK_SLOT_OFFSET) = (uint32_t) slot;
    xsend(packet);
}

static void slot_mark_failed(SlotCache *cache) {
    cache->failures++;
    if (cache->failures >= TT_MAX_FAILURES) {
        cache->state = SLOT_FAILED;
    } else {
        cache->state = SLOT_DIRTY;
        cache->dirty_tick = tick_now;
    }
}

static void resolve_flight(void) {
    SlotCache *cache = cache_for(flight.kind, flight.slot);
    const int prefix = common_prefix(&flight.fetch, &flight.sentinel);

    if (!flight.stale && cache) {
        if (prefix > 0) {
            cache->block.count = prefix;
            memcpy(cache->block.lines, flight.fetch.lines, sizeof(TooltipLine) * prefix);
            item_details_parse(&cache->block, &cache->details);
            cache->has_text = 1;
            cache->state = SLOT_READY;
            cache->failures = 0;
        } else {
            slot_mark_failed(cache);
        }
    }

    /* Whatever the two replies do not share came from elsewhere in the tick. */
    block_release(&flight.fetch, prefix);
    block_release(&flight.sentinel, prefix);
    flight.active = 0;
}

static void close_capture(void) {
    if (capture == CAPTURE_FETCH) {
        flight.fetch_done = 1;
    } else if (capture == CAPTURE_SENTINEL) {
        flight.sentinel_done = 1;
    }
    capture = CAPTURE_NONE;

    if (flight.active && flight.fetch_done && flight.sentinel_done) resolve_flight();
}

static void abandon_flight(void) {
    SlotCache *cache = cache_for(flight.kind, flight.slot);

    /* No header at all means the item never announces itself; stop asking. */
    if (cache && !flight.stale && flight.fetch.count == 0 && flight.sentinel.count == 0) {
        remember_headerless(cache->sprite);
        cache->state = SLOT_FAILED;
    } else if (cache && !flight.stale) {
        slot_mark_failed(cache);
    }

    block_release(&flight.fetch, 0);
    block_release(&flight.sentinel, 0);
    flight.active = 0;
    capture = CAPTURE_NONE;
    fifo_head = fifo_count = 0;
}

static int slot_ready_to_fetch(const SlotCache *cache) {
    return cache->state == SLOT_DIRTY && tick_now - cache->dirty_tick >= TT_DEBOUNCE_TICKS &&
           tick_now - cache->last_fetch_tick >= TT_REFETCH_TICKS;
}

static void start_flight(const ItemTooltipKind kind, const int slot) {
    SlotCache *cache = cache_for(kind, slot);

    flight.active = 1;
    flight.kind = kind;
    flight.slot = slot;
    flight.stale = 0;
    flight.sent_tick = tick_now;
    flight.fetch_done = flight.sentinel_done = 0;
    flight.fetch.count = flight.sentinel.count = 0;
    cache->last_fetch_tick = tick_now;

    send_look(kind, slot);
    fifo_push(REQ_FETCH);
    send_look(kind, slot);
    fifo_push(REQ_SENTINEL);
}

static void pump(void) {
    if (!g_config.ui.item_tooltips || flight.active || fifo_count > 0 || tick_now < pause_until) return;

    /* The server appends a comparison with the cursor item, which must not be cached. */
    if (pl.citem) return;

    /* The slot under the mouse goes first. */
    if (last_hover_kind >= 0) {
        const SlotCache *cache = cache_for((ItemTooltipKind) last_hover_kind, last_hover_slot);
        if (cache && slot_ready_to_fetch(cache)) {
            start_flight((ItemTooltipKind) last_hover_kind, last_hover_slot);
            return;
        }
    }

    for (int slot = 0; slot < TT_WORN_SLOTS; slot++) {
        if (slot_ready_to_fetch(&worn_cache[slot])) {
            start_flight(ITEM_TOOLTIP_WORN, slot);
            return;
        }
    }
    for (int slot = 0; slot < MAXITEMS; slot++) {
        if (slot_ready_to_fetch(&inventory_cache[slot])) {
            start_flight(ITEM_TOOLTIP_INVENTORY, slot);
            return;
        }
    }
}

void item_tooltip_reset(void) {
    memset(inventory_cache, 0, sizeof(inventory_cache));
    memset(worn_cache, 0, sizeof(worn_cache));
    memset(&flight, 0, sizeof(flight));
    fifo_head = fifo_count = 0;
    capture = CAPTURE_NONE;
    headerless_count = 0;
    memset(&cursor_cache, 0, sizeof(cursor_cache));
    vacated_count = arrival_count = 0;
    handoff_tick = -1;
    tick_now = TT_REFETCH_TICKS;
    pause_until = 0;
}

static void handoff_begin(void) {
    if (handoff_tick == tick_now) return;
    handoff_tick = tick_now;
    vacated_count = arrival_count = 0;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void item_tooltip_slot_changed(const ItemTooltipKind kind, const int slot, const int sprite) {
    SlotCache *cache = cache_for(kind, slot);
    if (!cache) return;

    if (flight.active && flight.kind == kind && flight.slot == slot) flight.stale = 1;

    /* The item that was here may be heading for the cursor, even if the new one looks the same. */
    handoff_begin();
    if (cache->has_text && vacated_count < TT_HANDOFF_MAX) vacated[vacated_count++] = *cache;
    if (sprite && arrival_count < TT_HANDOFF_MAX) {
        arrivals[arrival_count].kind = kind;
        arrivals[arrival_count].slot = slot;
        arrivals[arrival_count].sprite = sprite;
        arrival_count++;
    }

    if (!sprite) {
        cache->state = SLOT_EMPTY;
        cache->sprite = 0;
        cache->has_text = 0;
        cache->failures = 0;
        return;
    }

    if (sprite != cache->sprite) {
        cache->sprite = sprite;
        cache->has_text = 0;
        cache->failures = 0;
    }

    if (is_headerless(sprite)) {
        cache->state = SLOT_FAILED;
    } else if (cache->state != SLOT_DIRTY) {
        cache->state = SLOT_DIRTY;
        cache->dirty_tick = tick_now;
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void item_tooltip_manual_look(const ItemTooltipKind kind, const int slot) {
    /* Inventory looks keep CL_CMD_INV_LOOK: in build mode it selects the slot for area building. */
    if (kind == ITEM_TOOLTIP_WORN) {
        cmd3(CL_CMD_INV, INV_LOOK_EQUIPMENT, slot, selected_char);
    } else {
        cmd3(CL_CMD_INV_LOOK, slot, 0, selected_char);
    }

    fifo_push(REQ_MANUAL);
    pause_until = tick_now + TT_MANUAL_PAUSE_TICKS;
}

int item_tooltip_capture_line(const char *text, const int font) {
    if (item_details_is_reply_start(text, font)) {
        close_capture();
        if (fifo_count == 0) return 0;

        switch (fifo_pop().role) {
            case REQ_FETCH: capture = CAPTURE_FETCH; return block_append(&flight.fetch, text, font);
            case REQ_SENTINEL: capture = CAPTURE_SENTINEL; return block_append(&flight.sentinel, text, font);
            case REQ_MANUAL: capture = CAPTURE_MANUAL; return 0;
        }
        return 0;
    }

    if (capture == CAPTURE_FETCH) return block_append(&flight.fetch, text, font);
    if (capture == CAPTURE_SENTINEL) return block_append(&flight.sentinel, text, font);
    return 0;
}

void item_tooltip_end_of_tick(void) {
    /* A look reply is written in one go, so it never continues into the next tick. */
    close_capture();
    tick_now++;

    if (flight.active && tick_now - flight.sent_tick > TT_TIMEOUT_TICKS) {
        abandon_flight();
    }
    while (fifo_count > 0 && fifo[fifo_head].role == REQ_MANUAL &&
           tick_now - fifo[fifo_head].sent_tick > TT_TIMEOUT_TICKS) {
        fifo_pop();
    }

    pump();
}

void item_tooltip_hover(const ItemTooltipKind kind, const int slot) {
    hover_kind = kind;
    hover_slot = slot;

    if (kind != last_hover_kind || slot != last_hover_slot) {
        last_hover_kind = kind;
        last_hover_slot = slot;
        hover_start_ms = SDL_GetTicks();
    }
}

static const SlotCache *hovered_cache(void) {
    if (hover_kind < 0) return NULL;
    if (SDL_GetTicks() - hover_start_ms < TT_HOVER_DELAY_MS) return NULL;

    const SlotCache *cache = cache_for((ItemTooltipKind) hover_kind, hover_slot);
    if (!cache || cache->state == SLOT_EMPTY || !cache->has_text) return NULL;
    return cache;
}

const TooltipBlock *item_tooltip_hovered_block(void) {
    const SlotCache *cache = hovered_cache();
    return cache ? &cache->block : NULL;
}

const ItemDetails *item_tooltip_hovered_details(void) {
    const SlotCache *cache = hovered_cache();
    return cache ? &cache->details : NULL;
}

void item_tooltip_cursor_changed(const int sprite) {
    handoff_begin();

    /* The item leaving the cursor went into a slot this tick: it keeps its description there. */
    if (cursor_cache.has_text) {
        for (int arrival = 0; arrival < arrival_count; arrival++) {
            if (arrivals[arrival].sprite != cursor_cache.sprite) continue;

            SlotCache *destination = cache_for(arrivals[arrival].kind, arrivals[arrival].slot);
            if (destination && destination->sprite == arrivals[arrival].sprite) {
                destination->block = cursor_cache.block;
                destination->details = cursor_cache.details;
                destination->has_text = 1;
            }
            break;
        }
    }

    if (!sprite) {
        memset(&cursor_cache, 0, sizeof(cursor_cache));
        arrival_count = vacated_count = 0;
        return;
    }

    /* The item arriving on the cursor left a slot this tick. */
    for (int departure = 0; departure < vacated_count; departure++) {
        if (!vacated[departure].has_text || vacated[departure].sprite != sprite) continue;
        cursor_cache = vacated[departure];
        vacated[departure].has_text = 0;
        arrival_count = 0;
        return;
    }

    /* Otherwise a new item with no known description, or the same one with a new stack count. */
    if (sprite != cursor_cache.sprite) {
        memset(&cursor_cache, 0, sizeof(cursor_cache));
        cursor_cache.sprite = sprite;
    }
    arrival_count = 0;
}

const ItemDetails *item_tooltip_cursor_details(void) {
    if (!pl.citem || !cursor_cache.has_text || cursor_cache.sprite != pl.citem) return NULL;
    return &cursor_cache.details;
}

int item_tooltip_hovered_slot(ItemTooltipKind *kind, int *slot) {
    if (hover_kind < 0 || SDL_GetTicks() - hover_start_ms < TT_HOVER_DELAY_MS) return 0;
    *kind = (ItemTooltipKind) hover_kind;
    *slot = hover_slot;
    return 1;
}

void item_tooltip_frame_end(void) {
    if (hover_kind < 0) {
        last_hover_kind = -1;
        last_hover_slot = -1;
    }
    hover_kind = -1;
    hover_slot = -1;
}
