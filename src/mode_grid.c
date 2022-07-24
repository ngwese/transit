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
#include "util.h"

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

typedef union {
  struct {
    u16 set : 1;
    u16 level : 1;
    u16 offset : 14;
  } f;
  u16 v;
} edge_t;

typedef struct {
  edge_t edges[GRID_WAVE_EDGES]; // offsets in phaser ramp where level transition should occur
  u8 cursor;                     // index of next edge
} waveform_t;

typedef struct {
  u8 track;
  u8 step;
  u8 voice;
  u8 z;
  u8 hold_count;
  bool fresh_trig;
} focused_step_t;

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
static void render_track_select(u8 x, u8 y);
static void render_playhead_nudge(u8 x, u8 y);
static void render_step_nudge(u8 x, u8 y);
static void render_cue_mode(u8 x, u8 y, cue_mode_t mode);
static void render_pattern_area(u8 x, u8 y);
static void render_meta_area(u8 x, u8 y);
static void render_meta_buffer_bar(u8 x, u8 y);

static void process_phasor(u8 now, bool reset);
static void build_waves(void);

static void handle_key_upper_step(u8 x, u8 y, u8 z);
static void handle_key_upper_len(u8 x, u8 y, u8 z);
static void handle_key_upper_pat(u8 x, u8 y, u8 z);
static bool handle_key_control(u8 x, u8 y, u8 z);

static void do_step_key(u8 tn, u8 x, u8 y, u8 z);
static void do_len_key(track_view_t *v, u8 x, u8 y, u8 z);
static void do_step_selection(u8 state);
static void do_row_selection(u8 state);
static void do_focused_step_timing(s8 direction);

inline static u16 edge_pack(u8 level, u16 offset);

//-----------------------------
//----- globals

// runtime state
static ui_mode_t ui_mode;
static track_view_t view[GRID_NUM_TRACKS];
static playhead_t playhead[GRID_NUM_TRACKS];

static u8 step_selection = 0;
static u8 row_selection = 0;
static u8 track_selection[GRID_NUM_TRACKS] = {0, 0};
static focused_step_t step_focus = {0, 0, 0, 0}; // FIXME: should changing pattern/meta clear this?

