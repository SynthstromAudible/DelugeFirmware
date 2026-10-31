/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */
#pragma once

/// @file
/// @brief Host stand-ins for the ports a synth::Voice's sources play through: a sample held in memory, and a DX7
/// voice and live input that make no sound.

#include "synth/sources/dx7/dx_voice_port.h"
#include "synth/sources/input/audio_input_port.h"
#include "synth/sources/sample/sample_playback_port.h"

#include <cstdint>
#include <span>
#include <vector>

namespace deluge::synth::test {

/// @brief A mono 16-bit sample of @p frames frames, read at the rate the voice asks for, with no loop.
class FakeSamplePort final : public SamplePlaybackPort {
public:
	FakeSamplePort() = default;
	explicit FakeSamplePort(int32_t frames) : frames_{frames} {}

	/// Frames played, Q8.24 (2^24 per frame).
	[[nodiscard]] int64_t position() const { return position_; }

	[[nodiscard]] int32_t num_channels() const override { return 1; }
	[[nodiscard]] int32_t sample_rate() const override { return 44100; }
	[[nodiscard]] int32_t neutral_phase_increment() const override { return kMaxSampleValue; }
	[[nodiscard]] int32_t sample_length_at_system_sample_rate() const override { return frames_; }
	[[nodiscard]] bool is_stretch_repeat_mode() const override { return false; }
	[[nodiscard]] bool pitch_and_speed_independent() const override { return false; }
	[[nodiscard]] int32_t time_stretch_amount() const override { return 0; }
	[[nodiscard]] uint32_t sequence_sync_length_ticks() const override { return 0; }
	[[nodiscard]] LoopType looping_type() const override { return LoopType::NONE; }
	[[nodiscard]] LoopPoints loop_points() const override { return {.start = 0, .end = frames_ * kBytesPerFrame}; }
	[[nodiscard]] uint64_t time_per_internal_tick_big() const override { return 0; }
	[[nodiscard]] int64_t synced_num_samples_in() const override { return 0; }
	[[nodiscard]] int32_t num_samples_lagging_behind_sync() const override { return 0; }
	[[nodiscard]] uint32_t adjust_pitch_to_correct_drift_from_sync(uint32_t phase_increment) const override {
		return phase_increment;
	}
	[[nodiscard]] int32_t interpolation_buffer_size(uint32_t /*phase_increment*/) const override { return 2; }
	[[nodiscard]] uint32_t pending_samples_late() const override { return 0; }
	[[nodiscard]] bool done_first_render_yet() const override { return rendered_; }
	[[nodiscard]] int32_t bytes_left_to_end() const override {
		return static_cast<int32_t>(frames_ - (position_ >> 24)) * kBytesPerFrame;
	}
	[[nodiscard]] int32_t bytes_per_frame() const override { return kBytesPerFrame; }
	bool note_on(uint32_t /*samples_late*/, int32_t /*priority_rating*/) override {
		position_ = 0;
		rendered_ = false;
		return true;
	}
	bool note_off_at_loop_end(int32_t /*priority_rating*/) override { return true; }
	void unassign(bool /*deleting_song*/) override {}
	LateStartOutcome attempt_late_start(int64_t /*raw_samples_late*/, int32_t /*num_samples*/) override {
		return LateStartOutcome::OK;
	}
	bool possibly_set_up_cache(int32_t /*phase_increment*/, int32_t /*time_stretch_ratio*/, int32_t /*priority_rating*/,
	                           LoopType /*looping_type*/) override {
		return true;
	}
	/// A square at a quarter of the frame rate, scaled by the amplitude; false once the last frame has been read.
	bool render(int32_t* buffer, int32_t num_samples, LoopType /*looping_type*/, int32_t phase_increment,
	            int32_t /*time_stretch_ratio*/, int32_t source_amplitude, int32_t amplitude_increment,
	            int32_t /*interpolation_buffer_size*/, int32_t /*priority_rating*/) override {
		rendered_ = true;
		for (int32_t i = 0; i < num_samples; i++) {
			if ((position_ >> 24) >= frames_) {
				return false;
			}
			const int32_t level = ((position_ >> 25) & 1) != 0 ? 1 << 28 : -(1 << 28);
			buffer[i] += static_cast<int32_t>((static_cast<int64_t>(level) * source_amplitude) >> 31);
			source_amplitude += amplitude_increment;
			position_ += static_cast<uint32_t>(phase_increment);
		}
		return true;
	}

private:
	static constexpr int32_t kBytesPerFrame = 2;
	int32_t frames_{44100};
	int64_t position_{};
	bool rendered_{};
};

/// @brief A DX7 voice port that never sounds.
class FakeDxPort final : public DxVoicePort {
public:
	bool note_on(void* /*patch*/, int /*midinote*/, int /*velocity*/) override { return true; }
	void keyup() override {}
	void unassign() override {}
	std::span<const int32_t> render(int32_t /*num_samples*/, uint32_t /*phase_increment*/, int32_t /*amp_mod_param*/,
	                                bool /*patch_changed*/, int /*midinote*/) override {
		return {};
	}
};

/// @brief A live input with nothing plugged in.
class FakeInputPort final : public AudioInputPort {
public:
	[[nodiscard]] uint32_t sample_time() const override { return 0; }
	[[nodiscard]] bool stereo_input_available() const override { return false; }
	[[nodiscard]] const int32_t* ring_read_pos() const override { return ring_.data(); }
	[[nodiscard]] const int32_t* ring_end() const override { return ring_.data() + ring_.size(); }
	[[nodiscard]] LiveInputBuffer* buffer_for_channel(InputChannel /*channel*/, bool /*may_create*/) override {
		return nullptr;
	}
	void report_hops(int32_t /*hops_ended*/) override {}

private:
	std::vector<int32_t> ring_ = std::vector<int32_t>(256);
};

} // namespace deluge::synth::test
