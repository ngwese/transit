//
// Copyright (c) 2022 Greg Wuller.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

// asf
#include "string.h"

// libavr32
#include "util.h"

// this
#include "meta.h"
#include "mode_common.h"

void meta_init(meta_pattern_t *m) {
  memset(m, 0, sizeof(meta_pattern_t));
  m->loop = 1;
}

void meta_default(meta_pattern_t *m) {
  meta_init(m);
  m->occupied = 1;
  m->length = 1;
  m->steps[0].pattern = 0;
}

void meta_copy(meta_pattern_t *dst, meta_pattern_t *src) {
  memcpy(dst, src, sizeof(meta_pattern_t));
}

bool meta_push(meta_pattern_t *m, meta_step_t s) {
  if (m->length < (META_STEP_MAX - 1)) {
    m->steps[++m->length] = s;
    return true;
  }
  return false;
}