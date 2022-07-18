//
// Copyright (c) 2022 Greg Wuller.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#define META_STEP_MAX 12

// libavr32
#include "compiler.h"
#include "types.h"

typedef struct {
  u8 pattern;
} meta_step_t;

typedef struct {
  meta_step_t steps[META_STEP_MAX];
  u8 length;
  u8 occupied : 1;
  u8 loop : 1;
  u8 reserved : 6;
} meta_pattern_t;

void meta_init(meta_pattern_t *m);
void meta_default(meta_pattern_t *m);
void meta_copy(meta_pattern_t *dst, meta_pattern_t *src);
bool meta_push(meta_pattern_t *m, meta_step_t s);
