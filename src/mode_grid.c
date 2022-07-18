//
// Copyright (c) 2016-2022 Greg Wuller.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

// asf
#include "gpio.h"
#include "print_funcs.h"
#include "string.h"

// libavr32
#include "events.h"
#include "flashc.h"
#include "i2c.h"
#include "monome.h"
#include "phasor.h"

// this
#include "main.h"
#include "mode_common.h"
#include "mode_grid.h"
#include "playhead.h"
#include "track.h"

#define GRID_WAVE_EDGES 8
#define GRID_NUM_OUTPUTS 8

#define TRACK1_DEFAULT_PATTERN 0
#define TRACK2_DEFAULT_PATTERN 12

//------------------------------
//------ types

typedef enum { uiEdit, uiLength, uiPattern } ui_mode_t;

typedef struct {
  u8 edges[GRID_WAVE_EDGES];
  u8 cursor;
} waveform_t;

//------------------------------
//------ prototypes

static void write_grid(void);
static void read_grid(void);

static void handler_GridFrontShort(s32 data);
static void handler_GridFrontLong(s32 data);
static void handler_GridTr(s32 data);
static void handler_GridTrNormal(s32 data);

static void handler_GridKey(s32 data);
static void handler_GridRefresh(s32 data);

static void render_grid(void);
static void render_nav(void);
static void render_nudge(u8 x, u8 y);
static void render_cue_mode(u8 x, u8 y, cue_mode_t mode);
static void render_pattern_bar(u8 x, u8 y);
static void render_meta_bar(u8 x, u8 y);
static void render_meta_buffer_bar(u8 x, u8 y);

static void process_phasor(u8 now, bool reset);
static void build_waves(void);

static void handle_key_upper_step(u8 x, u8 y, u8 z);
static void handle_key_upper_len(u8 x, u8 y, u8 z);
static void handle_key_upper_pat(u8 x, u8 y, u8 z);
static bool handle_key_control(u8 x, u8 y, u8 z);

static void do_step_key(track_view_t *v, u8 x, u8 y, u8 z);
static void do_len_key(track_view_t *v, u8 x, u8 y, u8 z);
static void do_step_selection(u8 state);
static void do_row_selection(u8 state);

//-----------------------------
//----- globals

// runtime state
static ui_mode_t ui_mode;
static track_view_t view[GRID_NUM_TRACKS];
static playhead_t playhead[GRID_NUM_TRACKS];

static u8 step_selection = 0;
static u8 row_selection = 0;
static u16 clock_hz;
static waveform_t waves[GRID_NUM_OUTPUTS];

// copy of nvram state for editing
static global_t g;
static preset_t p;

void enter_mode_grid(void) {
  print_dbg("\r\n> mode grid");
  read_grid();

  app_event_handlers[kEventTr] = &handler_GridTr;
  app_event_handlers[kEventTrNormal] = &handler_GridTrNormal;
  app_event_handlers[kEventMonomeGridKey] = &handler_GridKey;
  app_event_handlers[kEventMonomeRefresh] = &handler_GridRefresh;

  ui_mode = uiEdit;

  resume_grid();

  clock = &clock_null;

  process_ii = &ii_grid;

  if (connected == conGRID) {
    app_event_handlers[kEventFrontShort] = &handler_GridFrontShort;
    app_event_handlers[kEventFrontLong] = &handler_GridFrontLong;
  }

  clock_hz = calc_clock_frequency(g.clock_rate);
  print_dbg("\r\n clock_hz = ");
  print_dbg_ulong(clock_hz);
  phasor_set_callback(&process_phasor);
  phasor_setup(clock_hz, PPQ);
  phasor_start();
}

void leave_mode_grid(void) {
  print_dbg("\r\n leave mode grid");
  phasor_stop();
  phasor_set_callback(NULL);
}

////////////////////////////////////////////////////////////////////////////////
///// handlers

void handler_GridFrontShort(s32 data) {
  print_dbg("\r\n grid: front short ");
  print_dbg_ulong(data);
}

void handler_GridFrontLong(s32 data) {
  print_dbg("\r\n grid: front long ");
  print_dbg_ulong(data);
}

