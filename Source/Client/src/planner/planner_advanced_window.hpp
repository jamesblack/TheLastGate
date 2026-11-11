#pragma once

#include "planner/planner_choices.h"
#include "mods/buff_list.h"

typedef struct {
    int known;
    int ready;
    const char *name;
    const char *warning;
} PlannerAdvancedSpell;

typedef struct {
    PlannerAdvancedSpell spells[BUFF_KIND_COUNT];
} PlannerAdvancedView;

const char *planner_card_name(int card_index);

int planner_advanced_window_render(PlannerChoices *choices, const PlannerAdvancedView *view);
