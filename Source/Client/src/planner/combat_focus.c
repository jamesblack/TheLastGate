#include "combat_focus.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "planner/combat_shown.h"
#include "planner/skill_focus.h"
#include "util/math_util.h"

/* A mix of points is a base-(BUFF_FOCUS_MAX_POINTS + 1) number with one digit per candidate. */
#define POINT_RADIX (BUFF_FOCUS_MAX_POINTS + 1)
#define SEEN_CAPACITY ((size_t) COMBAT_FOCUS_MAX_STATES * 2)

typedef struct {
    int cost;
    int point_total;
    uint64_t key;
} QueueEntry;

typedef struct {
    QueueEntry *entries;
    int count;
} Queue;

/* Open addressing over mix keys; a slot holds key + 1 so zero marks it empty. */
typedef struct {
    uint64_t *slots;
    int count;
} SeenSet;

static int candidate_limit(const BuffFocusCandidate *candidate) {
    return clamp(candidate->max_points, 0, BUFF_FOCUS_MAX_POINTS);
}

static void decode_points(const CombatFocusInput *input, uint64_t key, int points[BUFF_FOCUS_MAX_CANDIDATES]) {
    int candidate_index;

    memset(points, 0, sizeof(int) * BUFF_FOCUS_MAX_CANDIDATES);
    for (candidate_index = 0; candidate_index < input->candidate_count; candidate_index++) {
        points[candidate_index] = (int) (key % POINT_RADIX);
        key /= POINT_RADIX;
    }
}

static int comes_before(const QueueEntry *first, const QueueEntry *second) {
    if (first->cost != second->cost) return first->cost < second->cost;
    if (first->point_total != second->point_total) return first->point_total < second->point_total;
    return first->key < second->key;
}

static void queue_push(Queue *queue, const QueueEntry entry) {
    int position = queue->count++;

    while (position > 0) {
        const int parent = (position - 1) / 2;
        if (!comes_before(&entry, &queue->entries[parent])) break;
        queue->entries[position] = queue->entries[parent];
        position = parent;
    }
    queue->entries[position] = entry;
}

static QueueEntry queue_pop(Queue *queue) {
    const QueueEntry first = queue->entries[0];
    const QueueEntry last = queue->entries[--queue->count];
    int position = 0;

    for (;;) {
        const int left = (position * 2) + 1;
        const int right = left + 1;
        int smallest = left;

        if (left >= queue->count) break;
        if (right < queue->count && comes_before(&queue->entries[right], &queue->entries[left])) smallest = right;
        if (!comes_before(&queue->entries[smallest], &last)) break;
        queue->entries[position] = queue->entries[smallest];
        position = smallest;
    }
    if (queue->count > 0) queue->entries[position] = last;
    return first;
}

/* Adds the key; returns 0 when it was already there. */
static int seen_add(SeenSet *seen, const uint64_t key) {
    size_t slot = (size_t) ((key * 0x9E3779B97F4A7C15ULL) >> 32) % SEEN_CAPACITY;

    while (seen->slots[slot] != 0) {
        if (seen->slots[slot] == key + 1) return 0;
        slot = (slot + 1) % SEEN_CAPACITY;
    }
    seen->slots[slot] = key + 1;
    seen->count++;
    return 1;
}

static int stat_with_points(const CombatFocusInput *input, const int points[BUFF_FOCUS_MAX_CANDIDATES]) {
    BuffModelInput model = input->model;
    BuffModelSkill combat_skills[COMBAT_SKILL_COUNT];
    BuffModelResult buffed;
    CombatValues shown;
    int candidate_index;
    int skill_id;
    int kind;

    memcpy(combat_skills, input->combat_skills, sizeof(combat_skills));
    for (candidate_index = 0; candidate_index < input->candidate_count; candidate_index++) {
        const int raise_index = input->candidates[candidate_index].raise_index;
        const int extra = points[candidate_index];

        if (extra == 0) continue;
        if (raise_index >= 0 && raise_index < ATTRIBUTE_COUNT) model.planned_attributes[raise_index] += extra;
        /* Under Reverse Fool only the sum of planned attribute points matters, so any attribute can hold them. */
        if (raise_index == BUFF_FOCUS_ATTRIBUTE_POOL) model.planned_attributes[0] += extra;
        if (raise_index == BUFF_FOCUS_HITPOINTS_RAISE) model.planned_hitpoints += extra;
        if (raise_index == BUFF_FOCUS_MANA_RAISE) model.planned_mana += extra;
        for (kind = 0; kind < BUFF_KIND_COUNT; kind++) {
            if (raise_index == input->spell_raise_index[kind]) model.spell_skills[kind].planned += extra;
        }
        for (skill_id = 0; skill_id < COMBAT_SKILL_COUNT; skill_id++) {
            if (raise_index == input->combat_raise_index[skill_id]) combat_skills[skill_id].planned += extra;
        }
    }

    buff_model_compute(&model, &buffed);
    shown = combat_shown_values(&model, &buffed, &input->combat, combat_skills);
    return combat_model_stat_value(&shown, input->stat);
}

