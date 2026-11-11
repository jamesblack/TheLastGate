#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define ATTRIBUTE_BRAVENESS 0
#define ATTRIBUTE_WILLPOWER 1
#define ATTRIBUTE_INTUITION 2
#define ATTRIBUTE_AGILITY 3
#define ATTRIBUTE_STRENGTH 4
#define ATTRIBUTE_COUNT 5

#define ATTRIBUTES_PER_SKILL 3

extern char *attribute_names[ATTRIBUTE_COUNT];

#ifdef __cplusplus
}
#endif
