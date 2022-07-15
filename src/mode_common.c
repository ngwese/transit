//
// Copyright (c) 2022 Greg Wuller.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "mode_common.h"

u16 calc_clock_frequency(u16 rate) {
  // assumes rate [0-1280]
  return rate + 8; // 8hz - 1288khz
}
