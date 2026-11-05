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

#include "dsp-next/fixed/pitch_shift/input_history.h"
#include "synth/sources/input/audio_input_port.h"
#include "synth/sources/input/live/live_input.h"

#include <cstdint>
#include <type_traits>

namespace deluge::synth {

/// @brief A mono or stereo live input, found by its run-time channel, as the pitch shifter's legacy render loop finds
/// it until the shifter's components replace it.
class LegacyInputView {
public:
	/// @brief The live input of @p channel from @p port, made if @p may_create; empty if there is none.
	[[nodiscard]] static LegacyInputView of(AudioInputPort& port, InputChannel channel, bool may_create) {
		if (channel == InputChannel::STEREO) {
			return LegacyInputView{nullptr, port.stereo_input(may_create)};
		}
		return LegacyInputView{port.mono_input(channel, may_create), nullptr};
	}

	[[nodiscard]] explicit operator bool() const { return mono_ != nullptr || stereo_ != nullptr; }

	/// @brief The input's history, if it is of @p Frame.
	template <typename Frame>
	[[nodiscard]] const dsp::fixed::pitch_shift::InputHistory<Frame>* history() const {
		if constexpr (std::is_same_v<Frame, dsp::StereoSample>) {
			return stereo_ != nullptr ? &stereo_->history() : nullptr;
		}
		else {
			return mono_ != nullptr ? &mono_->history() : nullptr;
		}
	}

	void ingest(int32_t n, AudioInputPort& port) const {
		if (stereo_ != nullptr) {
			stereo_->ingest(n, port);
		}
		else {
			mono_->ingest(n, port);
		}
	}

	[[nodiscard]] uint32_t frames_written() const {
		return stereo_ != nullptr ? stereo_->history().frames_written() : mono_->history().frames_written();
	}

	[[nodiscard]] uint8_t percussiveness_at(uint32_t entry) const {
		return stereo_ != nullptr ? stereo_->history().percussiveness_at(entry)
		                          : mono_->history().percussiveness_at(entry);
	}

private:
	LegacyInputView(MonoLiveInput* mono, StereoLiveInput* stereo) : mono_(mono), stereo_(stereo) {}
	MonoLiveInput* mono_;
	StereoLiveInput* stereo_;
};

} // namespace deluge::synth
