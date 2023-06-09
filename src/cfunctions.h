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

#ifndef CFUNCTIONS_H_
#define CFUNCTIONS_H_

#include "r_typedefs.h"

#define getMin(a, b) (((a) < (b)) ? (a) : (b))
#define getMax(a, b) (((a) > (b)) ? (a) : (b))

int getNumDecimalDigits(uint32_t number);
void intToString(int32_t number, char* buffer, int minNumDigits);
void floatToString(float number, char* __restrict__ buffer, int minNumDecimalPlaces, int maxNumDecimalPlaces);
void slotToString(int slot, int subSlot, char* __restrict__ buffer, int minNumDigits);

uint32_t fastTimerCountToUS(uint32_t timerCount);
uint32_t usToFastTimerCount(uint32_t us);
uint32_t msToSlowTimerCount(uint32_t ms);
uint32_t superfastTimerCountToUS(uint32_t timerCount);
uint32_t superfastTimerCountToNS(uint32_t timerCount);

void delayMS(uint32_t ms);
void delayUS(uint32_t us);

#endif /* CFUNCTIONS_H_ */
