/*
 * Copyright © 2026 Synthstrom Audible Limited
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

/// Differential test of waveRenderingFunctionGeneral — the argon-vectorised wavetable interpolation
/// at the heart of the basic-oscillator render (vector_rendering_function.h). Built two ways: native
/// ARM/NEON (under qemu) and the host x86 SIMDe backend. The outputs MUST match; a divergence is a
/// SIMDe-backend bug in the oscillator DSP (the suspected source of the host-sim over-gain). See
/// wave_run.sh.

#include "processing/vector_rendering_function.h"

#include <array>
#include <cstdint>
#include <cstdio>

namespace {
constexpr int kMaxMag = 9;
std::array<int16_t, (1 << kMaxMag) + 8> g_table; // a saw ramp; sized for the largest table + the +1 read
} // namespace

int main() {
	for (int mag = 6; mag <= 8; ++mag) {
		const int sz = 1 << mag;
		// deterministic saw ramp from -32768..+32767 across the table
		for (int i = 0; i < sz + 2; ++i) {
			g_table[i] = static_cast<int16_t>(-32768 + ((i % sz) * 65536 / sz));
		}
		// sweep phase increment (note pitch) and iterate phase (consecutive sample vectors)
		for (uint32_t inc = 1500000u; inc < 4000000000u; inc += 173000000u) {
			uint32_t phase = 0x12345678u;
			for (int rep = 0; rep < 8; ++rep) {
				auto [out, newPhase] =
				    waveRenderingFunctionGeneral(phase, static_cast<int32_t>(inc), 0u, g_table.data(), mag);
				auto a = out.to_array();
				printf("%d %d %d %d %u\n", a[0], a[1], a[2], a[3], newPhase);
				phase = newPhase;
			}
		}
	}
	return 0;
}
