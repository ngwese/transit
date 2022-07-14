//
// Copyright (c) 2022 Greg Wuller.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include "types.h"

// brightness constants
#define L1 2
#define L2 4
#define L3 10
#define L4 15

#define PPQ 64
#define MAX_PHASE (PPQ - 1) // phasor range [0-63] (PPQ-1)
#define MAX_WIDTH (PPQ - 1)
#define MAX_PERIOD PPQ

#define MAX_DIVISOR 16

#define MAX_PHASOR_HZ 1280

#define GRID_WIDTH 16
#define GRID_HEIGHT 8

u16 calc_clock_frequency(u16 rate);


