// param selects output to edit [1-8]
// front short, toggles "hold" focus on output
// enc 0 adjust start
// enc 1 adjust end
// clock in is reset
// clock out is internal clock
//
// enc adjust is relative to current values (need to store orig values
// when doing multiple outputs to allow scal up/down w/o loosing data)
//
// when start goes to/past end does it push end?
//
// hold down front for config mode:
// - turn param to adust clock
// - turn enc 0 to choose in == clock or in == reset
// - turn enc 1 to scale output divisors
//     (fan out 8 ticks on enc 1 showing the divisions)
//
// ideas:
// - slew between divisions (fractional math??)

#include <stdlib.h>
#include <string.h>

// asf
#include "gpio.h"
#include "print_funcs.h"

// libavr32
#include "adc.h"
#include "events.h"
#include "flashc.h"
#include "i2c.h"
#include "mailbox.h"
#include "monome.h"
#include "phasor.h"
#include "util.h"

// this
#include "main.h"
#include "mode_arc.h"

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

//------------------------------
//------ types

typedef enum {
  uiPlay,
  uiConfig,
} ui_mode_t;

typedef struct {
  u16 phase;  // ticks before beginning cycle
  u16 width;  // width of gate in ticks
  u16 period; // ticks cycle (0 == one shot?)
} gate_t;

typedef struct {
  u8 divisor;
  bool selected;

  gate_t base;
  gate_t effective; // factoring in divisor

  // wave_state_t state;
  // u16 this_period;   // effective period +/- delta phase
  // u16 this_width;

  mailbox_t divisor_change;
  mailbox_t phase_change;

  u16 now; // local tick count [0-effective.period)
  bool fired;
  // bool cycling;    // are
} output_t;

//------------------------------
//------ prototypes

static void write_arc(void);
static void read_arc(void);

static void render_arc(void);

static void handler_ArcPollADC(s32 data);
static void handler_ArcFrontShort(s32 data);
static void handler_ArcFrontLong(s32 data);
static void handler_ArcClockExt(s32 data);
static void handler_ArcClockNormal(s32 data);
static void handler_ArcEnc(s32 data);
static void handler_ArcRefresh(s32 data);

static void set_output_divisor(u8 n, u8 divisor);
static void set_output_phase(u8 n, u16 phase);
static void set_output_width(u8 n, u16 width);
static void set_output_period(u8 n, u16 period);

static void enc_do_play(u8 n, s8 delta);
static void enc_do_config(u8, s8 delta);
static void enc_set_acc_play(u8 selection);
static void enc_set_acc_config(u8 selection);

static u16 calc_clock_frequency(u16 rate);

// callback for phasor
static void process_outputs(u8 now, bool reset);

//-----------------------------
//----- globals

// https://en.wikipedia.org/wiki/Triangular_number
const uint8_t delta_acc[16] = {0,  1,  3,  6,  10, 15, 21,  28,
                               36, 45, 55, 66, 78, 91, 105, 120};

static bool fresh = false;
static s32 enc_acc[4];

static output_t output[8];
static u16 clock_hz;
static u16 ext_clock_width = MAX_WIDTH >> 1;

static u8 selection;

static ui_mode_t ui_mode;

// copy of nvram state for editing
static arc_state_t arc_state;

void enter_mode_arc(void) {
  print_dbg("\r\n> mode arc");
  read_arc();

  app_event_handlers[kEventPollADC] = &handler_ArcPollADC;
  app_event_handlers[kEventClockExt] = &handler_ArcClockExt;
  app_event_handlers[kEventClockNormal] = &handler_ArcClockNormal;
  app_event_handlers[kEventMonomeRingEnc] = &handler_ArcEnc;
  app_event_handlers[kEventMonomeRefresh] = &handler_ArcRefresh;

  ui_mode = uiPlay;

  resume_arc();

  clock = &clock_null;

  process_ii = &ii_arc;

  if (connected == conARC) {
    app_event_handlers[kEventFrontShort] = &handler_ArcFrontShort;
    app_event_handlers[kEventFrontLong] = &handler_ArcFrontLong;
  }

  clock_hz = calc_clock_frequency(arc_state.clock_rate);
  phasor_set_callback(&process_outputs);
  phasor_setup(clock_hz, PPQ);
  phasor_start();
}

