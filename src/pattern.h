#ifndef __PATTERN_H__
#define __PATTERN_H__

#include "step.h"

typedef struct {
	track_t tracks[6];
} pattern_t;

void pattern_init(pattern_t *s);

#endif // __PATTERN_H__
