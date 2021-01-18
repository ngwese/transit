#pragma once

// grid mode values saved to nvram
typedef struct {
  uint32_t clock_period;
} grid_state_t;

void enter_mode_grid(void);
void leave_mode_grid(void);

void keytimer_grid(void);

void default_grid(void);
void init_grid(void);
void resume_grid(void);
void clock_grid(u8 phase);
void ii_grid(uint8_t *d, uint8_t l);
