//
// Copyright (c) 2022 Greg Wuller.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "mode_common.h"

u16 calc_clock_frequency(u16 rate) {
  // assumes rate [8-1280]
  if (rate < 8) {
    rate = 8;
  }
  return rate; // 8hz - 1288hz
}
