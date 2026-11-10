#pragma once

void tick_pacer_reset(void);

void tick_pacer_on_arrival(unsigned int now_ms);

int tick_pacer_target(void);

double tick_pacer_period_ms(void);

int tick_pacer_next_tick_ms(int queued_ticks, int nominal_tick_ms);