static int points_cost(const BuffFocusCandidate *candidate, const int points) {
    int total = 0;
    int step_index;

    for (step_index = 0; step_index < points; step_index++) total += candidate->step_costs[step_index];
    return total;
}

static BuffFocusRoute to_route(const CombatFocusInput *input, const QueueEntry *entry) {
    int points[BUFF_FOCUS_MAX_CANDIDATES];
    BuffFocusRoute route;
    int candidate_index;

    memset(&route, 0, sizeof(route));
    decode_points(input, entry->key, points);
    route.available = 1;
    route.total_cost = entry->cost;
    for (candidate_index = 0; candidate_index < input->candidate_count; candidate_index++) {
        const BuffFocusCandidate *candidate = &input->candidates[candidate_index];
        BuffFocusRaise raise;
        int sorted_index;

        if (points[candidate_index] == 0) continue;
        raise.raise_index = candidate->raise_index;
        raise.points = points[candidate_index];
        raise.cost = points_cost(candidate, points[candidate_index]);

        sorted_index = route.raise_count;
        while (sorted_index > 0 && route.raises[sorted_index - 1].raise_index > raise.raise_index) {
            route.raises[sorted_index] = route.raises[sorted_index - 1];
            sorted_index--;
        }
        route.raises[sorted_index] = raise;
        route.raise_count++;
    }
    return route;
}

/* Visits mixes cheapest first, so the first one that reaches +1 is the cheapest route. */
CombatFocusResult combat_focus_search(const CombatFocusInput *input) {
    CombatFocusResult result;
    uint64_t digit_values[BUFF_FOCUS_MAX_CANDIDATES];
    int points[BUFF_FOCUS_MAX_CANDIDATES];
    Queue queue;
    SeenSet seen;
    QueueEntry start;
    int candidate_index;
    int target;

    memset(&result, 0, sizeof(result));
    memset(points, 0, sizeof(points));
    result.current_value = stat_with_points(input, points);
    target = result.current_value + 1;

    queue.entries = malloc(sizeof(QueueEntry) * COMBAT_FOCUS_MAX_STATES);
    queue.count = 0;
    seen.slots = calloc(SEEN_CAPACITY, sizeof(uint64_t));
    seen.count = 0;
    if (queue.entries == NULL || seen.slots == NULL) goto finished;

    for (candidate_index = 0; candidate_index < input->candidate_count; candidate_index++) {
        digit_values[candidate_index] = candidate_index == 0 ? 1 : digit_values[candidate_index - 1] * POINT_RADIX;
    }

    start.cost = 0;
    start.point_total = 0;
    start.key = 0;
    seen_add(&seen, start.key);
    queue_push(&queue, start);

    while (queue.count > 0) {
        const QueueEntry entry = queue_pop(&queue);

        decode_points(input, entry.key, points);
        if (entry.key != 0 && stat_with_points(input, points) >= target) {
            result.best = to_route(input, &entry);
            break;
        }
        for (candidate_index = 0; candidate_index < input->candidate_count; candidate_index++) {
            const BuffFocusCandidate *candidate = &input->candidates[candidate_index];
            const int step = points[candidate_index];
            QueueEntry next;

            if (step >= candidate_limit(candidate) || candidate->step_costs[step] == SKILL_FOCUS_NO_COST) continue;
            if (seen.count >= COMBAT_FOCUS_MAX_STATES) continue;
            next.cost = entry.cost + candidate->step_costs[step];
            next.point_total = entry.point_total + 1;
            next.key = entry.key + digit_values[candidate_index];
            if (seen_add(&seen, next.key)) queue_push(&queue, next);
        }
    }

finished:
    free(queue.entries);
    free(seen.slots);
    return result;
}
