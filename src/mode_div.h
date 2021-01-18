#pragma once

// arc mode values saved to nvram
typedef struct {
  uint32_t clock_period;
} div_state_t;

void enter_mode_div(void);
void leave_mode_div(void);

void keytimer_div(void);

void default_div(void);
void init_div(void);
void resume_div(void);
void clock_div(uint8_t phase);
void ii_div(uint8_t *d, uint8_t l);
