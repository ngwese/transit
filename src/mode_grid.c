// asf
#include "print_funcs.h"

// libavr32
#include "events.h"
#include "flashc.h"
#include "i2c.h"

// this
#include "main.h"
#include "mode_grid.h"

//------------------------------
//------ prototypes

static void write_grid(void);
static void read_grid(void);

static void handler_GridFrontShort(s32 data);
static void handler_GridFrontLong(s32 data);
static void handler_GridTr(s32 data);
static void handler_GridTrNormal(s32 data);


//-----------------------------
//----- globals

// copy of nvram state for editing
static grid_state_t grid_state;

void enter_mode_grid(void) {
	print_dbg("\r\n> mode grid");
	grid_state = f.grid_state;

	app_event_handlers[kEventTr] = &handler_GridTr;
	app_event_handlers[kEventTrNormal] = &handler_GridTrNormal;

	clock_set(grid_state.clock_period);

	process_ii = &ii_grid;
	
	if (connected == conGRID) {
		app_event_handlers[kEventFrontShort] = &handler_GridFrontShort;
		app_event_handlers[kEventFrontLong] = &handler_GridFrontLong;
	}
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
	print_dbg("\r\n grid: front long");
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
	flashc_memset32((void*)&(f.grid_state.clock_period), 100, 4, true);
}

void write_grid(void) {
	flashc_memset32((void*)&(f.grid_state.clock_period),
									grid_state.clock_period, 4, true);
}

void read_grid(void) {
}

void init_grid(void) {
}

void resume_grid(void) {
}

void clock_grid(uint8_t phase) {
	if (phase)
		set_tr(0);
	else
		clr_tr(0);
}

void ii_grid(uint8_t *d, uint8_t l) {
}

