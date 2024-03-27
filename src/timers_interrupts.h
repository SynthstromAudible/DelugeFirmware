/*
 * Copyright © 2014-2023 Mark Adams
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

#ifndef DELUGE_TIMERS_INTERRUPTS_H
#define DELUGE_TIMERS_INTERRUPTS_H

#include "RZA1/system/r_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

void clearIRQInterrupt(int irqNumber);

/// sets up a timer with an interrupt and handler but does not enable the timer
/// Valid scale values are 1, 4, 16, 64 for all timers 0-4. Timer 1, 3, 4 support 256. Timer 2, 3, 4 support 1024.
/// resulting frequency is 33.33MHz/scale
void setupTimerWithInterruptHandler(int timerNo, int scale, void (*handler)(uint32_t intSense), uint8_t priority);
void setupRunningClock(int timer, int preScale);
void setupAndEnableInterrupt(void (*handler)(uint32_t), uint16_t interruptID, uint8_t priority);
#ifdef __cplusplus
}
#endif

#endif // DELUGE_TIMERS_INTERRUPTS_H
