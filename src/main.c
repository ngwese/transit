//
// transit
//

#include <stdio.h>
#include <string.h> // memcpy

// asf
#include "delay.h"
#include "compiler.h"
#include "flashc.h"
#include "preprocessor.h"
#include "print_funcs.h"
#include "intc.h"
#include "pm.h"
#include "gpio.h"
#include "spi.h"
#include "sysclk.h"

// libavr32
#include "types.h"
#include "events.h"
#include "i2c.h"
#include "ii.h"
#include "init_common.h"
#include "init_trilogy.h"
#include "midi.h"
#include "monome.h"
#include "phasor.h"
#include "timers.h"
#include "adc.h"
#include "util.h"
#include "ftdi.h"
#include "twi.h"

// this
#include "conf_board.h"

#include "main.h"
#include "mode_grid.h"
#include "mode_arc.h"
#include "mode_midi.h"
#include "mode_div.h"

#define FIRSTRUN_KEY 0x22



////////////////////////////////////////////////////////////////////////////////
// prototypes

void clock_null(u8 phase);

// start/stop monome polling/refresh timers
extern void timers_set_monome(void);
extern void timers_unset_monome(void);

// check the event queue
static void check_events(void);

// handler protos (internal)
static void handler_KeyTimer(s32 data);
static void handler_Front(s32 data);
static void handler_FrontShort(s32 data);
static void handler_FrontLong(s32 data);
static void handler_FtdiConnect(s32 data);
static void handler_FtdiDisconnect(s32 data);
static void handler_MonomeConnect(s32 data);
static void handler_MonomePoll(s32 data);
static void handler_MidiConnect(s32 data);
static void handler_MidiDisconnect(s32 data);
static void handler_ClockNormal(s32 data);
static void handler_ClockExt(s32 data);


static void ii_null(uint8_t *d, uint8_t l);

u8 flash_is_fresh(void);
void flash_unfresh(void);
void flash_write(void);
void flash_read(void);
void state_write(void);
void state_read(void);


const u8 outs[8] = {B00, B01, B02, B03, B04, B05, B06, B07};

////////////////////////////////////////////////////////////////////////////////
// globals

connected_t connected;
bool external_clock;

static uint8_t clock_phase;
static uint8_t front_timer;

u16 adc[4];

static transit_mode_t active_mode;

__attribute__((__section__(".flash_nvram")))
nvram_data_t f;


////////////////////////////////////////////////////////////////////////////////
// timers

static softTimer_t clockTimer = { .next = NULL, .prev = NULL };
static softTimer_t keyTimer = { .next = NULL, .prev = NULL };
static softTimer_t adcTimer = { .next = NULL, .prev = NULL };
static softTimer_t monomePollTimer = { .next = NULL, .prev = NULL };
static softTimer_t monomeRefreshTimer  = { .next = NULL, .prev = NULL };
static softTimer_t midiPollTimer = { .next = NULL, .prev = NULL };


////////////////////////////////////////////////////////////////////////////////
// timer callbacks

static void clockTimer_callback(void* o) {  
	clock_phase++;
	if (clock_phase > 1)
		clock_phase = 0;
	clock(clock_phase);
}

static void keyTimer_callback(void* o) {  
	static event_t e;
	e.type = kEventKeyTimer;
	e.data = 0;
	event_post(&e);
}

static void adcTimer_callback(void* o) {  
	static event_t e;
	e.type = kEventPollADC;
	e.data = 0;
	event_post(&e);
}

static void monome_poll_timer_callback(void* obj) {
	ftdi_read();
}

static void monome_refresh_timer_callback(void* obj) {
	if (monomeFrameDirty > 0) {
		static event_t e;
		e.type = kEventMonomeRefresh;
		event_post(&e);
	}
}

static void midi_poll_timer_callback(void* obj) {
	midi_read();
}

void timers_set_monome(void) {
	timer_add(&monomePollTimer, 20, &monome_poll_timer_callback, NULL );
	timer_add(&monomeRefreshTimer, 30, &monome_refresh_timer_callback, NULL );
}

void timers_unset_monome(void) {
	timer_remove(&monomePollTimer);
	timer_remove(&monomeRefreshTimer);
}

////////////////////////////////////////////////////////////////////////////////
// mode switching

