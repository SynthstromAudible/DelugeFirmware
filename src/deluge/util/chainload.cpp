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
#include "definitions.h"
#include "libdeluge/system.h"

extern uint32_t spareRenderingBuffer[][SSI_TX_BUFFER_NUM_SAMPLES];
#if defined(__arm__)
extern "C" void v7_dma_flush_range(uint32_t start, uint32_t end);
#endif

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
	ENTER_CRITICAL_SECTION();
#endif
	// Disable timers
	deluge_system_quiesce();
	uint8_t* funcbuf = reinterpret_cast<uint8_t*>(spareRenderingBuffer);

#if defined(__arm__)
	// The chainloader below runs with the MMU and caches disabled, storing straight to
	// physical RAM - but its per-word DCCMVAU maintenance writes back any line still dirty
	// in the (disabled, not emptied) data cache, clobbering words it just stored. All RAM
	// here is mapped write-back, so clean & invalidate everything the chainloader touches
	// while the MMU is still on: the new image, the copy destination, and funcbuf - which
	// is an audio rendering buffer, so it is full of dirty lines whenever audio has been
	// rendered since boot (chainloading used to hang precisely then).
	v7_dma_flush_range((uint32_t)buffer, (uint32_t)buffer + (uint32_t)buf_size);
	v7_dma_flush_range(user_code_start, user_code_start + (uint32_t)code_size + 4);
	v7_dma_flush_range((uint32_t)funcbuf, (uint32_t)funcbuf + 1024);

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
