#pragma once

// midi mode values saved to nvram
typedef struct {
	uint32_t clock_period;
} midi_state_t;

void enter_mode_midi(void);
void leave_mode_midi(void);

void keytimer_midi(void);

void default_midi(void);
void init_midi(void);
void resume_midi(void);
void clock_midi(uint8_t phase);
void ii_midi(uint8_t *d, uint8_t l);