void handler_GridTr(s32 data) {
  print_dbg("\r\n grid: tr ");
  print_dbg_ulong(data);
}

void handler_GridTrNormal(s32 data) {
  print_dbg("\r\n grid: tr normal ");
  print_dbg_ulong(data);
}

////////////////////////////////////////////////////////////////////////////////
///// mode

void keytimer_grid(void) {
}

void default_grid(void) {
  print_dbg("\r\ndefault_grid()");
  print_dbg("\r\n defaulting globals");
  flashc_memset16((void *)&(f.grid_state.g.clock_rate), 640, 2, true);
  flashc_memset8((void *)&(f.grid_state.g.preset), 0, 1, true);

  // use the working preset to create the default preset
  print_dbg("\r\n defaulting presets");
  track_init(&p.track[0], TRACK1_DEFAULT_PATTERN);
  track_init(&p.track[1], TRACK2_DEFAULT_PATTERN);
  for (u8 i = 0; i < GRID_NUM_PATTERNS; i++) {
    pattern_init(&p.pattern[i]);
  }
  for (u8 i = 0; i < GRID_NUM_META; i++) {
    meta_init(&p.meta[i]);
  }
  p.clock_rate = 640;

  // copy the default preset to each slot
  for (u8 i = 0; i < GRID_NUM_PRESETS; i++) {
    flashc_memcpy((void *)&f.grid_state.p[i], &p, sizeof(p), true);
    print_dbg(" ...");
    print_dbg_ulong(i);
  }
}

void write_grid(void) {
  print_dbg("\r\nwrite_grid()");
  flashc_memset16((void *)&(f.grid_state.g.clock_rate), g.clock_rate, 2, true);
  flashc_memset8((void *)&(f.grid_state.g.preset), g.preset, 1, true);
  flashc_memcpy((void *)&f.grid_state.p[g.preset], &p, sizeof(p), true);
}

void read_grid(void) {
  // called when entering mode
  print_dbg("\r\nread_grid()");
  g = f.grid_state.g;           // restore saved globals
  p = f.grid_state.p[g.preset]; // restore selected preset
}

void init_grid(void) {
  // called on startup after flash has been initialized
  print_dbg("\r\ninit_grid()");
  g = f.grid_state.g;           // restore saved globals
  p = f.grid_state.p[g.preset]; // restore selected preset
}

void resume_grid(void) {
  // called when entering mode after read_grid
  print_dbg("\r\nresume_grid()");
  // clear all because waveform transition logic is based tr state
  clr_tr_all();

  playhead_init(&playhead[0]);
  playhead_init(&playhead[1]);
  track_view_init(&view[0], &p.track[0], &playhead[0], p.pattern);
  track_view_init(&view[1], &p.track[1], &playhead[1], p.pattern);

  monomeFrameDirty++;
}

void ii_grid(uint8_t *d, uint8_t l) {
}

void handler_GridKey(s32 data) {
  u8 x, y, z;
  monome_grid_key_parse_event_data(data, &x, &y, &z);

  // print_dbg("\r\n key x: ");
  // print_dbg_ulong(x);
  // print_dbg(" y: ");
  // print_dbg_ulong(y);
  // print_dbg(" z: ");
  // print_dbg_ulong(z);

  if (y >= 6) {
    handle_key_control(x, y, z);
  } else {
    switch (ui_mode) {
    case uiEdit:
      handle_key_upper_step(x, y, z);
      break;

    case uiLength:
      handle_key_upper_len(x, y, z);
      break;

    case uiPattern:
      handle_key_upper_pat(x, y, z);
      break;
    }
  }
}

static void handle_key_upper_step(u8 x, u8 y, u8 z) {
  if (y <= 2) {
    // first track
    do_step_key(&view[0], x, y, z);
  } else if (y <= 5) {
    // second track
    do_step_key(&view[1], x, y - 3, z);
  }
}

static void handle_key_upper_len(u8 x, u8 y, u8 z) {
  if (y <= 2) {
    // first track
    do_len_key(&view[0], x, y, z);
  } else if (y <= 5) {
    // second track
    do_len_key(&view[1], x, y - 3, z);
  }
}

