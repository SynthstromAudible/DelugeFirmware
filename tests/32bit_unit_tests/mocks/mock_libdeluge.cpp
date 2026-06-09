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

// Host stubs for the libdeluge boundary functions referenced by the unit-tested
// firmware source (the memory allocator, string/util helpers, chainload). The
// memory tests exercise MemoryRegion against their own buffers — the global
// allocator's regions are degenerate here, matching the zeroed linker symbols in
// mock_defines.h — so these only need to satisfy the linker.

#include "libdeluge/clock.h"
#include "libdeluge/memory.h"
#include "libdeluge/system.h"

uintptr_t deluge_memory_external_end(void) {
	return 0;
}
uintptr_t deluge_memory_internal_begin(void) {
	return 0;
}

uint64_t deluge_clock_now(void) {
	return 0;
}
void deluge_clock_delay_ms(uint32_t /*ms*/) {
}
void deluge_clock_delay_us(uint32_t /*us*/) {
}

void deluge_system_quiesce(void) {
}
