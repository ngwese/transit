//
// Copyright (c) 2022 Greg Wuller.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

// asf
#include "string.h"

// libavr32
#include "monome.h"

// this
#include "mode_common.h"
#include "track.h"

//
// step
//

void step_set(step_t *step, u8 voice, u8 value) {
  step->voice[voice].value = value;
}

void step_toggle(step_t *step, u8 voice) {
  if (step->voice[voice].value == 0) {
    // not set, enable and default value
    step->voice[voice].enabled = 1;
    step->voice[voice].value = 1;
  } else {
    // disable but retain the value
    step->voice[voice].enabled = !step->voice[voice].enabled;
  }
}

void step_set_select(step_t *step, u8 voice, bool selected) {
  step->voice[voice].selected = selected;
}

void step_toggle_select(step_t *step, u8 voice) {
  step->voice[voice].selected = !step->voice[voice].selected;
}

u8 step_get(step_t *step, u8 voice) {
  return step->voice[voice].value;
}

void step_load(step_t *step, u8 out[VOICE_COUNT]) {
  for (u8 i = 0; i < VOICE_COUNT; i++) {
    out[i] = step->voice[i].value;
  }
}

//
// track
//

void track_init(track_t *t) {
  memset(t, 0, sizeof(track_t));
  t->length = TRACK_DEFAULT_LENGTH;
}

void track_copy(track_t *dst, track_t *src) {
  memcpy(dst, src, sizeof(track_t));
}

//
// track view
//

void track_view_init(track_view_t *v, track_t *t, playhead_t *p) {
  v->page = 0;
  v->track = t;
  v->playhead = p;

  // FIXME: hack
  p->max = t->length;
}

void track_view_render(track_view_t *v, u8 top_row, bool show_playhead) {
  track_t *t = v->track;
  u8 view_start = v->page * PAGE_SIZE;
  u8 view_max = min(t->length - view_start, PAGE_SIZE);
  u8 top_offset = top_row * GRID_WIDTH;

  // steps
  if (view_max > 0) {
    for (u8 i = 0; i < view_max; i++) {
      step_t *s = &(t->step[view_start + i]);
      for (u8 v = 0; v < VOICE_COUNT; v++) {
        if (s->voice[v].enabled && s->voice[v].value) {
          monomeLedBuffer[top_offset + (v * GRID_WIDTH) + i] = L3;
        }
      }
    }
    monomeFrameDirty++;
  }

  // playhead
  if (show_playhead) {
    u8 l = playhead_position(v->playhead);
    if (l >= view_start && l < view_start + PAGE_SIZE) {
      l -= view_start;
      for (u8 v = 0; v < VOICE_COUNT; v++) {
        monomeLedBuffer[top_offset + (v * GRID_WIDTH) + l] += L2;
      }
      monomeFrameDirty++;
    }
  }
}