void leave_mode_arc(void) {
  print_dbg("\r\n leave mode arc");
  phasor_stop();
  phasor_set_callback(NULL);
}

////////////////////////////////////////////////////////////////////////////////
///// handlers

void handler_ArcPollADC(s32 data) {
  static u16 last = 10000;
  u16 p;

  adc_convert(&adc);

  p = adc[0] >> 9;
  if (p != last) {
    // print_dbg("\r\n arc: poll adc ");
    // print_dbg_ulong(p);
    last = p;
    selection = p;

    if (ui_mode == uiPlay) {
      enc_set_acc_play(selection);
    } else if (ui_mode == uiConfig) {
      enc_set_acc_config(selection);
    }

    monomeFrameDirty++;
  }
}

void handler_ArcFrontShort(s32 data) {
  // print_dbg("\r\n arc: front short ");
  // print_dbg_ulong(data);
  if (ui_mode == uiPlay) {
    output[selection].selected = !output[selection].selected;
  } else if (ui_mode == uiConfig) {
    enc_set_acc_play(selection);
    ui_mode = uiPlay;
  }
  monomeFrameDirty++;
}

void handler_ArcFrontLong(s32 data) {
  // print_dbg("\r\n arc: front long ");
  // print_dbg_ulong(data);
  enc_set_acc_config(selection);
  ui_mode = uiConfig;
  monomeFrameDirty++;
}

void handler_ArcClockExt(s32 data) {
  if (data) {
    phasor_reset();
  }
}

void handler_ArcClockNormal(s32 data) {
  // nothing
}

void handler_ArcEnc(s32 data) {
  u8 n;
  s8 delta;

  monome_ring_enc_parse_event_data(data, &n, &delta);

  if (ui_mode == uiPlay) {
    enc_do_play(n, delta);
  } else if (ui_mode == uiConfig) {
    enc_do_config(n, delta);
  }
}

void handler_ArcRefresh(s32 data) {
  if (monomeFrameDirty) {
    render_arc();
    monome_set_quadrant_flag(0);
    monome_set_quadrant_flag(1);
    // monome_set_quadrant_flag(2);
    // monome_set_quadrant_flag(3);
    (*monome_refresh)();
  }
}

//////////////////////////////////////////////////
///// app

static void set_output_divisor(u8 n, u8 divisor) {
  output[n].divisor = uclip(divisor, 1, MAX_DIVISOR);
}

static void calc_effective_divisor(u8 n) {
  u8 d = output[n].divisor;
  output[n].effective.phase = output[n].base.phase * d;
  output[n].effective.width = output[n].base.width * d;
  output[n].effective.period = output[n].base.period * d;
}

static void set_output_phase(u8 n, u16 phase) {
  u16 t = uclip(phase, 0, MAX_PHASE);
  output[n].base.phase = t;
  output[n].effective.phase = t * output[n].divisor;

  // print_dbg("\r\n > phase n: ");
  // print_dbg_ulong(n);
  // print_dbg(" b: ");
  // print_dbg_ulong(output[n].base.phase);
  // print_dbg(" e: ");
  // print_dbg_ulong(output[n].effective.phase);
}

// static void calc_effective_phase(u8 n) {
//	u16 t = output[n].base.phase;
//	output[n].effective.phase = t * output[n].divisor;
//}

static void set_output_width(u8 n, u16 width) {
  u16 t = uclip(width, 1, MAX_WIDTH);
  output[n].base.width = t;
  output[n].effective.width = t * output[n].divisor;

  // print_dbg("\r\n > width n: ");
  // print_dbg_ulong(n);
  // print_dbg(" b: ");
  // print_dbg_ulong(output[n].base.width);
  // print_dbg(" e: ");
  // print_dbg_ulong(output[n].effective.width);
}

static void set_output_period(u8 n, u16 period) {
  u16 t = uclip(period, 1, MAX_PERIOD);
  output[n].base.period = t;
  output[n].effective.period = t * output[n].divisor;

  // print_dbg("\r\n > period n: ");
  // print_dbg_ulong(n);
  // print_dbg(" b: ");
  // print_dbg_ulong(output[n].base.period);
  // print_dbg(" e: ");
  // print_dbg_ulong(output[n].effective.period);
}

