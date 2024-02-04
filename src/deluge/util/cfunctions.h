/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t getNumDecimalDigits(uint32_t number);
void intToString(int32_t number, char* buffer, int32_t minNumDigits);
void floatToString(float number, char* __restrict__ buffer, int32_t minNumDecimalPlaces, int32_t maxNumDecimalPlaces);
void slotToString(int32_t slot, int32_t subSlot, char* __restrict__ buffer, int32_t minNumDigits);
uint32_t fastTimerCountToUS(uint32_t timerCount);
uint32_t usToFastTimerCount(uint32_t us);
uint32_t msToSlowTimerCount(uint32_t ms);
uint32_t superfastTimerCountToUS(uint32_t timerCount);
uint32_t superfastTimerCountToNS(uint32_t timerCount);

void delayMS(uint32_t ms);
void delayUS(uint32_t us);

#ifdef __cplusplus
}
#endif
