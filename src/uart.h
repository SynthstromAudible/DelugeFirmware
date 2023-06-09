/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#ifndef UART_H_
#define UART_H_

#include "r_typedefs.h"

namespace Uart {
void setBaudRate(uint8_t scifID, uint32_t baudRate);
void print(char const* output);
void println(char const* output);
void println(int32_t number);
void printlnfloat(float number);
void printfloat(float number);
void print(int32_t number);
unsigned int getTxBufferFullness(uint8_t scifID);
} // namespace Uart

#endif /* UART_H_ */
