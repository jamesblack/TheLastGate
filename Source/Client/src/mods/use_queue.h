#pragma once
#include <stdbool.h>

#define USE_QUEUE_CAPACITY 50

typedef struct {
    int cmd;
    int x;
    int y;
} QueuedCommand;

typedef struct {
    QueuedCommand commands[USE_QUEUE_CAPACITY];
    int head;
    int tail;
    int count;
} QueuedCommands;

void add_cmd_to_queue(int cmd, int x, int y);
// Queues each command in order without per-entry feedback; returns how many were added.
int add_cmds_to_queue(const QueuedCommand *commands, int command_count);
void pop_cmd_from_queue();
int has_cmd_been_queued(int cmd, int x, int y);
void clear_cmd_queue();
int cmd_queue_size();
void cmd_queue_on_target(int x, int y);
