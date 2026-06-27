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

/// foundation/string_format.h — pure number→string formatting.
///
/// Foundation tier: no dependencies beyond libc, so the app, BSP and HAL may all
/// use it (the HAL's UART debug output formats numbers with intToString). Split
/// out of the old deluge/util/cfunctions.h, whose timer/delay helpers are board-
/// or clock-coupled and live elsewhere.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t getNumDecimalDigits(uint32_t number);
void intToString(int32_t number, char* buffer, int32_t minNumDigits);
void floatToString(float number, char* __restrict__ buffer, int32_t minNumDecimalPlaces, int32_t maxNumDecimalPlaces);
void slotToString(int32_t slot, int32_t subSlot, char* __restrict__ buffer, int32_t minNumDigits);

#ifdef __cplusplus
}
#endif
