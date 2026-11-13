#pragma once

#include "game/attributes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TREE_NODE_COUNT 12
#define TREE_EFFECTS_PER_150 150 /* Determination and Brilliance divide the attribute sum by this */
#define TREE_EFFECTS_PER_75 75   /* the Seyan Determination and Brilliance nodes divide it by this */

typedef enum {
    TREE_CLASS_NONE,
    TREE_CLASS_SEYAN,
    TREE_CLASS_ARCHTEMPLAR,
    TREE_CLASS_SKALD,
    TREE_CLASS_WARRIOR,
    TREE_CLASS_SORCERER,
    TREE_CLASS_SUMMONER,
    TREE_CLASS_ARCHHARAKIM,
    TREE_CLASS_BRAVER,
    TREE_CLASS_LYCANTH,
} TreeClass;

typedef enum {
    CORRUPTOR_FALSE_STRENGTH,
    CORRUPTOR_FALSE_AGILITY,
    CORRUPTOR_FALSE_WILLPOWER,
    CORRUPTOR_FALSE_INTUITION,
    CORRUPTOR_FALSE_BRAVENESS,
    CORRUPTOR_FALSE_HARRIER,
    CORRUPTOR_FALSE_PRAGMATISM,
    CORRUPTOR_FALSE_BARBARISM,
    CORRUPTOR_FALSE_CELERITY,
    CORRUPTOR_FALSE_MYSTICISM,
    CORRUPTOR_FALSE_CONCENTRATION,
    CORRUPTOR_FALSE_VIRTUOSITY,
    CORRUPTOR_FALSE_RIGOR,
    CORRUPTOR_FALSE_FLEXIBILITY,
    CORRUPTOR_FALSE_VALOR,
    CORRUPTOR_FALSE_UNBREAKABLE,
    CORRUPTOR_FALSE_DETERMINATION,
    CORRUPTOR_FALSE_INDIFFERENCE,
    CORRUPTOR_FALSE_CONSTITUTION,
    CORRUPTOR_FALSE_TACTICIAN,
    CORRUPTOR_FALSE_ZEALOTRY,
    CORRUPTOR_FALSE_BRILLIANCE,
    CORRUPTOR_FALSE_INTROSPECTION,
    CORRUPTOR_FALSE_WIZARDRY,
    CORRUPTOR_GUARDIAN_ANGEL,
    CORRUPTOR_COUNT,
} Corruptor;

typedef struct {
    int server_number; /* 1-based position in the server's corruptor list */
    const char *name;
    const char *one_copy; /* effect text for one copy, shown in the row */
    int modelled;         /* 0 for False Tactician */
} CorruptorInfo;

extern const CorruptorInfo CORRUPTOR_INFO[CORRUPTOR_COUNT];

typedef struct {
    TreeClass tree_class;
    unsigned int tree_points;                        /* pl.tree_points */
    unsigned char tree_node[TREE_NODE_COUNT];        /* pl.tree_node */
    unsigned int corruptor_ticks;                    /* bit 1 << Corruptor */
    unsigned char corruptor_counts[CORRUPTOR_COUNT]; /* card and ring copies */
} TreeEffectsInput;

typedef struct {
    int attribute_percent[ATTRIBUTE_COUNT];   /* total attribute x (100 + p) / 100 */
    int skill_extra_percent[ATTRIBUTE_COUNT]; /* Brv: passives, Wil/Int: active spells, Agl/Str: active melee */
    int hit_percent, parry_percent, weapon_percent, armor_percent;
    int immunity_percent, resist_percent, aptitude_percent, spell_bonus_percent;
    int determination;       /* WV and AV + determination x attribute sum / 150 */
    int determination_nodes; /* WV and AV + determination_nodes x (attribute sum / 75) */
    int brilliance;          /* Spell Bonus + brilliance x attribute sum / 150 */
    int brilliance_nodes;    /* Spell Bonus + brilliance_nodes x (attribute sum / 75) */
    int introspection;  /* Spell Bonus + introspection x aptitude / 50 */
    int guardian_angel; /* Bless power more(power, Braveness x guardian_angel, 20) */
} PlannerEffects;

TreeClass tree_effects_class_from_kindred(unsigned int kindred);
int tree_effects_node_learned(unsigned int tree_points, int node);                /* node 1..12 */
int tree_effects_tree_copies(const TreeEffectsInput *input, Corruptor corruptor); /* learned, corrupted nodes */
int tree_effects_node_in_math(TreeClass tree_class, int node); /* 1 when the uncorrupted node is in the table */
int tree_effects_corruptor_for_server_number(int server_number); /* Corruptor, or -1 */
void tree_effects_compute(const TreeEffectsInput *input, PlannerEffects *effects);

typedef enum {
    SKILL_GROUP_PASSIVE = 1,
    SKILL_GROUP_ACTIVE_SPELL = 2,
    SKILL_GROUP_ACTIVE_MELEE = 4,
} SkillGroup;

int tree_effects_skill_groups(int skill_number); /* bitmask; 31 is both passive and active melee, as on the server */

#ifdef __cplusplus
}
#endif
