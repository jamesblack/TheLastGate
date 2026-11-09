#pragma once

/*
 * Preview of how planned attribute raises change a skill's value.
 *
 * A skill gains (sum of its three attributes) / divisor from its attributes,
 * truncated, so a raise only shows up once the sum crosses a multiple of the
 * divisor. Self-contained (no game headers) so it can be built into the unit
 * tests.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define SKILL_PREVIEW_ATTRIBUTE_COUNT 3

/* Divisor applied to the attribute sum: 5 normally, 6 in Hard Mode. */
int skill_preview_attribute_divisor(int is_hard_mode);

/* Extra skill points the planned raises give on top of the current attribute share. */
int skill_preview_attribute_gain(const int attribute_values[SKILL_PREVIEW_ATTRIBUTE_COUNT],
                                 const int attribute_raises[SKILL_PREVIEW_ATTRIBUTE_COUNT], int divisor);

int skill_preview_uses_raised_attribute(const int attribute_raises[SKILL_PREVIEW_ATTRIBUTE_COUNT]);

#ifdef __cplusplus
}
#endif
