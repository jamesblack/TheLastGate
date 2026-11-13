#include "tarot_math.h"

#include "util/math_util.h"

#define FOOL_BONUS_DIVISOR 20
#define STRENGTH_REVERSED_HIT_NUMERATOR 4
#define STRENGTH_REVERSED_HIT_DENOMINATOR 5
#define STRENGTH_REVERSED_WEAPON_NUMERATOR 6
#define STRENGTH_REVERSED_WEAPON_DENOMINATOR 5
#define HERMIT_ARMOR_NUMERATOR 23
#define HERMIT_ARMOR_DENOMINATOR 20
#define HANGED_REVERSED_WEAPON_PERCENT 85
#define TEMPERANCE_REVERSED_PERCENT_PER_STACK 10

int tarot_set_has(const TarotSet cards, const int card_index) {
    if (card_index < 0 || card_index >= TAROT_CARD_COUNT) return 0;
    return (cards >> (unsigned int) card_index) & 1U ? 1 : 0;
}

int tarot_floor_divide(const int dividend, const int divisor) {
    const int quotient = dividend / divisor;

    if ((dividend % divisor != 0) && ((dividend < 0) != (divisor < 0))) return quotient - 1;
    return quotient;
}

static int fool_total_for_share(const int share) {
    return share + (share / FOOL_BONUS_DIVISOR);
}

static int fool_share_for_total(const int shown_total) {
    int share = shown_total * FOOL_BONUS_DIVISOR / (FOOL_BONUS_DIVISOR + 1);

    while (fool_total_for_share(share + 1) <= shown_total) share++;
    while (share > 0 && fool_total_for_share(share) > shown_total) share--;
    return share;
}

int tarot_fool_reversed_total(const int shown_total, const int sum_change) {
    const int share = fool_share_for_total(shown_total);
    const int new_share = share + tarot_floor_divide(sum_change, FOOL_POINTS_PER_BREAKPOINT);

    return shown_total + fool_total_for_share(new_share) - fool_total_for_share(share);
}

int tarot_scaled_change(const int server_value, const int raw_change, const int numerator, const int denominator) {
    const int raw_value = server_value * denominator / numerator;

    return ((raw_value + raw_change) * numerator / denominator) - (raw_value * numerator / denominator);
}

static int percent_scaled_change(const int server_value, const int raw_change, const int increased_percent) {
    if (increased_percent == 0) return raw_change;
    return tarot_scaled_change(server_value, raw_change, PERCENT_BASE + increased_percent, PERCENT_BASE);
}

CombatValues tarot_combat_shown(const TarotSet cards, const CombatValues increased_percent, const CombatValues server,
                                const CombatValues raw_change) {
    CombatValues change = raw_change;
    CombatValues shown;
    int weapon_numerator = 1;
    int weapon_denominator = 1;

    if (tarot_set_has(cards, TAROT_LOVERS_REVERSED)) {
        change.hit = change.parry = tarot_floor_divide(change.hit + change.parry, 2);
    }
    change.hit = percent_scaled_change(server.hit, change.hit, increased_percent.hit);
    change.parry = percent_scaled_change(server.parry, change.parry, increased_percent.parry);
    if (tarot_set_has(cards, TAROT_STRENGTH_REVERSED)) {
        change.hit = tarot_scaled_change(server.hit, change.hit, STRENGTH_REVERSED_HIT_NUMERATOR,
                                         STRENGTH_REVERSED_HIT_DENOMINATOR);
    }

    if (tarot_set_has(cards, TAROT_LOVERS)) {
        change.weapon = change.armor = tarot_floor_divide(change.weapon + change.armor, 2);
    }
    if (tarot_set_has(cards, TAROT_HERMIT)) {
        change.armor =
                tarot_scaled_change(server.armor, change.armor, HERMIT_ARMOR_NUMERATOR, HERMIT_ARMOR_DENOMINATOR);
    }

    if (tarot_set_has(cards, TAROT_STRENGTH_REVERSED)) {
        weapon_numerator *= STRENGTH_REVERSED_WEAPON_NUMERATOR;
        weapon_denominator *= STRENGTH_REVERSED_WEAPON_DENOMINATOR;
    }
    if (tarot_set_has(cards, TAROT_HANGED_REVERSED)) {
        weapon_numerator *= HANGED_REVERSED_WEAPON_PERCENT;
        weapon_denominator *= PERCENT_BASE;
    }
    if (tarot_set_has(cards, TAROT_TEMPERANCE_REVERSED)) {
        weapon_numerator *= PERCENT_BASE + (TEMPERANCE_REVERSED_PERCENT_PER_STACK * TAROT_TEMPERANCE_SICKNESS_STACKS);
        weapon_denominator *= PERCENT_BASE;
    }
    if (weapon_numerator != weapon_denominator) {
        change.weapon = tarot_scaled_change(server.weapon, change.weapon, weapon_numerator, weapon_denominator);
    }
    change.weapon = percent_scaled_change(server.weapon, change.weapon, increased_percent.weapon);
    change.armor = percent_scaled_change(server.armor, change.armor, increased_percent.armor);

    shown.hit = server.hit + change.hit;
    shown.parry = server.parry + change.parry;
    shown.weapon = server.weapon + change.weapon;
    shown.armor = server.armor + change.armor;
    return shown;
}
