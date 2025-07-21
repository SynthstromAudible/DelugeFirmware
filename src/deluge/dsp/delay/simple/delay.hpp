/*
 * Copyright (c) 2024 Katherine Whitlock
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
#include "buffer.hpp"
#include "definitions_cxx.hpp"
#include "dsp/interpolate/parameter.hpp"
#include "dsp_ng/core/types.hpp"
#include "model/types.hpp"
#include "util/fixedpoint_neon.h"
#include <argon.hpp>
#include <argon/vectorize/store_interleaved.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace deluge::dsp::delay::simple {

// A simple digital delay inspired by Ableton's Delay
class Delay {

	constexpr static size_t kNumSecsDelayMax = 5;
	constexpr static size_t kNumSamplesMainDelay = (5 * kSampleRate);
	constexpr static size_t kNumSamplesMax =
	    kNumSamplesMainDelay + (kNumSamplesMainDelay / 3.f); // 6.666 seconds maximum
public:
	/// @brief The mode of the Delay
	enum class Mode {
		Repitch, ///< Repitch on delay length change (akin to tape speed)
		Fade,    ///< Fade previous delay on length change
		Jump,    ///< Hard cut to new delay
	};

	/// @brief The
	struct ChannelConfig {
		Milliseconds duration;   // ms for time, beat divisions for sync, internally stored as ms
		Percentage<float> nudge; // up to 0.33 (33%)

		// Comparison ops
		bool operator==(const ChannelConfig& b) const = default;
		bool operator!=(const ChannelConfig& b) const = default;
	};

	struct Config {
		bool channel_link = true; // uses first config for both channels
		ChannelConfig l_channel;  // stereo
		ChannelConfig r_channel;

		Percentage<float> feedback = 0.5f; // Defaults to 50% for linear decay rate

		bool freeze = false; //< Disable new input and feedback, cycling the current buffer contents infinitely

		// Standard 2 param bandpass
		Frequency<float> filter_cutoff = 1000.f; // Hz
		QFactor<float> filter_width = 9.0f;      // Q

		// Mod block
		Frequency<float> lfo_rate = 0.5f;
		Percentage<float> lfo_filter_depth = 0.f;
		// float lfo_time_depth = 0.f;

		bool ping_pong = false;
	};

	void setConfig(const Config& config) {
		this->config_ = config;
		if (this->config_.channel_link) {
			this->config_.r_channel = this->config_.l_channel;
		}
	}

	void processBlock(std::span<q31_t> buffer) {
		interpolated::Parameter feedback{old_config_.feedback.value, config_.feedback.value, buffer.size()};

		interpolated::Parameter filter_cutoff{old_config_.filter_cutoff.value, config_.filter_cutoff.value,
		                                      buffer.size()};
		interpolated::Parameter filter_width{old_config_.filter_width.value, config_.filter_width.value, buffer.size()};

		interpolated::Parameter lfo_rate{old_config_.lfo_rate.value, config_.lfo_rate.value, buffer.size()};
		interpolated::Parameter lfo_filter_depth{old_config_.lfo_filter_depth.value, config_.lfo_filter_depth.value,
		                                         buffer.size()};
		// interpolated::Parameter lfo_time_depth{old_config_.lfo_time_depth, new_config_.lfo_time_depth,
		// buffer.size()};

		// l channel config has changed, so swap
		if (old_config_.l_channel != config_.l_channel) {
			auto& old_buffer = bufferLeft();
			swapBufferLeft();
			auto& buffer = bufferLeft();

			buffer.set_size(config_.l_channel.duration                                                    // main
			                + static_cast<size_t>(config_.l_channel.duration * config_.l_channel.nudge)); // nudge

			if (mode_ == Mode::Repitch) {
				buffer.CopyFromRepitch(old_buffer);
			}
			else if (mode_ == Mode::Fade) {
				buffer.CopyFrom(old_buffer);
				buffer.ApplyGainRamp(processors::GainRamp<float, float>{1.f, 0.f});
			}
			else if (mode_ == Mode::Jump) {
				buffer.CopyFrom(old_buffer);
			}
		}

		if (old_config_.r_channel != config_.r_channel) {
			// r channel config has changed, so swap
			auto& old_buffer = bufferRight();
			swapBufferRight();
			auto& buffer = bufferRight();

			buffer.set_size(config_.r_channel.duration                                                    // main
			                + static_cast<size_t>(config_.r_channel.duration * config_.r_channel.nudge)); // nudge

			if (mode_ == Mode::Repitch) {
				buffer.CopyFromRepitch(old_buffer);
			}
			else if (mode_ == Mode::Fade) {
				buffer.CopyFrom(old_buffer);
				buffer.ApplyGainRamp(processors::GainRamp<float, float>{1.f, 0.f});
			}
			else if (mode_ == Mode::Jump) {
				buffer.CopyFrom(old_buffer);
			}
		}

		// when the buffer is frozen, no writing is peformed, only advancement of the rec/play head
		if (config_.freeze) {
			for (auto& [left_out, right_out] : argon::vectorize::store_interleaved(buffer)) {
				auto&& [buffer_left, buffer_right] = buffers();

				left_out = buffer_left.ReadSIMD().ConvertTo<q31_t, 31>();   // To Q31
				right_out = buffer_right.ReadSIMD().ConvertTo<q31_t, 31>(); // To Q31

				buffer_left.Advance(4);
				buffer_right.Advance(4);
			}
			return;
		}

		// Otherwise...
		for (auto& [left, right] : argon::vectorize::store_interleaved(buffer)) {
			Argon<float> feedback_value = feedback.NextSIMD();
			Argon<float> filter_cutoff_value = filter_cutoff.NextSIMD();
			Argon<float> filter_width_value = filter_width.NextSIMD();
			Argon<float> lfo_rate_value = lfo_rate.NextSIMD();
			Argon<float> lfo_filter_depth_value = lfo_filter_depth.NextSIMD();

			auto&& [buffer_left, buffer_right] = buffers();
			auto input_left = left.ConvertTo<float, 31>();
			auto input_right = right.ConvertTo<float, 31>();

			left = buffer_left.ReadSIMD().ConvertTo<q31_t, 31>(); // To Q31
			buffer_left.Advance(4);
			buffer_left.WriteSIMD(input_left, feedback_value);

			right = buffer_right.ReadSIMD().ConvertTo<q31_t, 31>(); // To Q31
			buffer_right.Advance(4);
			buffer_right.WriteSIMD(input_right, feedback_value);
		}
		old_config_ = config_;
	}

	std::tuple<Buffer<kNumSamplesMax>&, Buffer<kNumSamplesMax>&> buffers() { return {bufferLeft(), bufferRight()}; }

	Buffer<kNumSamplesMax>& bufferLeft() { return l_buffers_[l_buffer_idx_]; }
	Buffer<kNumSamplesMax>& bufferRight() { return l_buffers_[l_buffer_idx_]; }

private:
	void swapBufferLeft() { l_buffer_idx_ = !l_buffer_idx_; }
	void swapBufferRight() { r_buffer_idx_ = !r_buffer_idx_; }

	constexpr size_t calcBufferSize(size_t time_ms) {
		return (time_ms * kSampleRate) / 1000; // divide by 1000 due to ms (i.e. * 0.001)
	}

	std::array<Buffer<kNumSamplesMax>, 2> l_buffers_; // 3D array.
	std::array<Buffer<kNumSamplesMax>, 2> r_buffers_; // 3D array.

	size_t l_buffer_idx_ = 0; // Which buffer is currently selected. This is used for
	size_t r_buffer_idx_ = 0; // when the delay time changes and how to handle it depending on mode

	// config
	Config old_config_;
	Config config_;

	Mode mode_ = Mode::Repitch;
};
} // namespace deluge::dsp::delay::simple
