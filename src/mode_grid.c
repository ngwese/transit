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
#include "mode_grid.h"
#include "mode_common.h"
#include "track.h"
#include "playhead.h"

//------------------------------
//------ types

typedef enum {
  uiEdit,
  uiPlay,
  uiConfig
} ui_mode_t;


//------------------------------
//------ prototypes

static void write_grid(void);
static void read_grid(void);

static void handler_GridFrontShort(s32 data);
static void handler_GridFrontLong(s32 data);
static void handler_GridTr(s32 data);
static void handler_GridTrNormal(s32 data);

static void handler_DebugGridKey(s32 data);
static void handler_DebugGridRefresh(s32 data);

static void render_grid(void);

static void process_phasor(u8 now, bool reset);

//-----------------------------
//----- globals

static ui_mode_t ui_mode;
static track_t track[2];
static track_view_t view[2];
static playhead_t playhead[2];
static u16 clock_hz;
static u16 render_dirty;

// copy of nvram state for editing
static grid_state_t grid_state;

void enter_mode_grid(void) {
  print_dbg("\r\n> mode grid");
  read_grid();

  app_event_handlers[kEventTr] = &handler_GridTr;
  app_event_handlers[kEventTrNormal] = &handler_GridTrNormal;
  app_event_handlers[kEventMonomeGridKey] = &handler_DebugGridKey;
  app_event_handlers[kEventMonomeRefresh] = &handler_DebugGridRefresh;

  ui_mode = uiEdit;

  resume_grid();

  clock = &clock_null;

  process_ii = &ii_grid;

  if (connected == conGRID) {
    app_event_handlers[kEventFrontShort] = &handler_GridFrontShort;
    app_event_handlers[kEventFrontLong] = &handler_GridFrontLong;
  }

  clock_hz = calc_clock_frequency(grid_state.clock_rate);
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
  // flashc_memset32((void *)&(f.grid_state.clock_period), 100, 4, true);
  flashc_memset16((void *)&(f.grid_state.clock_rate), 640, 2, true);
}

void write_grid(void) {
  // flashc_memset32((void *)&(f.grid_state.clock_period), grid_state.clock_period,
  //                 4, true);
  flashc_memset16((void *)&(f.grid_state.clock_rate), grid_state.clock_rate, 2,
                  true);
}

void read_grid(void) {
  grid_state = f.grid_state;
}

void init_grid(void) {
  track_init(&track[0]);
  track_init(&track[1]);
  playhead_init(&playhead[0]);
  playhead_init(&playhead[1]);
  track_view_init(&view[0], &track[0], &playhead[0]);
  track_view_init(&view[1], &track[1], &playhead[1]);

  render_dirty++;
}

void resume_grid(void) {
  render_dirty++;
}


void ii_grid(uint8_t *d, uint8_t l) {
}

void handler_DebugGridKey(s32 data) {
  u8 x, y, z;
  monome_grid_key_parse_event_data(data, &x, &y, &z);
  print_dbg("\r\n key x: ");
  print_dbg_ulong(x);
  print_dbg(" y: ");
  print_dbg_ulong(y);
  print_dbg(" z: ");
  print_dbg_ulong(z);

  monomeFrameDirty++;
}

void handler_DebugGridRefresh(s32 data) {
}

//
// app
//


static void render_grid(void) {
  memset(monomeLedBuffer, 0, MONOME_MAX_LED_BYTES);

  if (ui_mode == uiEdit) {
    track_view_render(&view[0], 0, /* show_playhead */ true);
    track_view_render(&view[1], 3, /* show_playhead */ true);
    // TODO: render controls
  } else if (ui_mode == uiPlay) {

  } else if (ui_mode == uiConfig) {

  }
}

static void process_phasor(u8 now, bool reset) {
  if (now == 0) {
    playhead_advance(&playhead[0]);
    playhead_advance(&playhead[1]);
    render_dirty++;

    // clock out high
    gpio_set_gpio_pin(B10);
  } else if (now == 128) {
    // clock out low
    gpio_clr_gpio_pin(B10);
  }
}