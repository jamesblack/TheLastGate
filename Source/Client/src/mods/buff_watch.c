#include "buff_watch.h"

#include <string.h>

#include "inter.h"

#define BUFF_WATCH_TIMEOUT_TICKS 30 /* give up on a reply after this long */
/* Every command the player sends counts against the server's chat flood limit, so hidden requests are spaced out. */
#define BUFF_WATCH_MISSING_GAP_TICKS 40   /* a buff is waiting for its first reading */
#define BUFF_WATCH_REFRESH_GAP_TICKS 100  /* every reading is on show; this only refreshes them */
#define BUFF_WATCH_RETRY_LIMIT 2 /* extra attempts after a lost or rejected request */
#define BUFF_WATCH_COMMAND "#buffs"
#define BUFF_WATCH_FLOOD_REPLY "Oops, you're a bit too fast for me!"
#define SAY_BUFFER_SIZE 128

static BuffListParser parser;
static BuffListParser stored;
static int fetch_pending;
static int request_in_flight;
static int capturing;
static int readings_fresh;
static int tick_now;
static int request_tick;
static int last_send_tick;
static int retries_left;
/* Tick a buff we hadn't read appeared; only a request sent after it can make the readings whole again. */
static int new_buff_tick;
/* Tick of each slot's last packet. Some slots are resent unchanged every tick, which is not a recast. */
static int slot_packet_tick[MAXBUFFS];

void buff_watch_reset(void) {
    buff_list_parser_start(&parser);
    buff_list_parser_start(&stored);
    fetch_pending = 0;
    request_in_flight = 0;
    capturing = 0;
    readings_fresh = 1;
    tick_now = 0;
    request_tick = 0;
    last_send_tick = -BUFF_WATCH_REFRESH_GAP_TICKS;
    retries_left = BUFF_WATCH_RETRY_LIMIT;
    new_buff_tick = -1;
    memset(slot_packet_tick, 0, sizeof(slot_packet_tick));
}

/* The request got no usable reply; try again later while attempts remain. */
static void request_failed(void) {
    request_in_flight = 0;
    if (retries_left > 0) {
        retries_left--;
        fetch_pending = 1;
    }
}

static void finish_capture(void) {
    capturing = 0;
    request_in_flight = 0;
    stored = parser;
    if (request_tick > new_buff_tick) readings_fresh = 1;
    retries_left = BUFF_WATCH_RETRY_LIMIT;
}

void buff_watch_spell_packet(const int slot, const int old_sprite, const int old_active, const int new_sprite,
                             const int new_active) {
    const int unchanged = old_sprite == new_sprite && old_active == new_active;
    int resent_every_tick = 0;

    if (slot >= 0 && slot < MAXBUFFS) {
        resent_every_tick = slot_packet_tick[slot] > 0 && tick_now - slot_packet_tick[slot] <= 1;
        slot_packet_tick[slot] = tick_now + 1;
    }
    if (unchanged && resent_every_tick) return;
    if (!buff_list_slot_needs_fetch(old_sprite, old_active, new_sprite, new_active)) return;
    fetch_pending = 1;
    retries_left = BUFF_WATCH_RETRY_LIMIT;
    /* A refresh or recast keeps showing the last reading while a new one is fetched; a new buff has none yet. */
    if (new_sprite != old_sprite && buff_list_sprite_kind(new_sprite) != BUFF_KIND_NONE) {
        readings_fresh = 0;
        new_buff_tick = tick_now;
    }
}

int buff_watch_capture_line(const char *text, const int font) {
    (void) font;

    if (capturing) {
        if (!buff_list_parser_feed(&parser, text)) finish_capture();
        return 1;
    }
    if (request_in_flight && buff_list_is_header(text)) {
        buff_list_parser_start(&parser);
        capturing = 1;
        return 1;
    }
    /* The server turned our request away for flooding; that refusal is ours, not the player's. */
    if (request_in_flight && strcmp(text, BUFF_WATCH_FLOOD_REPLY) == 0) {
        request_failed();
        return 1;
    }
    return 0;
}

void buff_watch_end_of_tick(void) {
    char command_text[SAY_BUFFER_SIZE];

    tick_now++;
    /* The reply is written in one go, so a capture never continues into the next tick. */
    if (capturing) finish_capture();
    if (request_in_flight && !capturing && tick_now - request_tick > BUFF_WATCH_TIMEOUT_TICKS) {
        request_failed();
    }
    if (!fetch_pending || request_in_flight) return;
    if (tick_now - last_send_tick < (readings_fresh ? BUFF_WATCH_REFRESH_GAP_TICKS : BUFF_WATCH_MISSING_GAP_TICKS)) {
        return;
    }

    memset(command_text, 0, sizeof(command_text));
    strcpy(command_text, BUFF_WATCH_COMMAND);
    say(command_text);
    fetch_pending = 0;
    request_in_flight = 1;
    request_tick = tick_now;
    last_send_tick = tick_now;
}

int buff_watch_is_active(const BuffKind kind) {
    int slot;

    for (slot = 0; slot < MAXBUFFS; slot++) {
        if (pl.spell[slot] > 0 && buff_list_sprite_kind(pl.spell[slot]) == kind) return 1;
    }
    return 0;
}

const BuffReading *buff_watch_reading(const BuffKind kind) {
    if (kind < 0 || kind >= BUFF_KIND_COUNT) return NULL;
    if (!readings_fresh || !buff_watch_is_active(kind) || !stored.readings[kind].known) return NULL;
    return &stored.readings[kind];
}

int buff_watch_waiting(void) {
    return !readings_fresh;
}
