//
// Copyright (c) 2022 Greg Wuller.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

// libavr32
#include "types.h"
#include "compiler.h"

// this
#include <playhead.h>


#define VOICE_COUNT 3

#define TRACK_STEP_MAX 64
#define TRACK_DEFAULT_LENGTH 16

//
// step
//

typedef struct {
    u8 value;
    // TODO: condition
} trig_t;

typedef struct {
    u8 selected : 1;
    u8 reserved : 7;
    trig_t voice[VOICE_COUNT];
} step_t;

void step_set(step_t* step, u8 voice, u8 value);
u8 step_get(step_t* step, u8 voice);
void step_load(step_t* step, u8 out[VOICE_COUNT]);

//
// track
//

typedef struct {
    step_t step[TRACK_STEP_MAX];
    u8 length;
} track_t;

void track_init(track_t* t);
void track_copy(track_t* dst, track_t* src);

//
// track view
//

#define PAGE_SIZE 16

typedef struct {
    u8 page;
    track_t* track;
    playhead_t* playhead;
} track_view_t;

void track_view_init(track_view_t* v, track_t* t, playhead_t* p);
void track_view_render(track_view_t* v, u8 top_row, bool show_playhead);

