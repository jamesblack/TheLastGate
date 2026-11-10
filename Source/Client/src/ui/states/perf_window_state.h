#pragma once
#include "common.h"

EXTERN_C_BEGIN

typedef struct {
    float fps_avg;
    float sample_ms_min;
    float sample_ms_max;
    int tick_queue;
    int tick_buffer_target;
    float server_tick_ms;
} PerfWindowState;

EXTERN_C_END
