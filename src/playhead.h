//
// Copyright (c) 2022 Greg Wuller.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

// libavr32
#include <types.h>

typedef struct {
    u8 position;
    u8 first;
    u8 max;
    s8 delta;
} playhead_t;


void playhead_init(playhead_t* p);
u8 playhead_position(playhead_t* p);
u8 playhead_advance(playhead_t* p);
u8 playhead_move(playhead_t* p, s8 delta);
