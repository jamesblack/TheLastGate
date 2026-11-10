#include <stdio.h>

#include "game/spectate_watch.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

#define OWN 12
#define TARGET 40
#define OTHER_TARGET 55

static SpectateWatch started_watch(void) {
    SpectateWatch watch;
    spectate_watch_reset(&watch);
    spectate_watch_log_line(&watch, "Now spectating Bob. Use /spectate self to return.", OWN);
    return watch;
}

static void test_ordinary_lines_change_nothing(void) {
    SpectateWatch watch;
    spectate_watch_reset(&watch);
    spectate_watch_log_line(&watch, "Bob says: \"Now spectating nothing\"", OWN);
    spectate_watch_view_centre(&watch, TARGET);
    CHECK(!spectate_watch_active(&watch));
}

static void test_start_message_pauses_before_view_moves(void) {
    SpectateWatch watch = started_watch();
    CHECK(spectate_watch_active(&watch));
    spectate_watch_view_centre(&watch, OWN);
    CHECK(spectate_watch_active(&watch));
    spectate_watch_view_centre(&watch, TARGET);
    CHECK(spectate_watch_active(&watch));
}

static void test_view_returning_to_own_character_ends_spectating(void) {
    SpectateWatch watch = started_watch();
    spectate_watch_view_centre(&watch, TARGET);
    spectate_watch_view_centre(&watch, 0);
    CHECK(spectate_watch_active(&watch));
    spectate_watch_view_centre(&watch, OWN);
    CHECK(!spectate_watch_active(&watch));
}

static void test_stop_message_ends_spectating(void) {
    SpectateWatch watch = started_watch();
    spectate_watch_view_centre(&watch, TARGET);
    spectate_watch_log_line(&watch, "No longer spectating.", TARGET);
    CHECK(!spectate_watch_active(&watch));
}

static void test_switching_target_keeps_own_character(void) {
    SpectateWatch watch = started_watch();
    spectate_watch_view_centre(&watch, TARGET);
    spectate_watch_log_line(&watch, "Now spectating Carl. Use /spectate self to return.", TARGET);
    spectate_watch_view_centre(&watch, TARGET);
    spectate_watch_view_centre(&watch, OTHER_TARGET);
    CHECK(spectate_watch_active(&watch));
    spectate_watch_view_centre(&watch, OWN);
    CHECK(!spectate_watch_active(&watch));
}

static void test_unknown_own_character_waits_for_stop_message(void) {
    SpectateWatch watch;
    spectate_watch_reset(&watch);
    spectate_watch_log_line(&watch, "Now spectating Bob. Use /spectate self to return.", 0);
    spectate_watch_view_centre(&watch, TARGET);
    spectate_watch_view_centre(&watch, OWN);
    CHECK(spectate_watch_active(&watch));
    spectate_watch_log_line(&watch, "No longer spectating.", OWN);
    CHECK(!spectate_watch_active(&watch));
}

static void test_target_leaving_game_ends_spectating(void) {
    SpectateWatch watch = started_watch();
    spectate_watch_view_centre(&watch, TARGET);
    spectate_watch_log_line(&watch, "Bobby left the game.", TARGET);
    CHECK(spectate_watch_active(&watch));
    spectate_watch_log_line(&watch, "Carl left the game.", TARGET);
    CHECK(spectate_watch_active(&watch));
    spectate_watch_log_line(&watch, "Bob left the game.", TARGET);
    CHECK(!spectate_watch_active(&watch));
}

static void test_target_leaving_without_goodbye_ends_spectating(void) {
    SpectateWatch watch = started_watch();
    spectate_watch_view_centre(&watch, TARGET);
    spectate_watch_log_line(&watch, "Bob left the game without saying goodbye.", TARGET);
    CHECK(!spectate_watch_active(&watch));
}

static void test_switching_target_watches_new_name(void) {
    SpectateWatch watch = started_watch();
    spectate_watch_log_line(&watch, "Now spectating Carl. Use /spectate self to return.", TARGET);
    spectate_watch_log_line(&watch, "Bob left the game.", OTHER_TARGET);
    CHECK(spectate_watch_active(&watch));
    spectate_watch_log_line(&watch, "Carl left the game.", OTHER_TARGET);
    CHECK(!spectate_watch_active(&watch));
}

static void test_left_game_ignored_when_not_spectating(void) {
    SpectateWatch watch;
    spectate_watch_reset(&watch);
    spectate_watch_log_line(&watch, " left the game.", OWN);
    CHECK(!spectate_watch_active(&watch));
}

int main(void) {
    test_ordinary_lines_change_nothing();
    test_start_message_pauses_before_view_moves();
    test_view_returning_to_own_character_ends_spectating();
    test_stop_message_ends_spectating();
    test_switching_target_keeps_own_character();
    test_unknown_own_character_waits_for_stop_message();
    test_target_leaving_game_ends_spectating();
    test_target_leaving_without_goodbye_ends_spectating();
    test_switching_target_watches_new_name();
    test_left_game_ignored_when_not_spectating();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All spectate watch tests passed\n");
    return 0;
}
