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

/// @brief A mono or stereo live input, read by ring index and a run-time channel count, as the pitch shifter's legacy
/// arithmetic reads it until the shifter's components replace it.
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

	/// @brief The frame's left (or only) channel at ring index @p pos, as a word.
	[[nodiscard]] int32_t word_l(uint32_t pos) const {
		const dsp::fixed::pitch_shift::RingPosition p{pos};
		return stereo_ != nullptr ? stereo_->history().at(p).l.raw() : mono_->history().at(p).raw();
	}
	/// @brief The frame's right channel at ring index @p pos, as a word.
	[[nodiscard]] int32_t word_r(uint32_t pos) const {
		return stereo_->history().at(dsp::fixed::pitch_shift::RingPosition{pos}).r.raw();
	}

	/// @brief The top half of the left (or only) channel at ring index @p pos.
	[[nodiscard]] int16_t high_l(uint32_t pos) const {
		const dsp::fixed::pitch_shift::RingPosition p{pos};
		return stereo_ != nullptr ? stereo_->history().high_l(p).raw() : mono_->history().high_l(p).raw();
	}
	/// @brief The top half of the right channel at ring index @p pos.
	[[nodiscard]] int16_t high_r(uint32_t pos) const {
		return stereo_->history().high_r(dsp::fixed::pitch_shift::RingPosition{pos}).raw();
	}

	[[nodiscard]] int32_t folded_high_word(uint32_t pos) const {
		const dsp::fixed::pitch_shift::RingPosition p{pos};
		return stereo_ != nullptr ? stereo_->history().folded_high_word(p) : mono_->history().folded_high_word(p);
	}

	[[nodiscard]] uint8_t percussiveness_at(uint32_t entry) const {
		return stereo_ != nullptr ? stereo_->history().percussiveness_at(entry)
		                          : mono_->history().percussiveness_at(entry);
	}

	[[nodiscard]] dsp::fixed::pitch_shift::MovingAverages moving_averages(uint32_t start, int32_t length) const {
		const dsp::fixed::pitch_shift::RingPosition p{start};
		return stereo_ != nullptr ? stereo_->history().moving_averages(p, length)
		                          : mono_->history().moving_averages(p, length);
	}

	[[nodiscard]] bool written(uint32_t pos) const {
		const dsp::fixed::pitch_shift::RingPosition p{pos};
		return stereo_ != nullptr ? stereo_->history().written(p) : mono_->history().written(p);
	}

private:
	LegacyInputView(MonoLiveInput* mono, StereoLiveInput* stereo) : mono_(mono), stereo_(stereo) {}
	MonoLiveInput* mono_;
	StereoLiveInput* stereo_;
};

} // namespace deluge::synth
