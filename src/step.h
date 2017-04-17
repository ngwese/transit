#ifndef __STEP_H__
#define __STEP_H__

typedef struct {
	unsigned char sub : 4;
	unsigned char mute : 1;
	unsigned char reserved : 3;  // TBD
} step_t;

void step_init(step_t *s);
bool step_mute(step_t *s);
void step_set_mute(step_t *s, bool m);

#endif // __STEP_H__
