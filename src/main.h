#pragma once

#include "timers.h"

#include "mode_arc.h"
#include "mode_div.h"
#include "mode_grid.h"
#include "mode_midi.h"

#define TR1 B02
#define TR2 B03
#define TR3 B04
#define TR4 B05

#define KEY_HOLD_TIME 8

#define MS_TICKS(ms) ((ms) << 2)

////////////////////////////////////////////////////////////////////////////////
// types

typedef enum { conNONE, conARC, conGRID, conMIDI, conFLASH } connected_t;

typedef enum {
  mGrid,
  mArc,
  mMidi,
  mDiv,
} transit_mode_t;

// NVRAM data structure located in the flash array.
typedef const struct {
  u8 fresh;
  connected_t connected;
  transit_mode_t mode;
  grid_state_t grid_state;
  arc_state_t arc_state;
  midi_state_t midi_state;
  div_state_t div_state;
} nvram_data_t;

////////////////////////////////////////////////////////////////////////////////
// globals

extern nvram_data_t f;
extern connected_t connected;
extern bool external_clock;
extern u16 adc[4];

////////////////////////////////////////////////////////////////////////////////
// prototypes

void (*clock)(u8 phase);

extern void handler_None(s32 data);
extern void handler_PollADC(s32 data);

extern void clock_null(u8 phase);

void set_mode(transit_mode_t m);
void set_tr(uint8_t n);
void clr_tr(uint8_t n);
void clr_tr_all(void);
uint8_t get_tr(uint8_t n);
void clock_set(uint32_t n);
void clock_set_tr(uint32_t n, uint8_t phase);