static void handle_key_upper_pat(u8 x, u8 y, u8 z) {
  print_dbg("\r\n pattern key");
}

static bool handle_key_control(u8 x, u8 y, u8 z) {
  // selection / meta controls
  if (x == 0) {
    if (y == 6) {
      do_row_selection(z);
      return true;
    } else if (y == 7) {
      do_step_selection(z);
      return true;
    }
  }

  // navigation
  if (row_selection) {
    if (y == 6) {
      if (x == 1) {
        // top left nav
        ui_mode = z == 1 ? uiLength : uiEdit;
        return true;
      } else if (x == 2) {
        // top right nav
        ui_mode = z == 1 ? uiPattern : uiEdit;
        return true;
      }
    } else if (y == 7) {
      if (x == 1) {
        // bottom left nav
        return true;
      } else if (x == 2) {
        // bottom right nav
        return true;
      }
    }
  }

  if (z == 1) {
    if (x == 1) {
      // left half of page select
      if (y == 6) {
        view[0].page = view[1].page = 0;
        return true;
      } else if (y == 7) {
        view[0].page = view[1].page = 2;
        return true;
      }
    } else if (x == 2) {
      // right half of page select
      if (y == 6) {
        view[0].page = view[1].page = 1;
        return true;
      } else if (y == 7) {
        view[0].page = view[1].page = 3;
        return true;
      }
    } else if (x == 3) {
      // playhead nudge back
      if (y == 6) {
        view[0].playhead->nudge = -1;
        return true;
      } else if (y == 7) {
        view[1].playhead->nudge = -1;
        return true;
      }
    } else if (x == 4) {
      // playhead reset
      if (row_selection && step_selection && (y == 6 || y == 7)) {
        print_dbg("\r\n reset both");
        view[0].playhead->should_reset = view[1].playhead->should_reset = true;
        return true;
      } else if (row_selection && y == 6) {
        // reset top track
        print_dbg("\r\n reset top");
        view[0].playhead->should_reset = true;
        return true;
      } else if (step_selection && y == 7) {
        // reset bottom track
        print_dbg("\r\n reset bottom");
        view[1].playhead->should_reset = true;
        return true;
      }
    } else if (x == 5) {
      // playhead nudge forward
      if (y == 6) {
        view[0].playhead->nudge = 1;
        return true;
      } else if (y == 7) {
        view[1].playhead->nudge = 1;
        return true;
      }
    }
  }
  return false;
}

void handler_GridRefresh(s32 data) {
  if (monomeFrameDirty) {
    memset(monomeLedBuffer, 0, MONOME_MAX_LED_BYTES);
    render_grid();
    monome_set_quadrant_flag(0);
    monome_set_quadrant_flag(1);
    (*monome_refresh)();
  }
}

//
// app
//

static void render_grid(void) {
  switch (ui_mode) {
  case uiEdit:
    track_view_steps(&view[0], 0, /* show_playhead */ true);
    track_view_steps(&view[1], 3, /* show_playhead */ true);
    render_nav();
    render_nudge(3, 6);
    render_nudge(3, 7);
    break;

  case uiLength:
    track_view_length(&view[0], 0);
    track_view_length(&view[1], 3);
    render_nav();
    break;

  case uiPattern:
    render_cue_mode(0, 0, cueNone);
    render_pattern_bar(5, 0);
    render_meta_bar(5, 1);
    render_meta_buffer_bar(5, 2);

    render_cue_mode(0, 3, cueNone);
    render_pattern_bar(5, 3);
    render_meta_bar(5, 4);
    render_meta_buffer_bar(5, 5);

    render_nav();

  default:
    break;
  }
}

static void render_nav(void) {
  u8 curr_page = view[0].page; // NOTE: assumes both track
  monomeLedBuffer[monome_xy_idx(1, 6)] = 0 == curr_page ? L2 : L1;
  monomeLedBuffer[monome_xy_idx(2, 6)] = 1 == curr_page ? L2 : L1;
  monomeLedBuffer[monome_xy_idx(1, 7)] = 2 == curr_page ? L2 : L1;
  monomeLedBuffer[monome_xy_idx(2, 7)] = 3 == curr_page ? L2 : L1;
}

