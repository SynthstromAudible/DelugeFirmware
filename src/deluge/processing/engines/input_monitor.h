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

#include "definitions_cxx.hpp"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/mix/output_fold.h"

#include <cstdint>
#include <limits>

namespace deluge::audio {

/// @brief What the output stage's monitoring decision reads: the recording source and mode, the sockets, and the
/// shape of the input the recorder has measured.
struct MonitorInputs {
	AudioInputChannel source = AudioInputChannel::NONE;  ///< The recorder's source.
	InputMonitoringMode mode = InputMonitoringMode::OFF; ///< The user's monitoring setting.
	bool usb_direct_monitoring = false;                  ///< The USB host plays the input back itself.
	bool line_in = false;                                ///< A line in is plugged in.
	bool headphones = false;                             ///< Headphones are plugged in.
	bool mic = false;                                    ///< An external mic is plugged in.
	bool recorder = false;                               ///< A recorder exists to measure the input.
	bool looks_differential = false; ///< The recorder finds the right channel is the left's inverse.
	bool no_right_channel = false;   ///< The recorder finds the right channel silent.
};

/// @brief Whether the output stage monitors, and how it folds the input.
struct MonitorDecision {
	bool on = false;                                              ///< Whether the input is added to the output.
	dsp::fixed::OutputFold fold = dsp::fixed::OutputFold::STEREO; ///< How it is folded.
};

/// @brief The output stage's monitoring for @p in.
/// @details USB input: on unless the mode is OFF or the host monitors it directly (no risk of feedback from the
/// internal mic, so SMART counts as on), folded stereo. Stereo or left input: SMART monitors with a line in or
/// headphones plugged in; otherwise only ON does. Monitoring with a recorder folds a line in DIFFERENTIAL when it looks
/// differential and LEFT_ONLY when it has no right channel, an external mic LEFT_ONLY when it has no right channel, and
/// the internal mic always LEFT_ONLY.
/// @param in The source, mode, sockets and input shape.
/// @return Whether to monitor, and the fold.
[[nodiscard]] constexpr MonitorDecision decide(const MonitorInputs& in) {
	using dsp::fixed::OutputFold;
	if (in.source == AudioInputChannel::USB) {
		return {.on = in.mode != InputMonitoringMode::OFF && !in.usb_direct_monitoring, .fold = OutputFold::STEREO};
	}
	MonitorDecision d{};
	if (in.source == AudioInputChannel::STEREO || in.source == AudioInputChannel::LEFT) {
		d.on =
		    in.mode == InputMonitoringMode::SMART ? (in.line_in || in.headphones) : in.mode == InputMonitoringMode::ON;
	}
	if (d.on && in.recorder) {
		if (in.line_in) {
			if (in.looks_differential) {
				d.fold = OutputFold::DIFFERENTIAL;
			}
			else if (in.no_right_channel) {
				d.fold = OutputFold::LEFT_ONLY;
			}
		}
		else if (in.mic) {
			if (in.no_right_channel) {
				d.fold = OutputFold::LEFT_ONLY;
			}
		}
		else {
			d.fold = OutputFold::LEFT_ONLY;
		}
	}
	return d;
}

/// @brief The engine's input monitoring: the output stage's decision, and the warm-up that keeps the codec's power-on
/// transient out of an audio track's monitoring.
class InputMonitor {
public:
	/// @brief About 500 ms after power-on for the codec's ADC to settle before monitoring starts.
	static constexpr uint32_t kWarmupSamples = kSampleRate / 2;
	/// @brief About 50 ms to fade monitoring in, so it does not switch on with a click.
	static constexpr uint32_t kFadeInSamples = kSampleRate / 20;

	/// @brief Takes the output stage's decision for this block.
	/// @param in The source, mode, sockets and input shape.
	void decide(const MonitorInputs& in) { decision_ = audio::decide(in); }

	/// @brief Whether the output stage adds the input.
	[[nodiscard]] bool on() const { return decision_.on; }

	/// @brief How the output stage folds the input.
	[[nodiscard]] dsp::fixed::OutputFold fold() const { return decision_.fold; }

	/// @brief Latches the warm-up over once @p now passes it, so a wrap of the sample timer cannot mute monitoring.
	/// @param now The engine's sample timer.
	void advance_time(uint32_t now) {
		if (!warmed_up_ && now >= kWarmupSamples + kFadeInSamples) {
			warmed_up_ = true;
		}
	}

	/// @brief Whether the warm-up is over.
	[[nodiscard]] bool warmed_up() const { return warmed_up_; }

	/// @brief A track's monitoring gain at sample time @p time: 0 during the warm-up, then a linear fade to full.
	/// @param time The engine's sample timer, plus an offset into the block.
	/// @return The gain, Q31.
	[[nodiscard]] dsp::q31 gain(uint32_t time) const {
		if (warmed_up_) {
			return std::numeric_limits<dsp::q31>::max();
		}
		if (time <= kWarmupSamples) {
			return 0;
		}
		const uint32_t elapsed = time - kWarmupSamples;
		if (elapsed >= kFadeInSamples) {
			return std::numeric_limits<dsp::q31>::max();
		}
		return static_cast<dsp::q31>((static_cast<uint64_t>(elapsed) * std::numeric_limits<dsp::q31>::max())
		                             / kFadeInSamples);
	}

private:
	MonitorDecision decision_{}; ///< This block's.
	bool warmed_up_ = false;     ///< Latched once the warm-up and fade are over.
};

} // namespace deluge::audio
