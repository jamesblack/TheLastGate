#include <stdio.h>

#include "net/tick_pacer.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static unsigned int feed_steady(unsigned int now_ms, const int arrival_count, const unsigned int gap_ms) {
    int arrival_index;

    for (arrival_index = 0; arrival_index < arrival_count; arrival_index++) {
        tick_pacer_on_arrival(now_ms);
        now_ms += gap_ms;
    }
    return now_ms;
}

static void test_starts_with_safe_target(void) {
    tick_pacer_reset();
    CHECK(tick_pacer_target() == 4);
}

static void test_steady_connection_settles_at_minimum(void) {
    tick_pacer_reset();
    feed_steady(1000, 1000, 21);
    CHECK(tick_pacer_target() == 1);
    CHECK(tick_pacer_period_ms() > 20.5 && tick_pacer_period_ms() < 21.5);
}

static void test_bursty_connection_raises_target(void) {
    unsigned int now_ms = 1000;
    int burst_index;

    tick_pacer_reset();
    now_ms = feed_steady(now_ms, 1000, 21);
    // Three ticks land together every 63ms, so each gap after a burst is three periods long
    for (burst_index = 0; burst_index < 200; burst_index++) {
        tick_pacer_on_arrival(now_ms);
        tick_pacer_on_arrival(now_ms);
        tick_pacer_on_arrival(now_ms);
        now_ms += 63;
    }
    CHECK(tick_pacer_target() == 2);
}

static void test_target_lowers_slowly_after_calm_returns(void) {
    unsigned int now_ms = 1000;
    int burst_index;

    tick_pacer_reset();
    // Five ticks per 105ms burst need a four-tick cushion, which holds the starting target
    for (burst_index = 0; burst_index < 200; burst_index++) {
        tick_pacer_on_arrival(now_ms);
        tick_pacer_on_arrival(now_ms);
        tick_pacer_on_arrival(now_ms);
        tick_pacer_on_arrival(now_ms);
        tick_pacer_on_arrival(now_ms);
        now_ms += 105;
    }
    CHECK(tick_pacer_target() == 4);

    // One window of steady arrivals clears the bursty gaps but only allows a step or two down
    now_ms = feed_steady(now_ms, 300, 21);
    CHECK(tick_pacer_target() >= 2 && tick_pacer_target() <= 3);

    now_ms = feed_steady(now_ms, 1000, 21);
    CHECK(tick_pacer_target() == 1);
}

static void test_queue_on_target_plays_at_measured_period(void) {
    int tick_index, total_ms = 0;

    tick_pacer_reset();
    feed_steady(1000, 1000, 21);
    for (tick_index = 0; tick_index < 100; tick_index++) total_ms += tick_pacer_next_tick_ms(1, 20);
    CHECK(total_ms >= 2095 && total_ms <= 2105);
}

static void test_rate_nudges_toward_target(void) {
    tick_pacer_reset();
    feed_steady(1000, 1000, 20);
    CHECK(tick_pacer_next_tick_ms(3, 20) < 20);
    CHECK(tick_pacer_next_tick_ms(0, 20) > 20);
}

static void test_rate_is_clamped(void) {
    tick_pacer_reset();
    feed_steady(1000, 1000, 20);
    CHECK(tick_pacer_next_tick_ms(100, 20) == 10);
    CHECK(tick_pacer_next_tick_ms(0, 20) <= 25);
}

static void test_long_stall_barely_moves_period(void) {
    unsigned int now_ms;

    tick_pacer_reset();
    now_ms = feed_steady(1000, 1000, 20);
    now_ms += 2000;
    feed_steady(now_ms, 32, 20);
    CHECK(tick_pacer_period_ms() < 21.0);
}

static void test_uses_nominal_before_any_measurement(void) {
    tick_pacer_reset();
    CHECK(tick_pacer_next_tick_ms(4, 20) == 20);
}

int main(void) {
    test_starts_with_safe_target();
    test_steady_connection_settles_at_minimum();
    test_bursty_connection_raises_target();
    test_target_lowers_slowly_after_calm_returns();
    test_queue_on_target_plays_at_measured_period();
    test_rate_nudges_toward_target();
    test_rate_is_clamped();
    test_long_stall_barely_moves_period();
    test_uses_nominal_before_any_measurement();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("tick_pacer: all checks passed\n");
    return 0;
}
