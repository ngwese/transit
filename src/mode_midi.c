// asf
#include "print_funcs.h"

// libavr32
#include "events.h"
#include "flashc.h"
#include "i2c.h"
#include "midi_common.h"

// this
#include "main.h"
#include "mode_midi.h"

//------------------------------
//------ prototypes

static void write_midi(void);
static void read_midi(void);

static void handler_MidiFrontShort(s32 data);
static void handler_MidiFrontLong(s32 data);
static void handler_MidiTr(s32 data);
static void handler_MidiTrNormal(s32 data);
static void handler_MidiPacket(s32 data);

//-----------------------------
//----- globals

// copy of nvram state for editing
static midi_state_t midi_state;

static midi_behavior_t behavior = {0};

void enter_mode_midi(void) {
  print_dbg("\r\n> mode midi");
  midi_state = f.midi_state;

  app_event_handlers[kEventTr] = &handler_MidiTr;
  app_event_handlers[kEventTrNormal] = &handler_MidiTrNormal;
  app_event_handlers[kEventMidiPacket] = &handler_MidiPacket;

  clock_set(midi_state.clock_period);

  process_ii = &ii_midi;

  if (connected == conMIDI) {
    app_event_handlers[kEventFrontShort] = &handler_MidiFrontShort;
    app_event_handlers[kEventFrontLong] = &handler_MidiFrontLong;
  }
}

void leave_mode_midi(void) {
}

////////////////////////////////////////////////////////////////////////////////
///// handlers

void handler_MidiFrontShort(s32 data) {
  print_dbg("\r\n midi: front short ");
  print_dbg_ulong(data);
}

void handler_MidiFrontLong(s32 data) {
  print_dbg("\r\n midi: front long");
  print_dbg_ulong(data);
}

void handler_MidiTr(s32 data) {
  print_dbg("\r\n midi: tr ");
  print_dbg_ulong(data);
}

void handler_MidiTrNormal(s32 data) {
  print_dbg("\r\n midi: tr normal ");
  print_dbg_ulong(data);
}

void handler_MidiPacket(s32 data) {
  midi_packet_parse(&behavior, (u32)data);
}

////////////////////////////////////////////////////////////////////////////////
///// mode

void keytimer_midi(void) {
}

void default_midi(void) {
  flashc_memset32((void *)&(f.midi_state.clock_period), 100, 4, true);
}

void write_midi(void) {
  flashc_memset32((void *)&(f.midi_state.clock_period), midi_state.clock_period, 4, true);
}

void read_midi(void) {
}

void init_midi(void) {
}

void resume_midi(void) {
}

void clock_midi(uint8_t phase) {
  if (phase)
    set_tr(0);
  else
    clr_tr(0);
}

void ii_midi(uint8_t *d, uint8_t l) {
}
