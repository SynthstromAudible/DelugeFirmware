/*
 * Copyright © 2024 The Deluge Community
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

#include "dsp/sid/sid_waves.h"
#include "io/debug/log.h"
#include <array>
#include <cstdint>

namespace deluge {
namespace dsp {
namespace sid {

// Simple function to test the SID pulse wave with different pulse width values
void testSidPulse() {
	// Create a small buffer for testing
	std::array<int32_t, 16> testBuffer = {0};
	uint32_t phase = 0;
	const uint32_t phaseIncrement = 0x10000; // Medium frequency

	// Test different pulse width values
	const uint32_t testPulseWidths[] = {
	    0,     // 0% - should be silent
	    1,     // Minimum non-zero - should produce sound
	    0x800, // 50% - square wave
	    0xFFF, // 100% - should be silent
	    0x100, // ~6.25% - narrow pulse
	    0xF00  // ~93.75% - wide pulse
	};

	D_PRINTLN("SID Pulse wave test:");
	for (const auto& pw : testPulseWidths) {
		// Clear the buffer
		for (auto& sample : testBuffer) {
			sample = 0;
		}

		// Reset phase for consistent testing
		phase = 0;

		// Render some samples with this pulse width
		renderSidPulse(0x7FFFFFFF, testBuffer.data(), testBuffer.data() + 16, phaseIncrement, phase, pw, true, 0);

		// Check if we have non-zero output (sound) or zero output (silence)
		bool hasSoundOutput = false;
		for (const auto& sample : testBuffer) {
			if (sample != 0) {
				hasSoundOutput = true;
				break;
			}
		}

		// Log the result
		D_PRINTLN("  Pulse width 0x%X (%d) - %s", pw, pw, hasSoundOutput ? "Producing sound" : "Silent");
	}
}

// Function to initialize SID wave tables and other resources
void initSID() {
	// Initialize the SID wave tables
	initSidWaveTables();

	// Test the SID pulse wave implementation
	testSidPulse();

	// Log that SID initialization is complete
	D_PRINTLN("SID wave tables initialized");
}

} // namespace sid
} // namespace dsp
} // namespace deluge
