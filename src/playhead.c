//
// Copyright (c) 2022 Greg Wuller.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

// libavr32
#include <util.h>

#include "playhead.h"
#include "track.h"

void playhead_init(playhead_t *p) {
  p->position = 0;
  p->first = 0;
  p->max = PATTERN_STEP_MAX;
  p->delta = 1;
  p->nudge = 0;
  p->should_reset = false;
}

u8 playhead_position(playhead_t *p) {
  return p->position;
}

u8 playhead_advance(playhead_t *p) {
  return playhead_move(p, p->delta);
}

u8 playhead_move(playhead_t *p, s8 delta) {
  if (p->nudge) {
    p->position = (p->position + p->nudge) % p->max;
    p->nudge = 0;
  }

  if (p->should_reset) {
    p->position = p->first;
    p->should_reset = false;
  } else {
    // FIXME: make this work with jumps larger that TRACK_STEP_MAX (i.e. wrap
    // multiple times)
    s32 next = p->position + sclip(delta, -PATTERN_STEP_MAX, PATTERN_STEP_MAX);
    if (next >= p->max) {
      p->position = p->first + (next - p->max);
    } else if (next < p->first) {
      p->position = p->max - (p->first - next);
    } else {
      p->position = next;
    }
  }
  return p->position;
}
