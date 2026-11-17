/*
 * Copyright © 2026 Sean Ditny
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

#include <cstdint>

class Output;

// A press pulses its track's clips only once it outlasts a short press, so quick taps never flash the pulse.
class HoldPulse {
public:
	// `candidate` is the output the current press would pulse, or nullptr. Returns true when pulsing() changes.
	bool update(Output* candidate, uint32_t now, uint32_t holdTime) {
		if (candidate != candidate_) {
			candidate_ = candidate;
			since_ = now;
		}
		Output* target = (candidate_ != nullptr && now - since_ >= holdTime) ? candidate_ : nullptr;
		if (target == pulsing_) {
			return false;
		}
		pulsing_ = target;
		return true;
	}
	// The output to pulse, provided `candidate` is still the press being held.
	Output* pulsingFor(Output const* candidate) const { return (candidate == candidate_) ? pulsing_ : nullptr; }
	// Still waiting for a press to reach the hold time.
	bool waiting() const { return candidate_ != nullptr && pulsing_ == nullptr; }
	// Time left until the waiting press becomes a hold, in the same units as `now`; 0 if it is due or not waiting.
	uint32_t remaining(uint32_t now, uint32_t holdTime) const {
		uint32_t elapsed = now - since_;
		return (waiting() && elapsed < holdTime) ? holdTime - elapsed : 0;
	}
	bool tracking() const { return candidate_ != nullptr || pulsing_ != nullptr; }

private:
	Output* candidate_ = nullptr;
	Output* pulsing_ = nullptr;
	uint32_t since_ = 0;
};