static void reset_output(u8 n) {
  clr_tr(n);

  mailbox_init(&output[n].divisor_change);
  mailbox_init(&output[n].phase_change);

  output[n].divisor = 1;
  output[n].selected = false;

  set_output_phase(n, 0);
  set_output_width(n, MAX_WIDTH >> 1);
  set_output_period(n, MAX_PERIOD);

  output[n].now = 0;
  output[n].fired = false;
  // output[n].cycling = false;
}

static void reset_all_outputs(void) {
  for (u8 i = 0; i < 8; i++) {
    reset_output(i);
  }
}

static u16 calc_clock_frequency(u16 rate) {
  // assumes rate [0-1280]
  return rate + 8; // 8hz - 1288khz
}

static void enc_set_acc_play(u8 selection) {
  // preset acc so editing feels higher res
  enc_acc[0] = output[selection].base.phase << 7;
  enc_acc[1] = output[selection].base.width << 7;
}

static void enc_set_acc_config(u8 selection) {
  enc_acc[0] = arc_state.clock_rate << 5;
  enc_acc[1] = output[selection].divisor << 7;
}

static void enc_do_play(u8 n, s8 delta) {
  s16 t;

  // TODO: handle multi-selection

  delta = sclip(delta, -15, 15);
  delta = delta > 0 ? delta_acc[delta] : -delta_acc[-delta];
  enc_acc[n] = sclip(enc_acc[n] + delta, 0, PPQ << 7);

  switch (n) {
  case 0:
    t = sclip(enc_acc[0] >> 7, 0, PPQ);
    if (t != output[selection].base.phase) {
      set_output_phase(selection, t);
      monomeFrameDirty++;
    }
    break;
  case 1:
    t = sclip(enc_acc[1] >> 7, 0, PPQ);
    if (t != output[selection].base.width) {
      set_output_width(selection, t);
      monomeFrameDirty++;
    }
    break;
  default:
    break;
  }
}

static void enc_do_config(u8 n, s8 delta) {
  message_t msg;
  u8 i;
  u8 d;
  u16 hz, rate;

  switch (n) {
  case 0:
    // clock
    delta = sclip(delta, -15, 15);
    delta = delta > 0 ? delta_acc[delta] : -delta_acc[-delta];
    enc_acc[n] = sclip(enc_acc[n] + delta, 0, 1280 << 5);

    rate = enc_acc[n] >> 5;
    if (rate != arc_state.clock_rate) {
      arc_state.clock_rate = rate;
      clock_hz = calc_clock_frequency(arc_state.clock_rate);
      hz = phasor_set_frequency(clock_hz);
      // print_dbg("\r\nclock: ");
      // print_dbg_ulong(arc_state.clock_rate);
      // print_dbg(" hz: ");
      // print_dbg_ulong(hz);
      monomeFrameDirty++;
    }
    break;
  case 1:
    // adjust divisor(s)
    delta = sclip(delta, -15, 15);
    delta = delta > 0 ? delta_acc[delta] : -delta_acc[-delta];
    enc_acc[n] = sclip(enc_acc[n] + delta, 1 << 7, 16 << 7); // max divisor 16

    d = enc_acc[1] >> 7;
    for (i = 0; i < 8; i++) {
      if (i == selection || output[i].selected) {
        set_output_divisor(i, d);
        msg.which = i;
        msg.data = d;
        mailbox_post(&output[i].divisor_change, &msg, kPostReplace);
      }
    }
    monomeFrameDirty++;
    break;
  default:
    break;
  }
}

// each rig has 64 leds
// each output region has 16 (now 8)

static void arc_draw_selected(void) {
  u8 i, s, p;

  for (i = 0; i < 8; i++) {
    if (i == selection || output[i].selected) {
      // draw selection on enc 1
      s = 64 + (i * 8);
      for (p = 0; p < 7; p++) {
        monomeLedBuffer[s + p] = i == selection ? L2 : L1;
      }
      // make last led in region lower
      monomeLedBuffer[s + p - 1]--;
      monomeLedBuffer[s + p] = 0;
    }
  }
}

static void arc_draw_divisor(u8 n) {
  u8 i, s, d;

  s = 64;
  d = output[n].divisor;
  for (i = 0; i < d; i++) {
    monomeLedBuffer[s + i * 3] = L3;
  }
}

