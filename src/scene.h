#ifndef __SCENE_H__
#define __SCENE_H__

#include "pattern.h"

typedef struct {
	u16 empty;               // one bit per pattern
	pattern_t patterns[16];
	s8 sequence[24];         // which pattern to play, negative numbers are signals 
	bool track_mute[6];
} scene_t;

void scene_init(scene_t *s);

#endif // __SCENE_H__
