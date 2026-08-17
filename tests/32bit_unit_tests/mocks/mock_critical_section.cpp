/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

// Host bodies for the interrupt-masking C ABI that libdeluge declares and the BSP normally owns.
//
// deluge_resource's masking discipline (crates/deluge_resource/src/sync.rs) calls all three across
// the C ABI on every acquire/release, so any host driver that links a Rust staticlib re-exporting
// deluge_resource needs bodies for them even when the spec itself never contends.
//
// The no-op/false bodies are exact rather than merely adequate: these drivers are single-threaded
// and never run in ISR context, so there is nothing to mask and no interrupt to report. Matches
// tests/unit/mocks/hal_mocks.cpp's own deluge_in_interrupt.

#include "libdeluge/system.h"
#include <cstdint>

extern "C" {

void ENTER_CRITICAL_SECTION() {
}

void EXIT_CRITICAL_SECTION() {
}

bool deluge_in_interrupt() {
	return false;
}

/// Mirrors deluge_in_interrupt above: these drivers never run from an ISR, so they are never
/// the audio context either. Consequence: the resource manager always masks under the specs,
/// which is what the masked-path assertions expect.
bool deluge_in_audio_context() {
	return false;
}

/// The clock deluge_resource measures loader service latency with — in the firmware it is
/// `AudioEngine::audioSampleTimer` (see src/deluge/io/debug/resource_clock.cpp), which these drivers
/// do not link. A monotonic counter satisfies the manager's only requirement (that successive reads
/// do not go backwards); no spec asserts on the latency figures it produces.
uint32_t deluge_debug_now_frames() {
	static uint32_t now = 1;
	return ++now;
}
}