static void render_nudge(u8 x, u8 y) {
  u8 offset = monome_xy_idx(x, y);
  monomeLedBuffer[offset] = L1;
  monomeLedBuffer[offset + 1] = L2;
  monomeLedBuffer[offset + 2] = L1;
}

static void render_cue_mode(u8 x, u8 y, cue_mode_t mode) {
  u8 offset = monome_xy_idx(x, y);
  monomeLedBuffer[offset] = mode == cueNone ? L2 : L1;
  monomeLedBuffer[offset + 1] = mode == cueNone ? L3 : L2;
  monomeLedBuffer[offset + 2] = mode == cueNone ? L4 : L3;
}

static void render_pattern_bar(u8 x, u8 y) {
}

static void render_meta_bar(u8 x, u8 y) {
}

static void render_meta_buffer_bar(u8 x, u8 y) {
}

static void build_waves(void) {
  memset(&waves, 0, sizeof(waves));

  u8 wn = 0;
  for (u8 tn = 0; tn < 2; tn++) {
    u8 sn = playhead_position(&playhead[tn]);
    for (u8 v = 0; v < VOICE_COUNT; v++) {
      pattern_t *pat = track_view_pattern(&view[tn]);
      trig_t t = pat->step[sn].voice[v];
      if (t.enabled && t.value) {
        // for now straight in time trigger
        waves[wn].edges[0] = MID_PHASE;
        waves[wn].edges[1] = MID_PHASE + 16; // FIXME: this is assuming a specific PPQ
      }
      wn++;
    }
    // extra to skip over 4th tr
    wn++;
  }
}

static void process_phasor(u8 now, bool reset) {
  switch (now) {
  case MIN_PHASE:
    playhead_advance(&playhead[0]);
    playhead_advance(&playhead[1]);
    // calculate waveform; this could be too expensive
    build_waves();
    break;

  case MID_PHASE:
    monomeFrameDirty++;
    // clock out high
    gpio_set_gpio_pin(B10);
    break;

  case MAX_PHASE:
    // clock out low
    gpio_clr_gpio_pin(B10);
    break;
  }

  for (u8 wn = 0; wn < 8; wn++) {
    u8 edge = waves[wn].edges[waves[wn].cursor];
    if (edge != 0 && edge == now) {
      // at an edge, toggle tr
      get_tr(wn) ? clr_tr(wn) : set_tr(wn);
      waves[wn].cursor++;
    }
  }
}

inline static void do_step_selection(u8 state) {
  step_selection = state;
}

inline static void do_row_selection(u8 state) {
  row_selection = state;
}

static void do_step_key(track_view_t *v, u8 x, u8 y, u8 z) {
  if (z == 1) {
    u8 n = v->page * PAGE_SIZE + x;
    pattern_t *pat = track_view_pattern(v);
    step_t *s = &pat->step[n];

    if (step_selection) {
      step_toggle_select(s, y);
    } else {
      step_toggle(s, y);
    }
  }
}

static void do_len_key(track_view_t *v, u8 x, u8 y, u8 z) {
  pattern_t *pat = track_view_pattern(v);
  u8 curr_length = pat->length;

  if (z == 1) {
    if (y == 0) {
      // pages
      if (x < 4) {
        v->playhead->max = pat->length = (x + 1) * PAGE_SIZE;
        // print_dbg("\r\nlen: ");
        // print_dbg_ulong(v->track->length);
      }
    } else if (y == 1) {
      // steps in page
      // print_dbg("\r\n >> curr_length: ");
      // print_dbg_ulong(curr_length);
      u8 base = 0;
      if (curr_length > PAGE_SIZE) {
        u8 rem = curr_length % PAGE_SIZE;
        base = curr_length - (rem == 0 ? PAGE_SIZE : rem);
      }
      // print_dbg(", base: ");
      // print_dbg_ulong(base);
      if (x < 15) {
        v->playhead->max = pat->length = min(base + x + 1, PATTERN_STEP_MAX);
      }
      print_dbg("\r\n len: ");
      print_dbg_ulong(pat->length);
    }
  }
}
