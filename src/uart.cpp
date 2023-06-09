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

#include "uart.h"
#include <math.h>
#include "functions.h"

extern "C" {
#include "sio_char.h"
}

namespace Uart {

void println(char const* output) {
#if ENABLE_TEXT_OUTPUT
	uartPrintln(output);
#endif
}

void println(int32_t number) {
#if ENABLE_TEXT_OUTPUT
	char buffer[12];
	intToString(number, buffer);
	uartPrintln(buffer);
#endif
}

void print(char const* output) {
#if ENABLE_TEXT_OUTPUT
	uartPrint(output);
#endif
}

void print(int32_t number) {
#if ENABLE_TEXT_OUTPUT
	char buffer[12];
	intToString(number, buffer);
	uartPrint(buffer);
#endif
}

} // namespace Uart
