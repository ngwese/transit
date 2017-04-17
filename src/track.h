#ifndef __TRACK_H__
#define __TRACK_H__

#include "step.h"

#define TRACK_STEPS 64  // 4 pages * 16 steps

typedef struct {
	step_t steps[TRACK_STEPS];
	u8 length;
	u8 meter[2];   // time signature
} track_t;

void track_init(track_t *s, u8 beats, u8 unit);

#endif // __TRACK_H__
