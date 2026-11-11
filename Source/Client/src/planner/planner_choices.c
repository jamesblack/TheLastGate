#include "planner_choices.h"

#include <stdint.h>
#include <stdio.h>

#include "planner/combat_model.h"

/* File layout, all 32-bit little-endian words: format version, record count, then per record the character number,
 * weapon and off-hand. */
#define PLANNER_CHOICES_FORMAT 1
#define PLANNER_CHOICES_MAX_RECORDS 256
#define CARD_WORD_BITS 32U
#define CARD_WORD_MASK 0xFFFFFFFFU

typedef struct {
    uint32_t character_number;
    int32_t weapon;
    int32_t offhand;
} ChoiceRecord;

/* Reads every record; returns how many were read, or 0 when the file is missing or damaged. */
static int read_records(const char *path, ChoiceRecord records[PLANNER_CHOICES_MAX_RECORDS]) {
    FILE *file = fopen(path, "rb");
    uint32_t header[2];
    int record_count = 0;

    if (file == NULL) return 0;
    if (fread(header, sizeof(header), 1, file) == 1 && header[0] == PLANNER_CHOICES_FORMAT &&
        header[1] <= PLANNER_CHOICES_MAX_RECORDS) {
        record_count = (int) header[1];
        if (record_count > 0 &&
            fread(records, sizeof(ChoiceRecord), (size_t) record_count, file) != (size_t) record_count) {
            record_count = 0;
        }
    }
    fclose(file);
    return record_count;
}

static int write_records(const char *path, const ChoiceRecord *records, const int record_count) {
    FILE *file = fopen(path, "wb");
    uint32_t header[2];
    int written;

    if (file == NULL) return 0;
    header[0] = PLANNER_CHOICES_FORMAT;
    header[1] = (uint32_t) record_count;
    written = fwrite(header, sizeof(header), 1, file) == 1 &&
              fwrite(records, sizeof(ChoiceRecord), (size_t) record_count, file) == (size_t) record_count;
    return fclose(file) == 0 && written;
}

static int checked_choice(const int32_t value, const int choice_count) {
    return value >= 0 && value < choice_count ? (int) value : PLANNER_CHOICE_NONE;
}

PlannerChoices planner_choices_load(const char *path, const unsigned int character_number) {
    static ChoiceRecord records[PLANNER_CHOICES_MAX_RECORDS];
    PlannerChoices choices = {PLANNER_CHOICE_NONE, PLANNER_CHOICE_NONE};
    int record_count;
    int record_index;

    if (character_number == 0) return choices;
    record_count = read_records(path, records);
    for (record_index = 0; record_index < record_count; record_index++) {
        if (records[record_index].character_number != character_number) continue;
        choices.weapon = checked_choice(records[record_index].weapon, COMBAT_WEAPON_COUNT);
        choices.offhand = checked_choice(records[record_index].offhand, COMBAT_OFFHAND_COUNT);
        break;
    }
    return choices;
}

int planner_choices_save(const char *path, const unsigned int character_number, const PlannerChoices choices) {
    static ChoiceRecord records[PLANNER_CHOICES_MAX_RECORDS];
    int record_count;
    int record_index;

    if (character_number == 0) return 0;
    record_count = read_records(path, records);
    for (record_index = 0; record_index < record_count; record_index++) {
        if (records[record_index].character_number == character_number) break;
    }
    if (record_index == record_count) {
        /* A new character; when the file is full the oldest record makes room. */
        if (record_count == PLANNER_CHOICES_MAX_RECORDS) {
            for (record_index = 1; record_index < record_count; record_index++) {
                records[record_index - 1] = records[record_index];
            }
            record_index = record_count - 1;
        } else {
            record_count++;
        }
    }
    records[record_index].character_number = character_number;
    records[record_index].weapon = choices.weapon;
    records[record_index].offhand = choices.offhand;
    return write_records(path, records, record_count);
}
