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

#include "chainload.h"
#include "RZA1/mtu/mtu.h"
#include "definitions_cxx.hpp"

static void chainloader(uint32_t code_start, uint32_t code_size, uint32_t code_exec, char* buf) {
	int32_t loop_num = (((uint32_t)code_size + 3) / (sizeof(uint32_t)));

	uint32_t* psrc = (uint32_t*)buf;
	uint32_t* pdst = (uint32_t*)code_start;
	uint32_t* pdst2 = (uint32_t*)(code_start + UNCACHED_MIRROR_OFFSET);

	for (int32_t i = 0; i < loop_num; i++) {
		pdst[i] = psrc[i];
		pdst2[i] = psrc[i];
	}

	((void (*)(void))code_exec)(); // lessgo
}

void chainload_from_buf(uint8_t* buffer, int buf_size) {
	uint32_t user_code_start = *(uint32_t*)(buffer + OFF_USER_CODE_START);
	uint32_t user_code_end = *(uint32_t*)(buffer + OFF_USER_CODE_END);
	uint32_t user_code_exec = *(uint32_t*)(buffer + OFF_USER_CODE_EXECUTE);

	int32_t code_size = (int32_t)(user_code_end - user_code_start);
	if (code_size > buf_size) {
		return;
	}

	char* funcbuf = (char*)user_code_end; // this should be a nice spot
	char* from = (char*)chainloader;
	for (int i = 0; i < 128; i++) {
		(funcbuf + UNCACHED_MIRROR_OFFSET)[i] = from[i];
	}

	disableTimer(TIMER_MIDI_GATE_OUTPUT);
	disableTimer(TIMER_SYSTEM_SLOW);
	disableTimer(TIMER_SYSTEM_FAST);
	disableTimer(TIMER_SYSTEM_SUPERFAST);

	auto ptr = (void (*)(uint32_t, uint32_t, uint32_t, char*))funcbuf;
	ptr(user_code_start, code_size, user_code_exec, (char*)buffer);
}
