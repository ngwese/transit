//
// Copyright (c) 2016-2022 Greg Wuller.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include "track.h"

// grid mode values saved to nvram
typedef struct {
  // uint32_t clock_period;
  u16 clock_rate;
} grid_state_t;

void enter_mode_grid(void);
void leave_mode_grid(void);

void keytimer_grid(void);

void default_grid(void);
void init_grid(void);
void resume_grid(void);
void clock_grid(u8 phase);
void ii_grid(uint8_t *d, uint8_t l);
