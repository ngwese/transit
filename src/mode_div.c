// asf
#include "gpio.h"
#include "print_funcs.h"

// libavr32
#include "events.h"
#include "flashc.h"
#include "i2c.h"

// this
#include "main.h"
#include "mode_div.h"

//------------------------------
//------ prototypes

static void write_div(void);
static void read_div(void);

static void handler_DivFrontShort(s32 data);
static void handler_DivFrontLong(s32 data);
static void handler_DivClockExt(s32 data);
static void handler_DivClockNormal(s32 data);

static void pulse_div(uint8_t phase);

//-----------------------------
//----- globals

// copy of nvram state for editing
static div_state_t div_state;
static u8 div_value[8];
static u8 div_counter[8];

void enter_mode_div(void) {
  print_dbg("\r\n> mode div");
  div_state = f.div_state;

  init_div();

  app_event_handlers[kEventPollADC] = &handler_PollADC; // from main
  app_event_handlers[kEventClockExt] = &handler_DivClockExt;
  app_event_handlers[kEventClockNormal] = &handler_DivClockNormal;

  clock = &clock_div;
  clock_set(div_state.clock_period);

  process_ii = &ii_div;

  if (connected == conNONE) {
    app_event_handlers[kEventFrontShort] = &handler_DivFrontShort;
    app_event_handlers[kEventFrontLong] = &handler_DivFrontLong;
  }
}

void leave_mode_div(void) {
}

////////////////////////////////////////////////////////////////////////////////
///// handlers

void handler_DivFrontShort(s32 data) {
  print_dbg("\r\n div: front short ");
  print_dbg_ulong(data);
}

void handler_DivFrontLong(s32 data) {
  print_dbg("\r\n div: front long");
  print_dbg_ulong(data);
}

void handler_DivClockExt(s32 data) {
  if (external_clock) {
    pulse_div(data);
  }
}

void handler_DivClockNormal(s32 data) {
  external_clock = data != 0;
}

////////////////////////////////////////////////////////////////////////////////
///// mode

void keytimer_div(void) {
}

void default_div(void) {
  flashc_memset32((void *)&(f.div_state.clock_period), 100, 4, true);
}

void write_div(void) {
  flashc_memset32((void *)&(f.div_state.clock_period), div_state.clock_period,
                  4, true);
}

void read_div(void) {
}

void init_div(void) {
  for (u8 i = 0; i < 8; i++) {
    div_counter[i] = div_value[i] = i + 1;
  }
}

void resume_div(void) {
}

void clock_div(uint8_t phase) {
  if (!external_clock) {
    pulse_div(phase);
  }
}

void pulse_div(uint8_t phase) {
  u8 i;

  if (phase) {
    gpio_set_gpio_pin(B10);
    for (i = 0; i < 8; i++) {
      if (div_counter[i] == div_value[i]) {
        set_tr(i);
      }
    }
  } else {
    gpio_clr_gpio_pin(B10);
    for (i = 0; i < 8; i++) {
      if (div_counter[i] <= div_value[i]) {
        clr_tr(i);
      }

      div_counter[i]--;
      if (div_counter[i] == 0) {
        div_counter[i] = div_value[i];
      }
    }
  }
}

void ii_div(uint8_t *d, uint8_t l) {
}
