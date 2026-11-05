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

#include "definitions_cxx.hpp" // OscType
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/stereo_sample.h"

#include <type_traits>

/// @file
/// @brief The live histories an input oscillator owns, and the view of them its voices read.

namespace deluge::synth {

template <typename Frame>
class LiveInput;
/// The live history of the left or the right input channel.
using MonoLiveInput = LiveInput<dsp::Q<31>>;
/// The live history of both input channels.
using StereoLiveInput = LiveInput<dsp::StereoSample>;

/// @brief The histories an input oscillator's voices read, borrowed from its @ref LiveInputStore.
/// @details The mono history is the oscillator's channel's (left or right); a stereo oscillator has the stereo history
/// and the left one, which its shifters read when no stereo input was plugged in at their note's start. Either may be
/// absent: not an input type, or out of memory.
struct LiveInputs {
	MonoLiveInput* mono = nullptr;
	StereoLiveInput* stereo = nullptr;

	/// @brief The history a shifter of @p Frame reads.
	template <typename Frame>
	[[nodiscard]] LiveInput<Frame>* of() const {
		if constexpr (std::is_same_v<Frame, dsp::StereoSample>) {
			return stereo;
		}
		else {
			return mono;
		}
	}
};

/// @brief The live histories an input oscillator owns.
/// @details Made on the interaction tier (@ref for_type allocates) and handed to the render by @ref swap, which the
/// owner runs inside the host's audio-excluded section; the histories it swapped out are freed with the store they
/// went to, after the section. The render only reads @ref inputs and writes the histories.
class LiveInputStore {
public:
	LiveInputStore() = default;
	~LiveInputStore();
	LiveInputStore(const LiveInputStore&) = delete;
	LiveInputStore& operator=(const LiveInputStore&) = delete;
	LiveInputStore(LiveInputStore&& other) noexcept { swap(other); }
	LiveInputStore& operator=(LiveInputStore&&) = delete;

	/// @brief The histories an oscillator of @p type reads, made afresh: none unless @p type is an input type. A
	/// history that cannot be allocated is left absent.
	[[nodiscard]] static LiveInputStore for_type(OscType type);

	/// @brief Whether this holds exactly the histories @p type reads, every one present.
	[[nodiscard]] bool holds_for(OscType type) const;

	/// @brief Exchanges histories with @p other.
	void swap(LiveInputStore& other) noexcept;

	/// @brief The histories, for the voices to read.
	[[nodiscard]] const LiveInputs& inputs() const { return inputs_; }

private:
	LiveInputs inputs_{};
};

} // namespace deluge::synth
