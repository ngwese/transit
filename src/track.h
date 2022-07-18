//
// Copyright (c) 2022 Greg Wuller.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

// libavr32
#include "compiler.h"
#include "types.h"

// this
#include <playhead.h>

#define VOICE_COUNT 3

#define PATTERN_STEP_MAX 64
#define PATTERN_DEFAULT_LENGTH 16

typedef enum { cueNone = 0, cuePattern, cueMeta } cue_mode_t;

//
// step
//

typedef struct {
  u8 selected : 1;
  u8 enabled : 1;
  u8 reserved : 6;
  u8 value;
  // TODO: condition
} trig_t;

typedef struct {
  u8 flags;
  trig_t voice[VOICE_COUNT];
} step_t;

void step_set(step_t *step, u8 voice, u8 value);
void step_toggle(step_t *step, u8 voice);
void step_set_select(step_t *step, u8 voice, bool selected);
void step_toggle_select(step_t *step, u8 voice);
u8 step_get(step_t *step, u8 voice);
void step_load(step_t *step, u8 out[VOICE_COUNT]);

//
// pattern
//
typedef struct {
  step_t step[PATTERN_STEP_MAX];
  u8 length;
  u8 occupied : 1;
  u8 reserved : 7;
} pattern_t;

void pattern_init(pattern_t *p);
void pattern_copy(pattern_t *dst, pattern_t *src);

//
// track
//

typedef struct {
  cue_mode_t cue;
  u8 pattern;
} track_t;

void track_init(track_t *t, u8 initial_pattern);
void track_copy(track_t *dst, track_t *src);

//
// track view
//

#define PAGE_SIZE 16

typedef struct {
  u8 page;
  track_t *track;
  playhead_t *playhead;
  pattern_t *patterns;
} track_view_t;

void track_view_init(track_view_t *v, track_t *t, playhead_t *p, pattern_t *patterns);
void track_view_steps(track_view_t *v, u8 top_row, bool show_playhead);
void track_view_length(track_view_t *v, u8 top_row);
pattern_t *track_view_pattern(track_view_t *v);