static void arc_draw_gate(u8 n) {
  u8 offset = 0; // enc 0
  u8 start = offset + output[n].base.phase;
  u8 end = start + output[n].base.width;

  monomeLedBuffer[start] = L3;
  for (u8 p = start + 1; p < end; p++) {
    if (p > 63) {
      monomeLedBuffer[p - 64] = L2;
    } else {
      monomeLedBuffer[p] = L2;
    }
  }
}

static void arc_draw_clock(void) {
  u8 offset = 0; // enc 0
  u8 p;

  // fan
  for (p = offset; p < offset + 64; p += 8) {
    monomeLedBuffer[p] = L1;
    monomeLedBuffer[p + 1] = L1;
  }

  // 1280 / 64 == 20
  p = uclip(offset + arc_state.clock_rate / 20, 0, 63);
  monomeLedBuffer[p] = L3;
}

static void render_arc(void) {
  memset(monomeLedBuffer, 0, sizeof(monomeLedBuffer));

  if (ui_mode == uiPlay)
    arc_draw_gate(selection);
  else if (ui_mode == uiConfig)
    arc_draw_clock();

  arc_draw_selected();
  arc_draw_divisor(selection);
}

static void process_outputs(u8 now, bool reset) {
  message_t msg;
  u8 i;
  s32 n;

  // print_dbg("\r\nprocess: ");
  // print_dbg_ulong(now);

  // sync all
  if (reset) {
    for (i = 0; i < 8; i++) {
      clr_tr(i);
      output[i].now = now * output[i].divisor;
      output[i].fired = false;
      // output[i].cycling = false;
    }
  }

  if (now == 0) {
    gpio_set_gpio_pin(B10); // ext clock

    // apply queued division changes at base phasor cycle start
    for (i = 0; i < 8; i++) {
      if (mailbox_get(&output[i].divisor_change, &msg)) {
        // tweak divisor
        // set_output_divisor(i, msg.data);
        calc_effective_divisor(i);
      }
    }
  } else if (now >= ext_clock_width) {
    gpio_clr_gpio_pin(B10);
  }

  for (i = 0; i < 8; i++) {
    n = output[i].now - output[i].effective.phase;
    // print_dbg(" ");
    // print_dbg_ulong(output[i].now);
    // print_dbg("|");
    // print_dbg_ulong(n);

    // print_dbg("\r\n|  n: ");
    // print_dbg_hex(n);

    if (n < 0) {
      n = output[i].effective.period + n;
      // print_dbg(" warp: ");
      // print_dbg_ulong(n);
      // wrapped = true;
    }

    if (!output[i].fired && (n < output[i].effective.width)) {
      // print_dbg("+");
      // print_dbg_ulong(n);
      set_tr(i);
      output[i].fired = true;
    } else if (output[i].fired && (n >= output[i].effective.width)) {
      // print_dbg("-");
      // print_dbg_ulong(n);
      clr_tr(i);
      output[i].fired = false;
    }

    output[i].now++;
    if (output[i].now >= output[i].effective.period) {
      // print_dbg("\r\n| wrap: ");
      // print_dbg_ulong(output[i].now);
      output[i].now = 0;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
///// mode

void keytimer_arc(void) {
}

void default_arc(void) {
  flashc_memset16((void *)&(f.arc_state.clock_rate), 640, 2, true);
}

void write_arc(void) {
  flashc_memset16((void *)&(f.arc_state.clock_rate), arc_state.clock_rate, 2,
                  true);
}

void read_arc(void) {
  arc_state = f.arc_state;
}

void init_arc(void) {
  reset_all_outputs();

  // some more interesting defaults
  set_output_phase(1, 16);
  set_output_width(1, 16);

  set_output_divisor(2, 2);
  calc_effective_divisor(2);
  set_output_divisor(3, 2);
  calc_effective_divisor(3);
  set_output_phase(3, 32);
  set_output_width(3, 16);

  set_output_divisor(4, 3);
  calc_effective_divisor(4);
  set_output_divisor(5, 3);
  calc_effective_divisor(5);
  set_output_phase(5, 48);
  set_output_width(5, 8);

  set_output_divisor(6, 4);
  calc_effective_divisor(6);
  set_output_divisor(7, 5);
  calc_effective_divisor(7);
  set_output_phase(7, 8);
  set_output_width(7, 48);
}

void resume_arc(void) {
  if (fresh) {
    init_arc();
    fresh = false;
  } else {
    // more to do?
    monomeFrameDirty++;
  }
}

void ii_arc(uint8_t *d, uint8_t l) {
}
