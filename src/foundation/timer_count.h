/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

/// foundation/timer_count.h — conversions between the board's hardware timer
/// counts and real time.
///
/// Foundation tier: pure math over the board crystal frequency (board_config.h's
/// XTAL_SPEED_MHZ), no other dependencies — so the HAL (OLED/USB timing) and the
/// app can both use them. Split out of the old deluge/util/cfunctions.h. (The
/// busy-wait delayMS/delayUS wrappers, which go through the libdeluge clock
/// boundary, stay above the foundation in util/cfunctions.h.)

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t fastTimerCountToUS(uint32_t timerCount);
uint32_t usToFastTimerCount(uint32_t us);
uint32_t msToSlowTimerCount(uint32_t ms);
uint32_t superfastTimerCountToUS(uint32_t timerCount);
uint32_t superfastTimerCountToNS(uint32_t timerCount);

#ifdef __cplusplus
}
#endif