void set_mode(transit_mode_t m) {
	// ensure external clock is set correctly
	external_clock = !gpio_get_pin_value(B09);

	// exit
	switch (active_mode) {
	case mGrid:
		leave_mode_grid();
		break;
	case mArc:
		leave_mode_arc();
		break;
	case mMidi:
		leave_mode_midi();
		break;
	case mDiv:
		leave_mode_div();
		break;
	default:
		break;
	}

	// enter 
	switch (m) {
	case mGrid:
		enter_mode_grid();
		active_mode = m;
		break;
	case mArc:
		enter_mode_arc();
		active_mode = m;
		break;
	case mMidi:
		enter_mode_midi();
		active_mode = m;
		break;
	case mDiv:
		enter_mode_div();
		active_mode = m;
		break;
	default:
		break;
	}
}

////////////////////////////////////////////////////////////////////////////////
// event handlers

void handler_None(s32 data) {
}

void handler_PollADC(s32 data) {
	static u16 clock_last = 10000;
	u16 i, clock_time;

	adc_convert(&adc);

	i = adc[0];
	i = i >> 2;

	if (i != clock_last) {
		// 500ms - 12ms
		clock_time = 25000 / (i + 25);
		//print_dbg("\r\nclock (ms): ");
		//print_dbg_ulong(clock_time);
		clock_set(clock_time);
	}

	clock_last = i;
}

static void handler_FtdiConnect(s32 data) { 
	ftdi_setup();
}

static void handler_FtdiDisconnect(s32 data) { 
	timers_unset_monome();
	app_event_handlers[ kEventFrontShort ]	= &handler_FrontShort;
	app_event_handlers[ kEventFrontLong ]	= &handler_FrontLong;
	connected = conNONE;
}

static void handler_MonomeConnect(s32 data) {
	print_dbg("\r\n> connect: monome ");

	switch (monome_device()) {
	case eDeviceGrid:
		print_dbg("GRID");
		connected = conGRID;
		set_mode(mGrid);
		break;
	case eDeviceArc:
		print_dbg("ARC");
		connected = conARC;
		set_mode(mArc);
		break;
	default:
		break;
	}
	timers_set_monome();
}

static void handler_MonomePoll(s32 data) { 
	monome_read_serial(); 
}

static void handler_MidiConnect(s32 data) {
	print_dbg("\r\n> midi connect");
	timer_add(&midiPollTimer, 8, &midi_poll_timer_callback, NULL);
	connected = conMIDI;
	set_mode(mMidi);
}

static void handler_MidiDisconnect(s32 data) {
	print_dbg("\r\n> midi disconnect");
	timer_remove(&midiPollTimer);
	app_event_handlers[ kEventFrontShort ] = &handler_FrontShort;
	app_event_handlers[ kEventFrontLong ]	= &handler_FrontLong;
	connected = conNONE;
	set_mode(mDiv);
}

static void handler_Front(s32 data) {
	//print_dbg("\r\n+ front ");
	//print_dbg_ulong(data);

	if (data == 1) {
		front_timer = KEY_HOLD_TIME;
	}
	else {
		if (front_timer) {
			static event_t e;
			e.type = kEventFrontShort;
			e.data = 0;
			event_post(&e);
		}
		front_timer = 0;
	}
}

static void handler_FrontShort(s32 data) {
	if (connected == conNONE) {
		print_dbg("\r\n explicit switch to div");
		set_mode(mDiv);
	}
}

static void handler_FrontLong(s32 data) {
}

static void handler_SaveFlash(s32 data) {
	flash_write();
}

static void handler_KeyTimer(s32 data) {
	static uint8_t keyfront_state;

	if (keyfront_state != !gpio_get_pin_value(NMI)) {
		keyfront_state = !gpio_get_pin_value(NMI);
		static event_t e;
		e.type = kEventFront;
		e.data = keyfront_state;
		event_post(&e);
	}

	if (front_timer) {
		if (front_timer == 1) {
			static event_t e;
			e.type = kEventFrontLong;
			e.data = 0;
			event_post(&e);
			front_timer = 0;
		}
		else {
			front_timer--;
		}
	}

	switch (connected) {
	case conGRID:
		keytimer_grid();
		break;
	case conARC:
		keytimer_arc();
		break;
	case conMIDI:
		keytimer_midi();
		break;
	case conFLASH:
	case conNONE:
		break;
	}
}

static void handler_ClockNormal(s32 data) {
	external_clock = data != 0;
}

static void handler_ClockExt(s32 data) {
	print_dbg("\r\n clock ext: ");
	print_dbg_ulong(data);
}