static u16 clock_hz;
static waveform_t waves[GRID_NUM_OUTPUTS];
static edge_t carry[GRID_NUM_OUTPUTS]; // offset into next waveform of the trailing edge of the
                                       // previous waveform

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
  if (step_focus.hold_count > 0) {
    step_focus.hold_count--;
    // print_dbg("\r\n kt = ");
    // print_dbg_ulong(step_focus.hold_count);
  }
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
    do_step_key(0, x, y, z);
  } else if (y <= 5) {
    // second track
    do_step_key(1, x, y - 3, z);
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
  if (z == 0)
    return;

  if (x <= 3) {
    // cue area
    print_dbg("\r\n cue area");
  } else if (x <= 7) {
    // pattern area
    print_dbg("\r\n pattern: ");
    u8 p = (y * 4) + (x - 4);
    print_dbg_ulong(p);
    // TODO: break this out into a do_track_pattern_select
    // FIXME: this also needs to adjust the playhead
    if (track_selection[0]) {
      view[0].track->pattern = p;
      print_dbg(" [t1]");
    }
    if (track_selection[1]) {
      view[1].track->pattern = p;
      print_dbg(" [t2]");
    }
  } else if (x == 9 || x == 10) {
    // meta area
    print_dbg("\r\n meta: ");
    u8 m = (y * 2) + (x - 9);
    print_dbg_ulong(m);
  } else if (x == 12) {
    // meta live area
    if (y == 0) {
      print_dbg("\r\n meta: live 1");
    } else if (y == 3) {
      print_dbg("\r\n meta: live 2");
    }
  } else {
    print_dbg("\r\n empty pattern view key");
  }
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

  // page selection (via navigation quad)
  if (x == 1) {
    // left half of page select
    if (y == 6) {
      if (z == 1) {
        view[0].page = view[1].page = 0;
        return true;
      }
    } else if (y == 7) {
      if (z == 1) {
        view[0].page = view[1].page = 2;
        return true;
      }
    }
  } else if (x == 2) {
    // right half of page select
    if (y == 6) {
      if (z == 1) {
        view[0].page = view[1].page = 1;
        return true;
      }
    } else if (y == 7) {
      if (z == 1) {
        view[0].page = view[1].page = 3;
        return true;
      }
    }
  }

  // track select (only in pattern mode)
  if (ui_mode == uiPattern) {
    if (x == 4) {
      if (y == 6) {
        track_selection[0] = z;
        // print_dbg("\r\n track_selection[0] = ");
        // print_dbg_ulong(z);
        return true;
      } else if (y == 7) {
        track_selection[1] = z;
        // print_dbg("\r\n track_selection[1] = ");
        // print_dbg_ulong(z);
        return true;
      }
    }
  }

  // playhead (only in edit mode)
  if (ui_mode == uiEdit && z == 1) {
    // TODO: refactor to split up ui_modes
    if (x == 3) {
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
    } else if (x >= 11) {
      // step timing controls
      if (y == 7) {
        do_focused_step_timing(x - 13); // -2, -1, 0, 1, 2
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

inline static u16 edge_pack(u8 level, u16 offset) {
  edge_t e = {.f = {.set = 1, .level = level, .offset = offset}};
  return e.v;
}

static void render_grid(void) {
  switch (ui_mode) {
  case uiEdit:
    track_view_steps(&view[0], 0, /* show_playhead */ true);
    track_view_steps(&view[1], 3, /* show_playhead */ true);
    render_nav();
    render_playhead_nudge(3, 6);
    render_playhead_nudge(3, 7);

    if (step_focus.z) {
      render_step_nudge(11, 7);
    }
    break;

  case uiLength:
    track_view_length(&view[0], 0);
    track_view_length(&view[1], 3);
    render_nav();
    break;

  case uiPattern:
    render_cue_mode(0, 0, cueNone);
    render_cue_mode(0, 3, cueNone);

    render_pattern_area(4, 0);
    render_meta_area(9, 0);

    render_nav();
    render_track_select(4, 6);

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

static void render_playhead_nudge(u8 x, u8 y) {
  u8 offset = monome_xy_idx(x, y);
  monomeLedBuffer[offset] = L1;
  monomeLedBuffer[offset + 1] = L2;
  monomeLedBuffer[offset + 2] = L1;
}

static void render_step_nudge(u8 x, u8 y) {
  u8 offset = monome_xy_idx(x, y);
  monomeLedBuffer[offset] = L2;     // coarse nudge early
  monomeLedBuffer[offset + 1] = L1; // fine nudge early
  monomeLedBuffer[offset + 2] = L3; // reset (center) timing
  monomeLedBuffer[offset + 3] = L1; // fine nudge late
  monomeLedBuffer[offset + 4] = L2; // coarse nudge late
}

static void render_track_select(u8 x, u8 y) {
  monomeLedBuffer[monome_xy_idx(x, y)] = L1;
  monomeLedBuffer[monome_xy_idx(x, y + 1)] = L1;
}

static void render_cue_mode(u8 x, u8 y, cue_mode_t mode) {
  u8 offset = monome_xy_idx(x, y);
  // monomeLedBuffer[offset] = mode == cueNone ? L2 : L1;
  // monomeLedBuffer[offset + 1] = mode == cueNone ? L3 : L2;
  // monomeLedBuffer[offset + 2] = mode == cueNone ? L4 : L3;
  monomeLedBuffer[offset] = L1;
  monomeLedBuffer[offset + 1] = L2;
  monomeLedBuffer[offset + 2] = L3;
  monomeLedBuffer[monome_xy_idx(x + mode, y + 1)] = L2;
}

static void render_pattern_area(u8 x, u8 y) {
  u8 top1 = monome_xy_idx(x, y);
  u8 bottom1 = monome_xy_idx(x, y + 2);
  u8 top2 = monome_xy_idx(x, y + 3);
  u8 bottom2 = monome_xy_idx(x, y + 5);
  for (u8 i = 0; i < 4; i++) {
    monomeLedBuffer[top1 + i] = L1;
    monomeLedBuffer[bottom1 + i] = L1;
    monomeLedBuffer[top2 + i] = L1;
    monomeLedBuffer[bottom2 + i] = L1;
  }

  // show which track is selected, consider a way to solo a track to
  // disambiguate
  bool active_selection = track_selection[0] || track_selection[1];
  for (u8 i = 0; i < GRID_NUM_TRACKS; i++) {
    u8 p = view[i].track->pattern;
    u8 py = p >> 2;
    u8 px = p - (py * 4);
    u8 idx = monome_xy_idx(px + x, py + y);
    u8 level = L3;
    if (active_selection && track_selection[i] == 0) {
      // if a track selection key is held, dim the _non_ selected pattern marker
      level = L2;
    }
    monomeLedBuffer[idx] = level;
  }
}

static void render_meta_area(u8 x, u8 y) {
  u8 top1 = monome_xy_idx(x, y);
  u8 bottom1 = monome_xy_idx(x, y + 2);
  u8 top2 = monome_xy_idx(x, y + 3);
  u8 bottom2 = monome_xy_idx(x, y + 5);
  for (u8 i = 0; i < 2; i++) {
    monomeLedBuffer[top1 + i] = L1;
    monomeLedBuffer[bottom1 + i] = L1;
    monomeLedBuffer[top2 + i] = L1;
    monomeLedBuffer[bottom2 + i] = L1;
  }

  // TODO: show meta selection

  // live buffer button/latch
  monomeLedBuffer[monome_xy_idx(x + 3, y)] = L1;
  monomeLedBuffer[monome_xy_idx(x + 3, y + 3)] = L1;
}

static void render_meta_buffer_bar(u8 x, u8 y) {
}

static void build_waves(void) {
  memset(&waves, 0, sizeof(waves));

  u8 wn = 0;
  for (u8 tn = 0; tn < 2; tn++) {
    u8 sn = playhead_position(&playhead[tn]);

    for (u8 v = 0; v < VOICE_COUNT; v++) {
      u8 edge_idx = 0;
      // add falling edge for gates which are high at the end of the previous phasor cycle
      if (carry[wn].f.set) {
        waves[wn].edges[edge_idx].v = carry[wn].v;
        carry[wn].v = 0; // clear the carry so we don't repeat
      }

      pattern_t *pat = track_view_pattern(&view[tn]);
      trig_t t = pat->step[sn].voice[v];

      if (t.enabled && t.value) {
        // determine rise
        u8 rise = MID_PHASE + t.timing;
        if (waves[wn].edges[edge_idx].f.set) {
          if (rise > waves[wn].edges[edge_idx].f.offset) {
            // only add a trigger if lands after the previous, otherwise tie
            ++edge_idx;
            waves[wn].edges[edge_idx++].v = edge_pack(1, rise);
          } else {
            // rise comes before previous fall, erase previous fall
            waves[wn].edges[edge_idx].v = 0;
          }
        } else {
          waves[wn].edges[edge_idx++].v = edge_pack(1, rise);
        }

        // determine fall
        u8 fall = rise + 4;
        if (fall > PPQ) {
          carry[wn].v = edge_pack(0, fall - PPQ);
        } else {
          // FIXME: what happens if fall == PPQ?
          waves[wn].edges[edge_idx++].v = edge_pack(0, fall);
        }
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
    edge_t edge = waves[wn].edges[waves[wn].cursor];
    if (edge.f.set && edge.f.offset == now) {
      edge.f.level ? set_tr(wn) : clr_tr(wn);
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

static void do_step_key(u8 tn, u8 x, u8 y, u8 z) {
  track_view_t *v = &view[tn];
  pattern_t *pat = track_view_pattern(v);
  u8 n = v->page * PAGE_SIZE + x;
  step_t *s = &pat->step[n];

  if (z == 1) {
    if (step_selection) {
      step_toggle_select(s, y);
    } else {
      // only focus on the step if it is enabled
      // track the last press step key
      // NOTE: n could be > than the track length
      step_focus.track = tn;
      step_focus.step = n;
      step_focus.voice = y;
      step_focus.z = step_get(s, y) != 0;
      step_focus.hold_count = 3; // NOTE: keytimer period is 50 so this is 50 * 3
      step_focus.fresh_trig = false;

      if (step_get(s, y) == 0) {
        step_set(s, y, 1);
        step_focus.z = 1;
        step_focus.fresh_trig = true;
        // print_dbg("\r\n > trig set");
      }
    }
  } else {
    // z == 0
    if (step_focus.track == tn && step_focus.step == n && step_focus.voice == y) {
      if (step_focus.hold_count > 0 && step_get(s, y) != 0 && !step_focus.fresh_trig) {
        // quick press and release, toggle
        step_set(s, y, 0);
        // print_dbg("\r\n > trig clear");
      }
      step_focus.z = 0;
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

static void do_focused_step_timing(s8 direction) {
  if (step_focus.z == 1) {
    pattern_t *pat = track_view_pattern(&view[step_focus.track]);
    if (step_focus.step < pat->length) {
      trig_t *t = &pat->step[step_focus.step].voice[step_focus.voice];
      if (direction == 0) {
        t->timing = 0;
        print_dbg("\r\n timing reset");
      } else {
        s8 delta = direction;
        if (direction == -2 || direction == 2) {
          delta *= 4;
        }
        t->timing = sclip(t->timing + delta, -MID_PHASE, MID_PHASE);
        print_dbg("\r\n timing = ");
        if (t->timing < 0) {
          print_dbg("-");
          print_dbg_ulong(abs(t->timing));
        } else {
          print_dbg_ulong(t->timing);
        }
      }
    } else {
      print_dbg("\r\n focused step > pattern length");
    }
  }
}