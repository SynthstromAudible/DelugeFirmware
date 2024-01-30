/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute
 * it and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "chainload.h"
#include "RZA1/intc/devdrv_intc.h"
#include "RZA1/mtu/mtu.h"
#include "definitions_cxx.hpp"
#include <cstddef>

extern uint32_t spareRenderingBuffer[][SSI_TX_BUFFER_NUM_SAMPLES];

void chainload_from_buf(uint8_t* buffer, int buf_size) {
	uint32_t user_code_start = *(uint32_t*)(buffer + OFF_USER_CODE_START);
	uint32_t user_code_end = *(uint32_t*)(buffer + OFF_USER_CODE_END);
	uint32_t user_code_exec = *(uint32_t*)(buffer + OFF_USER_CODE_EXECUTE);

	int32_t code_size = (int32_t)(user_code_end - user_code_start);
	if (code_size > buf_size) {
		return;
	}

	// Disable interrupts so we don't get interrupted during the chainload
#if defined(__arm__)
	asm volatile("CPSID   i");
#endif
	// Disable timers
	disableTimer(TIMER_MIDI_GATE_OUTPUT);
	disableTimer(TIMER_SYSTEM_SLOW);
	disableTimer(TIMER_SYSTEM_FAST);
	disableTimer(TIMER_SYSTEM_SUPERFAST);
	uint8_t* funcbuf = reinterpret_cast<uint8_t*>(spareRenderingBuffer);

#if defined(__arm__)
	// Jump to the chainloader
	asm volatile("mov r0,%0\n\t"
	             "mov r1,%1\n\t"
	             "mov r2,%2\n\t"
	             "mov r3,%3\n\t"
	             "mov r4,%4\n\t"
	             "blx deluge_chainload"
	             :
	             : "r"(user_code_start), "r"(code_size), "r"(user_code_exec), "r"(buffer), "r"(funcbuf)
	             : "r0", "r1", "r2", "r3", "r4");
#endif
}
