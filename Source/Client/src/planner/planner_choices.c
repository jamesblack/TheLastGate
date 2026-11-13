#include "planner_choices.h"

#include <stdint.h>
#include <stdio.h>

#include "planner/combat_model.h"
#include "planner/tree_effects.h"

/* File layout, all 32-bit little-endian words: format version, record count, then the records. Format 1 records hold
 * the character number, weapon and off-hand; format 2 adds the spell toggles and the tarot cards as two words; format 3
 * adds the corruptor tick bits as one word and one count byte per corruptor, padded to 28 bytes. */
#define PLANNER_CHOICES_FORMAT_FIRST 1
#define PLANNER_CHOICES_FORMAT_SECOND 2
#define PLANNER_CHOICES_FORMAT 3
#define PLANNER_CHOICES_MAX_RECORDS 256
#define CARD_WORD_BITS 32U
#define CARD_WORD_MASK 0xFFFFFFFFU
#define CORRUPTOR_COUNT_SLOTS 28
#define CORRUPTOR_COPIES_MAX 20

_Static_assert(CORRUPTOR_COUNT <= CORRUPTOR_COUNT_SLOTS, "the choices file has no room for every corruptor");

typedef struct {
    uint32_t character_number;
    int32_t weapon;
    int32_t offhand;
} FirstFormatRecord;

typedef struct {
    uint32_t character_number;
    int32_t weapon;
    int32_t offhand;
    uint32_t spell_toggles;
    uint32_t cards_low;
    uint32_t cards_high;
} SecondFormatRecord;

typedef struct {
    uint32_t character_number;
    int32_t weapon;
    int32_t offhand;
    uint32_t spell_toggles;
    uint32_t cards_low;
    uint32_t cards_high;
    uint32_t corruptor_ticks;
    uint8_t corruptor_counts[CORRUPTOR_COUNT_SLOTS];
} ChoiceRecord;

static void clear_corruptors(ChoiceRecord *record) {
    int slot;

    record->corruptor_ticks = 0;
    for (slot = 0; slot < CORRUPTOR_COUNT_SLOTS; slot++) {
        record->corruptor_counts[slot] = 0;
    }
}

static int read_first_format_records(FILE *file, ChoiceRecord *records, const int record_count) {
    FirstFormatRecord old_record;
    int record_index;

    for (record_index = 0; record_index < record_count; record_index++) {
        if (fread(&old_record, sizeof(old_record), 1, file) != 1) return 0;
        records[record_index].character_number = old_record.character_number;
        records[record_index].weapon = old_record.weapon;
        records[record_index].offhand = old_record.offhand;
        records[record_index].spell_toggles = 0;
        records[record_index].cards_low = 0;
        records[record_index].cards_high = 0;
        clear_corruptors(&records[record_index]);
    }
    return record_count;
}

static int read_second_format_records(FILE *file, ChoiceRecord *records, const int record_count) {
    SecondFormatRecord old_record;
    int record_index;

    for (record_index = 0; record_index < record_count; record_index++) {
        if (fread(&old_record, sizeof(old_record), 1, file) != 1) return 0;
        records[record_index].character_number = old_record.character_number;
        records[record_index].weapon = old_record.weapon;
        records[record_index].offhand = old_record.offhand;
        records[record_index].spell_toggles = old_record.spell_toggles;
        records[record_index].cards_low = old_record.cards_low;
        records[record_index].cards_high = old_record.cards_high;
        clear_corruptors(&records[record_index]);
    }
    return record_count;
}

/* Reads every record; returns how many were read, or 0 when the file is missing or damaged. */
static int read_records(const char *path, ChoiceRecord records[PLANNER_CHOICES_MAX_RECORDS]) {
    FILE *file = fopen(path, "rb");
    uint32_t header[2];
    int record_count = 0;

    if (file == NULL) return 0;
    if (fread(header, sizeof(header), 1, file) == 1 && header[1] <= PLANNER_CHOICES_MAX_RECORDS) {
        record_count = (int) header[1];
        if (header[0] == PLANNER_CHOICES_FORMAT_FIRST) {
            record_count = read_first_format_records(file, records, record_count);
        } else if (header[0] == PLANNER_CHOICES_FORMAT_SECOND) {
            record_count = read_second_format_records(file, records, record_count);
        } else if (header[0] != PLANNER_CHOICES_FORMAT ||
                   (record_count > 0 &&
                    fread(records, sizeof(ChoiceRecord), (size_t) record_count, file) != (size_t) record_count)) {
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

static unsigned char checked_copies(const uint8_t stored, const int ticked) {
    if (stored > CORRUPTOR_COPIES_MAX) return CORRUPTOR_COPIES_MAX;
    return ticked && stored == 0 ? 1 : stored;
}

PlannerChoices planner_choices_load(const char *path, const unsigned int character_number) {
    static ChoiceRecord records[PLANNER_CHOICES_MAX_RECORDS];
    PlannerChoices choices = {PLANNER_CHOICE_NONE, PLANNER_CHOICE_NONE, 0, 0, 0, {0}};
    int record_count;
    int record_index;
    int corruptor;

    if (character_number == 0) return choices;
    record_count = read_records(path, records);
    for (record_index = 0; record_index < record_count; record_index++) {
        if (records[record_index].character_number != character_number) continue;
        choices.weapon = checked_choice(records[record_index].weapon, COMBAT_WEAPON_COUNT);
        choices.offhand = checked_choice(records[record_index].offhand, COMBAT_OFFHAND_COUNT);
        choices.spell_toggles = records[record_index].spell_toggles;
        choices.tarot_cards =
                ((TarotSet) records[record_index].cards_high << CARD_WORD_BITS) | records[record_index].cards_low;
        choices.corruptor_ticks = records[record_index].corruptor_ticks;
        for (corruptor = 0; corruptor < CORRUPTOR_COUNT; corruptor++) {
            choices.corruptor_counts[corruptor] = checked_copies(records[record_index].corruptor_counts[corruptor],
                                                                 (int) ((choices.corruptor_ticks >> corruptor) & 1U));
        }
        break;
    }
    return choices;
}

int planner_choices_save(const char *path, const unsigned int character_number, const PlannerChoices choices) {
    static ChoiceRecord records[PLANNER_CHOICES_MAX_RECORDS];
    int record_count;
    int record_index;
    int corruptor;

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
    records[record_index].spell_toggles = choices.spell_toggles;
    records[record_index].cards_low = (uint32_t) (choices.tarot_cards & CARD_WORD_MASK);
    records[record_index].cards_high = (uint32_t) (choices.tarot_cards >> CARD_WORD_BITS);
    records[record_index].corruptor_ticks = choices.corruptor_ticks;
    for (corruptor = 0; corruptor < CORRUPTOR_COUNT; corruptor++) {
        records[record_index].corruptor_counts[corruptor] = choices.corruptor_counts[corruptor];
    }
    return write_records(path, records, record_count);
}
