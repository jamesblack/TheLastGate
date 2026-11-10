#include "tick_pacer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ARRIVAL_WINDOW 256
#define GAP_PERCENTILE 95
#define GAP_TOLERANCE_PERIODS 0.25
#define LONGEST_COUNTED_GAP_MS 200
#define RECALC_EVERY_ARRIVALS 16
#define TARGET_START 4
#define TARGET_MIN 1
#define TARGET_MAX 8
#define TARGET_LOWER_AFTER_ARRIVALS 96
#define RATE_PER_TICK_OF_ERROR 0.08
#define RATE_FASTEST 0.5
#define RATE_SLOWEST 1.25

static unsigned int gaps[ARRIVAL_WINDOW];
static int gap_count;
static int gap_head;
static unsigned int window_span_ms;
static unsigned int last_arrival_ms;
static int has_last_arrival;
static int arrivals_since_recalc;
static int arrivals_since_target_change;
static int buffer_target = TARGET_START;
static double measured_period_ms;
static double carried_ms;

void tick_pacer_reset(void) {
    memset(gaps, 0, sizeof(gaps));
    gap_count = 0;
    gap_head = 0;
    window_span_ms = 0;
    last_arrival_ms = 0;
    has_last_arrival = 0;
    arrivals_since_recalc = 0;
    arrivals_since_target_change = 0;
    buffer_target = TARGET_START;
    measured_period_ms = 0.0;
    carried_ms = 0.0;
}

static int compare_gaps(const void *left, const void *right) {
    const unsigned int left_gap = *(const unsigned int *) left;
    const unsigned int right_gap = *(const unsigned int *) right;
    return (left_gap > right_gap) - (left_gap < right_gap);
}

static void recalculate_target(void) {
    unsigned int sorted_gaps[ARRIVAL_WINDOW];
    unsigned int slow_gap;
    int needed_ticks;

    measured_period_ms = (double) window_span_ms / gap_count;
    if (measured_period_ms < 1.0) return;

    memcpy(sorted_gaps, gaps, sizeof(unsigned int) * gap_count);
    qsort(sorted_gaps, gap_count, sizeof(unsigned int), compare_gaps);
    slow_gap = sorted_gaps[(gap_count - 1) * GAP_PERCENTILE / 100];

    // A gap of N periods drains N-1 ticks from the queue before the next one lands; the tolerance
    // stops a gap of exactly N periods rounding up a whole tick on measurement noise
    needed_ticks = (int) ceil(slow_gap / measured_period_ms - GAP_TOLERANCE_PERIODS) - 1;
    if (needed_ticks < TARGET_MIN) needed_ticks = TARGET_MIN;
    if (needed_ticks > TARGET_MAX) needed_ticks = TARGET_MAX;

    // Grow at once so the next hiccup is covered, shrink one step at a time so a calm spell can't strip the cushion
    if (needed_ticks > buffer_target) {
        buffer_target = needed_ticks;
        arrivals_since_target_change = 0;
    } else if (needed_ticks < buffer_target && arrivals_since_target_change >= TARGET_LOWER_AFTER_ARRIVALS) {
        buffer_target--;
        arrivals_since_target_change = 0;
    }
}

void tick_pacer_on_arrival(const unsigned int now_ms) {
    if (has_last_arrival) {
        unsigned int gap_ms = now_ms - last_arrival_ms;

        // A client stall (e.g. dragging the window) would otherwise inflate the measured period for a whole window;
        // the cap is already beyond the largest buffer target, so the target still sees it as a worst-case gap
        if (gap_ms > LONGEST_COUNTED_GAP_MS) gap_ms = LONGEST_COUNTED_GAP_MS;

        if (gap_count == ARRIVAL_WINDOW) {
            window_span_ms -= gaps[gap_head];
        } else {
            gap_count++;
        }
        gaps[gap_head] = gap_ms;
        window_span_ms += gap_ms;
        gap_head = (gap_head + 1) % ARRIVAL_WINDOW;
    }
    last_arrival_ms = now_ms;
    has_last_arrival = 1;

    arrivals_since_target_change++;
    if (++arrivals_since_recalc >= RECALC_EVERY_ARRIVALS && gap_count >= RECALC_EVERY_ARRIVALS) {
        arrivals_since_recalc = 0;
        recalculate_target();
    }
}

int tick_pacer_target(void) {
    return buffer_target;
}

double tick_pacer_period_ms(void) {
    return measured_period_ms;
}

int tick_pacer_next_tick_ms(const int queued_ticks, const int nominal_tick_ms) {
    const double base_ms = measured_period_ms >= 1.0 ? measured_period_ms : nominal_tick_ms;
    double rate = 1.0 - RATE_PER_TICK_OF_ERROR * (queued_ticks - buffer_target);
    double tick_ms;
    int whole_ms;

    if (rate < RATE_FASTEST) rate = RATE_FASTEST;
    if (rate > RATE_SLOWEST) rate = RATE_SLOWEST;

    // Ticks are whole milliseconds, so carry the fraction to keep the average period exact
    tick_ms = base_ms * rate + carried_ms;
    whole_ms = (int) tick_ms;
    carried_ms = tick_ms - whole_ms;
    return whole_ms;
}
