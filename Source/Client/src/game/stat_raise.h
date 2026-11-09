#pragma once

/*
 * Planned stat raises shared by the skill screen and the enhanced skill window.
 *
 * Planned raises live in stat_raised[], indexed: 0-4 attributes, 5 Hitpoints,
 * 7 Mana, and STAT_RAISE_SKILL_BASE + skilltab position for skills. Index 6
 * (Endurance) cannot be raised. stat_points_used holds the exp the plan costs.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define STAT_RAISE_ATTRIBUTE_COUNT 5
#define STAT_RAISE_HITPOINTS 5
#define STAT_RAISE_MANA 7
#define STAT_RAISE_SKILL_BASE 8
#define STAT_RAISE_SLOT_COUNT 108

/* Exp cost of the next +1 for that slot, or HIGH_VAL when it cannot be raised. */
int stat_raise_cost(int raise_index);

/* Cost of the point that would follow extra_points more planned points; HIGH_VAL when invalid or capped. */
int stat_raise_cost_after(int raise_index, int extra_points);

/* Plans point_counts[n] points in raise_indices[n] for every entry, or nothing at all. Returns 1 on success. */
int stat_raise_try_raise_bundle(const int raise_indices[], const int point_counts[], int entry_count);

int stat_raise_try_raise(int raise_index);

int stat_raise_try_lower(int raise_index);

void stat_raise_commit_all(void);

void stat_raise_reset_all(void);

/*
 * Call after every skill list sort. When the order differs from the last sort, planned
 * skill raises would land on the wrong skills, so the whole plan is cleared.
 */
void stat_raise_skill_list_sorted(void);

/* Goes up by one each time a skill list change cleared the plan. */
int stat_raise_skill_list_version(void);

/* Points per click: 90 with Ctrl or Alt held, 10 with Shift, else 1. */
int stat_raise_click_multiplier(int shift_held, int ctrl_or_alt_held);

#ifdef __cplusplus
}
#endif
