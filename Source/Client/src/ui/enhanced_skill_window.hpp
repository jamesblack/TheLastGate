#pragma once

/*
 * Skill planner window: every attribute, Hitpoints, Mana and known skill with
 * raise controls, and a preview of what the planned raises do to each skill.
 */
void enhanced_skill_window_render(void);

/* Forgets the focused skill; called when the plan is committed. Closing the window keeps it. */
void enhanced_skill_window_clear_focus(void);
