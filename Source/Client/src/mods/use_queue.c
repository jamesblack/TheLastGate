#include "use_queue.h"

#include <stdbool.h>

#include "engine.h"
#include "inter.h"
#include "stubborn_actions.h"

static QueuedCommands queued_commands = {0};

static bool is_current_action(const int x, const int y) {
    return pl.misc_action == 4 && pl.misc_target1 == x && pl.misc_target2 == y;
}

// Appends to the ring buffer; false when the entry was skipped or the queue is full.
static bool enqueue_command(const int cmd, const int x, const int y) {
    if (is_current_action(x, y)) return false;
    if (queued_commands.count >= USE_QUEUE_CAPACITY) return false;
    if (has_cmd_been_queued(cmd, x, y)) return false;

    queued_commands.commands[queued_commands.tail] = (QueuedCommand){
        .cmd = cmd,
        .x = x,
        .y = y,
    };

    queued_commands.tail = (queued_commands.tail + 1) % USE_QUEUE_CAPACITY;
    queued_commands.count++;
    return true;
}

static void start_queue_if_idle(void) {
    if (pl.misc_action == 0 && queued_commands.count > 0) pop_cmd_from_queue();
}

void add_cmd_to_queue(const int cmd, const int x, const int y) {
    if (is_current_action(x, y)) return;
    if (queued_commands.count >= USE_QUEUE_CAPACITY) {
        xlog(0, "Your queue is full.");
        return;
    }

    const int count_before = queued_commands.count;
    if (!enqueue_command(cmd, x, y)) return;

    play_sound("sfx/click.wav",CLICKVOL, 0);
    if (count_before == 0) start_queue_if_idle();
}

int add_cmds_to_queue(const QueuedCommand *commands, const int command_count) {
    const int count_before = queued_commands.count;
    int added_count = 0;

    for (int command_index = 0; command_index < command_count; command_index++) {
        if (queued_commands.count >= USE_QUEUE_CAPACITY) {
            xlog(0, "Your queue is full.");
            break;
        }
        const QueuedCommand *command = &commands[command_index];
        if (enqueue_command(command->cmd, command->x, command->y)) added_count++;
    }

    if (added_count == 0) return 0;

    play_sound("sfx/click.wav",CLICKVOL, 0);
    if (count_before == 0) start_queue_if_idle();
    return added_count;
}

void pop_cmd_from_queue() {
    if (queued_commands.count == 0) return;
    const QueuedCommand command = queued_commands.commands[queued_commands.head];
    mod_stubborn_actions_set_use_cmd(command.x, command.y);
    cmdq(command.cmd, command.x, command.y);
}

int has_cmd_been_queued(const int cmd, const int x, const int y) {
    int position = 0;
    int index = queued_commands.head;
    int remaining = queued_commands.count;

    while (remaining > 0) {
        const QueuedCommand value = queued_commands.commands[index];
        if (value.cmd == cmd && value.x == x && value.y == y) return position + 1;
        index = (index + 1) % USE_QUEUE_CAPACITY;
        remaining--;
        position++;
    }
    return 0;
}

void clear_cmd_queue() {
    queued_commands.head = 0;
    queued_commands.tail = 0;
    queued_commands.count = 0;
}

int cmd_queue_size() {
    return queued_commands.count;
}

void cmd_queue_on_target(int x, int y) {
    if (queued_commands.count == 0) return;
    const QueuedCommand current_command = queued_commands.commands[queued_commands.head];
    if (current_command.x != x || current_command.y != y) return;

    queued_commands.head = (queued_commands.head + 1) % USE_QUEUE_CAPACITY;
    queued_commands.count--;
}
