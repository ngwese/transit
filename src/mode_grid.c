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

//------------------------------
//------ types

typedef enum { uiEdit, uiPlay, uiConfig, uiLength } ui_mode_t;

typedef struct {
  u8 edges[8];
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
static void render_page(void);
static void render_nudge(u8 x, u8 y);
static void render_controls(void);

static void render_waves(void);
static void process_phasor(u8 now, bool reset);

static void do_step_key(track_view_t *v, u8 x, u8 y, u8 z);
static void do_len_key(track_view_t *v, u8 x, u8 y, u8 z);
static void do_step_selection(u8 state);
static void do_row_selection(u8 state);

//-----------------------------
//----- globals

static ui_mode_t ui_mode;
static track_t track[2];
static track_view_t view[2];
static playhead_t playhead[2];
static u16 clock_hz;

static u8 step_selection = 0;
static u8 row_selection = 0;

static waveform_t waves[8];

// copy of nvram state for editing
static grid_state_t grid_state;

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

  clock_hz = calc_clock_frequency(grid_state.clock_rate);
  print_dbg("\r\n clock_hz = ");
  print_dbg_ulong(clock_hz);
  phasor_set_callback(&process_phasor);
  phasor_setup(clock_hz, PPQ);
  phasor_start();
}

void leave_mode_grid(void) {
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
  flashc_memset16((void *)&(f.grid_state.clock_rate), 640, 2, true);
  flashc_memset16((void *)&(f.grid_state.preset), 0, 1, true);
}

void write_grid(void) {
  print_dbg("\r\nwrite_grid()");
  flashc_memset16((void *)&(f.grid_state.clock_rate), grid_state.clock_rate, 2, true);
  flashc_memset16((void *)&(f.grid_state.preset), grid_state.preset, 1, true);
}

void read_grid(void) {
  print_dbg("\r\nread_grid()");
  grid_state = f.grid_state;
}

void init_grid(void) {
  print_dbg("\r\ninint_grid()");
  track_init(&track[0]);
  track_init(&track[1]);
  playhead_init(&playhead[0]);
  playhead_init(&playhead[1]);
  track_view_init(&view[0], &track[0], &playhead[0]);
  track_view_init(&view[1], &track[1], &playhead[1]);
}

void resume_grid(void) {
  print_dbg("\r\nresume_grid()");
  // clear all because waveform transition logic is based tr state
  clr_tr_all();
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

  if (y <= 2) {
    // first track
    switch (ui_mode) {
    case uiEdit:
      do_step_key(&view[0], x, y, z);
      break;

    case uiLength:
      do_len_key(&view[0], x, y, z);
      break;

    default:
      break;
    }
  } else if (y <= 5) {
    // second track
    switch (ui_mode) {
    case uiEdit:
      do_step_key(&view[1], x, y - 3, z);
      break;

    case uiLength:
      do_len_key(&view[1], x, y - 3, z);
      break;

    default:
      break;
    }
  } else {
    // controls
    if (x == 0) {
      // select
      if (y == 6) {
        do_row_selection(z);
      } else if (y == 7) {
        do_step_selection(z);
      }
    } else if (x == 1 || x == 2) {
      // page controls
      if (row_selection || step_selection) {
        ui_mode = z == 1 ? uiLength : uiEdit;
      } else if (!row_selection && !step_selection && ui_mode == uiLength) {
        // jump back to edit if select buttons are released
        ui_mode = uiEdit;
      } else {
        if (z == 1) {
          if (x == 1) {
            if (y == 6) {
              // first page
              // print_dbg("\r\n page 1");
              view[0].page = view[1].page = 0;
            } else if (y == 7) {
              // print_dbg("\r\n page 3");
              view[0].page = view[1].page = 2;
            }
          } else if (x == 2) {
            if (y == 6) {
              // print_dbg("\r\n page 2");
              view[0].page = view[1].page = 1;
            } else if (y == 7) {
              // print_dbg("\r\n page 4");
              view[0].page = view[1].page = 3;
            }
          }
        }
      }
    } else if (x == 3) {
      // playhead nudge back
      if (z == 1) {
        if (y == 6) {
          view[0].playhead->nudge = -1;
        } else if (y == 7) {
          view[1].playhead->nudge = -1;
        }
      }
    } else if (x == 4) {
      // playhead reset
      if (z == 1) {
        if (row_selection && step_selection && (y == 6 || y == 7)) {
          print_dbg("\r\n reset both");
          view[0].playhead->should_reset = view[1].playhead->should_reset = true;
        } else if (row_selection && y == 6) {
          // reset top track
          print_dbg("\r\n reset top");
          view[0].playhead->should_reset = true;
        } else if (step_selection && y == 7) {
          // reset bottom track
          print_dbg("\r\n reset bottom");
          view[1].playhead->should_reset = true;
        }
      }
    } else if (x == 5) {
      // playhead nudge forward
      if (z == 1) {
        if (y == 6) {
          view[0].playhead->nudge = 1;
        } else if (y == 7) {
          view[1].playhead->nudge = 1;
        }
      }
    }
  }
}

void handler_GridRefresh(s32 data) {
  // print_dbg("\r\nhandler_GridRefresh");
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
    render_controls();
    break;

  case uiLength:
    track_view_length(&view[0], 0);
    track_view_length(&view[1], 3);
    render_controls();
    break;

  case uiPlay:
    break;

  case uiConfig:
    break;

  default:
    break;
  }
}

static void render_page(void) {
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

static void render_controls(void) {
  render_page();
  render_nudge(3, 6);
  render_nudge(3, 7);
}

static void render_waves(void) {
  memset(&waves, 0, sizeof(waves));

  u8 wn = 0;
  for (u8 tn = 0; tn < 2; tn++) {
    u8 sn = playhead_position(&playhead[tn]);
    for (u8 v = 0; v < VOICE_COUNT; v++) {
      trig_t t = track[tn].step[sn].voice[v];
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
    render_waves();
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
    step_t *s = &v->track->step[n];

    if (step_selection) {
      step_toggle_select(s, y);
    } else {
      step_toggle(s, y);
    }
  }
}

static void do_len_key(track_view_t *v, u8 x, u8 y, u8 z) {
  u8 curr_length = v->track->length;
  if (z == 1) {
    if (y == 0) {
      // pages
      if (x < 4) {
        v->playhead->max = v->track->length = (x + 1) * PAGE_SIZE;
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
        v->playhead->max = v->track->length = min(base + x + 1, TRACK_STEP_MAX);
      }
      print_dbg("\r\n len: ");
      print_dbg_ulong(v->track->length);
    }
  }
}
