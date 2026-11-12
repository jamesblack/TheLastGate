/*
 * Unit tests for the background buffs fetch: when it asks, what it captures
 * out of the chat stream, and what it gives up on.
 */

#include <stdio.h>
#include <string.h>

#include "common.h"
#include "mods/buff_watch.h"

struct cplayer pl; // NOLINT(readability-identifier-length)

static int say_count;
static char said_text[100];

void say(char *input) {
    say_count++;
    strncpy(said_text, input, sizeof(said_text) - 1);
}

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static void fresh_start(void) {
    memset(&pl, 0, sizeof(pl));
    say_count = 0;
    said_text[0] = 0;
    buff_watch_reset();
}

static void deliver_bless_reply(const char *power_line) {
    CHECK(buff_watch_capture_line(BUFF_LIST_HEADER, 1) == 1);
    CHECK(buff_watch_capture_line(" ", 1) == 1);
    CHECK(buff_watch_capture_line(power_line, 1) == 1);
    CHECK(buff_watch_capture_line(" ", 1) == 1);
}

static void test_change_sends_one_request(void) {
    fresh_start();
    buff_watch_spell_packet(0, 0, 0, BUFF_SPRITE_BLESS, 16);
    buff_watch_end_of_tick();
    buff_watch_end_of_tick();
    CHECK(say_count == 1);
    CHECK(strcmp(said_text, "#buffs") == 0);
}

static void test_reply_captured_and_stored(void) {
    const BuffReading *reading;

    fresh_start();
    pl.spell[0] = BUFF_SPRITE_BLESS;
    buff_watch_spell_packet(0, 0, 0, BUFF_SPRITE_BLESS, 16);
    buff_watch_end_of_tick();
    deliver_bless_reply("Bless power of 142 for 4m 10s");
    reading = buff_watch_reading(BUFF_KIND_BLESS);
    CHECK(reading != NULL);
    CHECK(reading != NULL && reading->power == 142);
    CHECK(buff_watch_waiting() == 0);
    CHECK(buff_watch_is_active(BUFF_KIND_BLESS) == 1);
    CHECK(buff_watch_is_active(BUFF_KIND_ENHANCE) == 0);
}

static void test_typed_reply_not_captured(void) {
    fresh_start();
    CHECK(buff_watch_capture_line(BUFF_LIST_HEADER, 1) == 0);
    CHECK(buff_watch_capture_line("Bless power of 1 for 0m 1s", 1) == 0);
}

static void test_reading_cleared_on_change(void) {
    fresh_start();
    pl.spell[0] = BUFF_SPRITE_BLESS;
    buff_watch_spell_packet(0, 0, 0, BUFF_SPRITE_BLESS, 16);
    buff_watch_end_of_tick();
    deliver_bless_reply("Bless power of 142 for 4m 10s");
    pl.spell[0] = 0;
    buff_watch_spell_packet(0, BUFF_SPRITE_BLESS, 16, 0, 0);
    CHECK(buff_watch_reading(BUFF_KIND_BLESS) == NULL);
}

static void test_change_during_flight_refetches(void) {
    fresh_start();
    pl.spell[0] = BUFF_SPRITE_BLESS;
    buff_watch_spell_packet(0, 0, 0, BUFF_SPRITE_BLESS, 16);
    {
        int tick_index;
        /* The recast lands a few ticks later, while the first request is still unanswered. */
        for (tick_index = 0; tick_index < 5; tick_index++) buff_watch_end_of_tick();
        buff_watch_spell_packet(0, BUFF_SPRITE_BLESS, 16, BUFF_SPRITE_BLESS, 16);
        deliver_bless_reply("Bless power of 60 for 4m 10s");
        for (tick_index = 0; tick_index < 105; tick_index++) buff_watch_end_of_tick();
    }
    CHECK(say_count == 2);
    /* A recast keeps the last reading on show while the new one is fetched. */
    CHECK(buff_watch_waiting() == 0);
    CHECK(buff_watch_reading(BUFF_KIND_BLESS) != NULL && buff_watch_reading(BUFF_KIND_BLESS)->power == 60);
}

static void test_timeout_releases_capture(void) {
    int tick_index;

    fresh_start();
    buff_watch_spell_packet(0, 0, 0, BUFF_SPRITE_BLESS, 16);
    for (tick_index = 0; tick_index < 32; tick_index++) buff_watch_end_of_tick();
    CHECK(buff_watch_capture_line(BUFF_LIST_HEADER, 1) == 0);
    CHECK(buff_watch_waiting() == 1);
    CHECK(say_count == 1);
}

static void test_capture_closed_at_end_of_tick(void) {
    fresh_start();
    pl.spell[0] = BUFF_SPRITE_BLESS;
    buff_watch_spell_packet(0, 0, 0, BUFF_SPRITE_BLESS, 16);
    buff_watch_end_of_tick();
    CHECK(buff_watch_capture_line(BUFF_LIST_HEADER, 1) == 1);
    CHECK(buff_watch_capture_line(" ", 1) == 1);
    CHECK(buff_watch_capture_line("Bless power of 77 for 4m 10s", 1) == 1);
    buff_watch_end_of_tick();
    CHECK(buff_watch_capture_line("You hit the goblin.", 1) == 0);
    CHECK(buff_watch_reading(BUFF_KIND_BLESS) != NULL);
}

