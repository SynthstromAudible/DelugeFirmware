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

#pragma once

#include <cstdint>

namespace deluge::gui::ui {

/// @brief Milliseconds on legacy's UI clock: `AudioEngine::audioSampleTimer` at 44 samples a millisecond, as
///        `UITimerManager::setTimer` counts them, carried across the counter's wrap.
class SampleClockMs {
public:
	/// @brief The samples in a millisecond, as legacy's UI timers count them.
	static constexpr uint32_t kSamplesPerMs = 44;

	/// @brief The milliseconds counted by @p samples, the counter's value now.
	/// @details Monotonic: the samples since the last reading are added, so a wrap of the counter carries the count
	///          on rather than restarting it, and the samples short of a millisecond wait for the next reading.
	uint32_t advance(uint32_t samples) {
		pending_ += samples - last_;
		last_ = samples;
		ms_ += static_cast<uint32_t>(pending_ / kSamplesPerMs);
		pending_ %= kSamplesPerMs;
		return ms_;
	}

private:
	uint32_t last_ = 0;    ///< The counter at the last reading.
	uint64_t pending_ = 0; ///< Samples counted but short of a millisecond.
	uint32_t ms_ = 0;      ///< The milliseconds counted.
};

} // namespace deluge::gui::ui
