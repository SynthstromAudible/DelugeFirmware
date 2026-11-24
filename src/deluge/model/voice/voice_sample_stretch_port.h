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
#include "synth/sources/sample/sample_stretch_port.h"

#include <cstdint>

class Sample;
class SamplePlaybackGuide;
class VoiceSample;

/// @brief What a stretched head's read takes from the voice's render, set per part of a block.
struct StretchReadParams {
	int32_t phase_increment = 0;
	int32_t interpolation_width = 0;
	int32_t kernel = 0;
	int32_t channels_after_condensing = 0;
	int32_t priority_rating = 1;
	LoopType loop = LoopType::NONE;
};

/// @brief The time stretcher's port over a voice-sample (decision 1 of docs/dev/time_stretcher_design.md): its sample,
/// its playback guide and its two read heads, the voice-sample's own reader the newer and its spare the older. Built on
/// the stack for each call. A port built for `end` alone may have no guide: `end` only lets go.
class VoiceSampleStretchPort final : public deluge::synth::SampleStretchPort {
public:
	VoiceSampleStretchPort(VoiceSample& voice, Sample& sample, SamplePlaybackGuide* guide,
	                       const StretchReadParams& params)
	    : voice_(&voice), sample_(&sample), guide_(guide), params_(params) {}

	/// @brief The next part's read parameters: the channels a read writes change when a cache is let go mid-block.
	void set_params(const StretchReadParams& params) { params_ = params; }

	[[nodiscard]] deluge::synth::SourceFacts facts() const override;
	[[nodiscard]] std::optional<int64_t> synced_position() const override;
	[[nodiscard]] std::optional<deluge::dsp::fixed::sample_read::FrameRun> run(int32_t frame,
	                                                                           Direction direction) const override;
	[[nodiscard]] std::optional<deluge::dsp::fixed::timestretch::PercWindow> perc_at(int32_t pixel,
	                                                                                 Direction direction) override;
	deluge::synth::PercFill fill_perc(int32_t from, int32_t to, Direction direction, int32_t max_frames) override;
	void shadow() override;
	deluge::synth::SeatResult seat_and_swap(int32_t frame, deluge::dsp::UQ<24> phase, LoopType loop) override;
	bool read(Head head, std::span<deluge::dsp::Q<31>> out, int32_t frames, deluge::synth::HeadGain gain) override;
	[[nodiscard]] deluge::synth::HeadMark mark(Head head) const override;
	[[nodiscard]] bool cursors_meet() const override;
	void keep(deluge::synth::Reservation& reservation, int32_t frame, Direction direction) override;
	void let_go(deluge::synth::Reservation& reservation) override;
	void hold(deluge::synth::PercChunk chunk) override;
	void drop(deluge::synth::PercChunk chunk) override;

private:
	VoiceSample* voice_;
	Sample* sample_;
	SamplePlaybackGuide* guide_;
	StretchReadParams params_;
};
