#pragma once

// arc mode values saved to nvram
typedef struct {
  u16 clock_rate;
} arc_state_t;

void enter_mode_arc(void);
void leave_mode_arc(void);

void keytimer_arc(void);

void default_arc(void);
void init_arc(void);
void resume_arc(void);
void clock_arc(uint8_t phase);
void ii_arc(uint8_t *d, uint8_t l);
