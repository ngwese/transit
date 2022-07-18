//
// Copyright (c) 2016-2022 Greg Wuller.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include "meta.h"
#include "track.h"

#define GRID_NUM_TRACKS 2
#define GRID_NUM_PRESETS 2
#define GRID_NUM_PATTERNS 24
#define GRID_NUM_META 24

typedef struct {
  u16 clock_rate;
  track_t track[GRID_NUM_TRACKS];
  pattern_t pattern[GRID_NUM_PATTERNS];
  meta_pattern_t meta[GRID_NUM_META];
} preset_t;

typedef struct {
  u16 clock_rate; // global clock rate
  u8 preset;      // which preset is selected
} global_t;

// grid mode values saved to nvram
typedef struct {
  global_t g;
  preset_t p[GRID_NUM_PRESETS];
} grid_state_t;

void enter_mode_grid(void);
void leave_mode_grid(void);

void keytimer_grid(void);

void default_grid(void);
void init_grid(void);
void resume_grid(void);
void clock_grid(u8 phase);
void ii_grid(uint8_t *d, uint8_t l);