static void run_ticks(const int tick_count) {
    int tick_index;

    for (tick_index = 0; tick_index < tick_count; tick_index++) buff_watch_end_of_tick();
}

static void test_flood_reply_swallowed_and_retried(void) {
    fresh_start();
    buff_watch_spell_packet(0, 0, 0, BUFF_SPRITE_BLESS, 16);
    buff_watch_end_of_tick();
    CHECK(buff_watch_capture_line("Oops, you're a bit too fast for me!", 0) == 1);
    run_ticks(45);
    CHECK(say_count == 2);
}

static void test_timeout_retries_then_gives_up(void) {
    fresh_start();
    buff_watch_spell_packet(0, 0, 0, BUFF_SPRITE_BLESS, 16);
    run_ticks(1000);
    CHECK(say_count == 3);
    CHECK(buff_watch_waiting() == 1);
}

static void test_requests_spaced_apart(void) {
    fresh_start();
    pl.spell[0] = BUFF_SPRITE_BLESS;
    buff_watch_spell_packet(0, 0, 0, BUFF_SPRITE_BLESS, 16);
    buff_watch_end_of_tick();
    deliver_bless_reply("Bless power of 60 for 4m 10s");
    run_ticks(5);
    buff_watch_spell_packet(0, BUFF_SPRITE_BLESS, 16, BUFF_SPRITE_BLESS, 16);
    run_ticks(10);
    CHECK(say_count == 1);
    run_ticks(100);
    CHECK(say_count == 2);
}

static void test_flood_line_from_typing_not_swallowed(void) {
    fresh_start();
    CHECK(buff_watch_capture_line("Oops, you're a bit too fast for me!", 0) == 0);
}

/* Some buff slots are resent unchanged every tick; only the first such resend can be a recast. */
static void test_resend_every_tick_fetches_once(void) {
    int tick_index;

    fresh_start();
    pl.spell[0] = BUFF_SPRITE_BLESS;
    buff_watch_spell_packet(0, 0, 0, BUFF_SPRITE_BLESS, 16);
    buff_watch_end_of_tick();
    deliver_bless_reply("Bless power of 60 for 4m 10s");
    run_ticks(120);
    CHECK(say_count == 1);

    for (tick_index = 0; tick_index < 200; tick_index++) {
        buff_watch_spell_packet(0, BUFF_SPRITE_BLESS, 16, BUFF_SPRITE_BLESS, 16);
        if (tick_index == 1) deliver_bless_reply("Bless power of 66 for 4m 10s");
        buff_watch_end_of_tick();
    }
    CHECK(say_count == 2);
    CHECK(buff_watch_waiting() == 0);
    CHECK(buff_watch_reading(BUFF_KIND_BLESS) != NULL);
    CHECK(buff_watch_reading(BUFF_KIND_BLESS) != NULL && buff_watch_reading(BUFF_KIND_BLESS)->power == 66);
}

/* A refresh, such as an aura or a group member recasting, must not blank the numbers. */
static void test_refresh_keeps_reading_shown(void) {
    fresh_start();
    pl.spell[0] = BUFF_SPRITE_BLESS;
    buff_watch_spell_packet(0, 0, 0, BUFF_SPRITE_BLESS, 16);
    buff_watch_end_of_tick();
    deliver_bless_reply("Bless power of 142 for 4m 10s");
    run_ticks(5);
    buff_watch_spell_packet(0, BUFF_SPRITE_BLESS, 10, BUFF_SPRITE_BLESS, 16);
    CHECK(buff_watch_waiting() == 0);
    CHECK(buff_watch_reading(BUFF_KIND_BLESS) != NULL && buff_watch_reading(BUFF_KIND_BLESS)->power == 142);
    run_ticks(120);
    CHECK(say_count == 2);
}

static void test_new_buff_waits_for_reading(void) {
    fresh_start();
    pl.spell[0] = BUFF_SPRITE_BLESS;
    buff_watch_spell_packet(0, 0, 0, BUFF_SPRITE_BLESS, 16);
    buff_watch_end_of_tick();
    deliver_bless_reply("Bless power of 142 for 4m 10s");
    run_ticks(5);
    pl.spell[1] = BUFF_SPRITE_ENHANCE;
    buff_watch_spell_packet(1, 0, 0, BUFF_SPRITE_ENHANCE, 16);
    CHECK(buff_watch_waiting() == 1);
    CHECK(buff_watch_reading(BUFF_KIND_ENHANCE) == NULL);
}

int main(void) {
    test_change_sends_one_request();
    test_reply_captured_and_stored();
    test_typed_reply_not_captured();
    test_reading_cleared_on_change();
    test_change_during_flight_refetches();
    test_timeout_releases_capture();
    test_capture_closed_at_end_of_tick();
    test_resend_every_tick_fetches_once();
    test_flood_reply_swallowed_and_retried();
    test_timeout_retries_then_gives_up();
    test_requests_spaced_apart();
    test_flood_line_from_typing_not_swallowed();
    test_refresh_keeps_reading_shown();
    test_new_buff_waits_for_reading();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All buff_watch tests passed\n");
    return 0;
}
