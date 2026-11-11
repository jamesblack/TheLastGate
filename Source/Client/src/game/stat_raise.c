#include "stat_raise.h"

#include "common.h"
#include "engine.h"
#include "inter.h"

#define SHIFT_CLICK_MULTIPLIER 10
#define CTRL_CLICK_MULTIPLIER 90

static int is_valid_index(const int raise_index) {
    /* Skill slots exist only for positions inside the skill table. */
    return raise_index >= 0 && raise_index < STAT_RAISE_SLOT_COUNT && raise_index < STAT_RAISE_SKILL_BASE + MAXSKILL;
}

static int cost_at_planned(const int raise_index, const int planned) {
    if (raise_index < ATTRIBUTE_COUNT) {
        return attrib_needed(raise_index, pl.attrib[raise_index][0] + planned);
    }
    if (raise_index == STAT_RAISE_HITPOINTS) return hp_needed(pl.hp[0] + planned);
    if (raise_index == STAT_RAISE_MANA) return mana_needed(pl.mana[0] + planned);
    if (raise_index >= STAT_RAISE_SKILL_BASE) {
        const int skill_id = skilltab[raise_index - STAT_RAISE_SKILL_BASE].nr;
        return skill_needed(skill_id, pl.skill[skill_id][0] + planned);
    }
    return HIGH_VAL; // NOLINT(bugprone-signed-bitwise)
}

int stat_raise_cost(const int raise_index) {
    return stat_raise_cost_after(raise_index, 0);
}

int stat_raise_cost_after(const int raise_index, const int extra_points) {
    if (!is_valid_index(raise_index) || extra_points < 0) return HIGH_VAL; // NOLINT(bugprone-signed-bitwise)
    return cost_at_planned(raise_index, stat_raised[raise_index] + extra_points);
}

int stat_raise_try_raise(const int raise_index) {
    const int cost = stat_raise_cost(raise_index);

    if (cost == HIGH_VAL || cost > pl.points - stat_points_used) return 0; // NOLINT(bugprone-signed-bitwise)
    stat_points_used += cost;
    stat_raised[raise_index]++;
    return 1;
}

int stat_raise_try_lower(const int raise_index) {
    if (!is_valid_index(raise_index) || stat_raised[raise_index] <= 0) return 0;
    stat_raised[raise_index]--;
    stat_points_used -= cost_at_planned(raise_index, stat_raised[raise_index]);
    return 1;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int stat_raise_try_raise_bundle(const int raise_indices[], const int point_counts[], const int entry_count) {
    int entry_index;
    int point_index;

    for (entry_index = 0; entry_index < entry_count; entry_index++) {
        for (point_index = 0; point_index < point_counts[entry_index]; point_index++) {
            if (stat_raise_try_raise(raise_indices[entry_index])) continue;

            /* Undo everything planned so far, newest first, so the plan is left as it was. */
            while (point_index-- > 0) stat_raise_try_lower(raise_indices[entry_index]);
            while (entry_index-- > 0) {
                for (point_index = 0; point_index < point_counts[entry_index]; point_index++) {
                    stat_raise_try_lower(raise_indices[entry_index]);
                }
            }
            return 0;
        }
    }
    return 1;
}

void stat_raise_commit_all(void) {
    int raise_index;

    for (raise_index = 0; raise_index < STAT_RAISE_SLOT_COUNT; raise_index++) {
        if (stat_raised[raise_index]) {
            /* The server identifies skills by skill id, not by their sorted position. */
            const int stat_id = raise_index >= STAT_RAISE_SKILL_BASE
                                        ? skilltab[raise_index - STAT_RAISE_SKILL_BASE].nr + STAT_RAISE_SKILL_BASE
                                        : raise_index;
            cmd(CL_CMD_STAT, stat_id, stat_raised[raise_index]);
        }
    }
    stat_raise_reset_all();
}

void stat_raise_reset_all(void) {
    int raise_index;

    for (raise_index = 0; raise_index < STAT_RAISE_SLOT_COUNT; raise_index++) stat_raised[raise_index] = 0;
    stat_points_used = 0;
}

static int sorted_skill_numbers[MAXSKILL];
static int has_sorted_skill_list = 0;
static int skill_list_version = 0;

void stat_raise_skill_list_sorted(void) {
    int skill_position;
    int order_changed = 0;

    for (skill_position = 0; skill_position < MAXSKILL; skill_position++) {
        if (sorted_skill_numbers[skill_position] != skilltab[skill_position].nr) order_changed = 1;
        sorted_skill_numbers[skill_position] = skilltab[skill_position].nr;
    }
    if (order_changed && has_sorted_skill_list) {
        stat_raise_reset_all();
        skill_list_version++;
    }
    has_sorted_skill_list = 1;
}

int stat_raise_skill_list_version(void) {
    return skill_list_version;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int stat_raise_click_multiplier(const int shift_held, const int ctrl_or_alt_held) {
    if (ctrl_or_alt_held) return CTRL_CLICK_MULTIPLIER;
    if (shift_held) return SHIFT_CLICK_MULTIPLIER;
    return 1;
}