// assign default event handlers
static inline void assign_main_event_handlers(void) {
	app_event_handlers[ kEventFront ]	= &handler_Front;
	app_event_handlers[ kEventFrontShort ] = &handler_FrontShort;
	app_event_handlers[ kEventFrontLong ]	= &handler_FrontLong;
	app_event_handlers[ kEventPollADC ]	= &handler_PollADC;
	app_event_handlers[ kEventKeyTimer ] = &handler_KeyTimer;
	app_event_handlers[ kEventSaveFlash ] = &handler_SaveFlash;
	app_event_handlers[ kEventFtdiConnect ]	= &handler_FtdiConnect;
	app_event_handlers[ kEventFtdiDisconnect ]	= &handler_FtdiDisconnect;
	app_event_handlers[ kEventMonomeConnect ]	= &handler_MonomeConnect;
	app_event_handlers[ kEventMonomeDisconnect ]	= &handler_None;
	app_event_handlers[ kEventMonomePoll ]	= &handler_MonomePoll;
	app_event_handlers[ kEventMonomeRefresh ]	= &handler_None;
	app_event_handlers[ kEventMonomeGridKey ]	= &handler_None;
	app_event_handlers[ kEventMonomeRingEnc ]	= &handler_None;
	app_event_handlers[ kEventClockNormal ] = &handler_ClockNormal;
	app_event_handlers[ kEventClockExt ] = &handler_ClockExt;
	app_event_handlers[ kEventMidiConnect ]	= &handler_MidiConnect;
	app_event_handlers[ kEventMidiDisconnect ] = &handler_MidiDisconnect;
	app_event_handlers[ kEventMidiPacket ] = &handler_None;
}

// app event loop
void check_events(void) {
	static event_t e;
	if (event_next(&e)) {
		(app_event_handlers)[e.type](e.data);
	}
}



////////////////////////////////////////////////////////////////////////////////
// flash

u8 flash_is_fresh(void) {
  return (f.fresh != FIRSTRUN_KEY);
}

void flash_unfresh(void) {
  flashc_memset8((void*)&(f.fresh), FIRSTRUN_KEY, 4, true);
}

void flash_write(void) {
	print_dbg("\r\n> write preset ");
	//print_dbg_ulong(preset_select);
	//flashc_memset8((void*)&(f.preset_select), preset_select, 4, true);

	// flashc_memcpy((void *)&(f.state), &ansible_state, sizeof(ansible_state), true);
}

void flash_read(void) {
	print_dbg("\r\n> read preset ");
	//print_dbg_ulong(preset_select);

	//preset_select = f.preset_select;

	// memcpy(&ansible_state, &f.state, sizeof(ansible_state));

	// ...
}


////////////////////////////////////////////////////////////////////////////////
// functions

void set_tr(uint8_t n) {
	gpio_set_gpio_pin(outs[n]);
}

void clr_tr(uint8_t n) {
	gpio_clr_gpio_pin(outs[n]);
}

uint8_t get_tr(uint8_t n) {
	return gpio_get_pin_value(outs[n]);
}

void clock_set(uint32_t n) {
	timer_set(&clockTimer, n);
}

void clock_set_tr(uint32_t n, uint8_t phase) {
	timer_set(&clockTimer, n);
	clock_phase = phase;
	timer_manual(&clockTimer);
}

static void ii_null(uint8_t *d, uint8_t l) {
	print_dbg("\r\nii/null");
}




////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// main

int main(void)
{
	sysclk_init();

	init_dbg_rs232(FMCK_HZ);

	init_gpio();
	assign_main_event_handlers();
	init_events();
	init_tc();
	init_spi();
	init_adc();

	irq_initialize_vectors();
	register_interrupts();
	cpu_irq_enable();

	// must be called after irq_initialize_vectors!
	init_phasor();

	print_dbg("\r\n\n// transit //////////////////////////////// ");
	print_dbg("\r\n   flash struct size: ");
	print_dbg_ulong(sizeof(f));

	if (flash_is_fresh()) {
		// store flash defaults
		print_dbg("\r\nfirst run.");
		flashc_memset32((void*)&(f.mode), mDiv, 4, true);

		default_grid();
		default_arc();
		default_midi();
		default_div();
		
		flash_unfresh();
	}
	else {
		// load from flash at startup
	}

	init_grid();
	init_arc();
	init_midi();
	init_div();

	init_i2c_slave(0x30);
	process_ii = &ii_null;

	for (u8 i = 0; i < 8; i++) {
		clr_tr(i);
	}

	clock = &clock_null;

	timer_add(&clockTimer, 1000, &clockTimer_callback, NULL);
	timer_add(&keyTimer, 50, &keyTimer_callback, NULL);
	timer_add(&adcTimer, 100, &adcTimer_callback, NULL);

	connected = conNONE;
	set_mode(f.mode);

	init_usb_host();
	init_monome();

	while (true) {
		check_events();
	}
}
