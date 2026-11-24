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
#include "dsp/core/fixed_point.h"
#include "dsp/fixed/sample_read/frame_source.h"
#include "dsp/fixed/timestretch/hop_planner.h"
#include "synth/sources/sample/perc_fill.h"

#include <cstdint>
#include <optional>
#include <span>

namespace deluge::synth {

/// @file
/// @brief How the time stretcher reaches the sample it stretches: its own port, implemented app-side over a
/// voice-sample, its sample, its two read heads and its playback guide (decision 1 of
/// docs/dev/time_stretcher_design.md, section 3). Fourteen operations, each forced by a behaviour the goldens pin.

/// @brief The source's layout and markers, in frames where the stretcher reads them as frames.
struct SourceFacts {
	int32_t length_frames{};
	int32_t channels{};
	int32_t byte_depth{};
	int32_t data_start_byte{};
	int32_t start_marker_frame{}; ///< B8: the first frame played, in the play direction.
	int32_t start_byte{};         ///< S6, and a fudge's start: the guide's start byte after a loop.
	int32_t end_or_loop_byte{};   ///< Q4.
	std::optional<dsp::fixed::timestretch::LoopAhead>
	    loop_ahead;        ///< B7, while the stretcher-made loop has a pre-margin.
	int32_t sample_rate{}; ///< A3 and the hop-back reach.
};

/// @brief How a seat went (H1): started; not started, its chunk not ready; started, but holding no chunk.
enum class SeatResult : uint8_t { STARTED, NOT_READY, STARTED_EMPTY };

/// @brief A head's level for a read, and its step before each frame.
struct HeadGain {
	dsp::Q<31> level;
	dsp::Q<31> step;
};

/// @brief A head's reference byte (A0, with its interpolation compensation) and its phase (A5).
struct HeadMark {
	int32_t play_byte;
	dsp::UQ<24> phase;
};

/// @brief A reservation the port keeps for the stretcher; the stretcher owns the handle.
struct Reservation {
	void* held = nullptr;
};

/// @brief The stretcher's port to its sample.
class SampleStretchPort {
public:
	enum class Head : uint8_t { NEWER, OLDER };
	using Direction = dsp::fixed::sample_read::Direction;

	SampleStretchPort() = default;
	SampleStretchPort(const SampleStretchPort&) = delete;
	SampleStretchPort& operator=(const SampleStretchPort&) = delete;
	SampleStretchPort(SampleStretchPort&&) = delete;
	SampleStretchPort& operator=(SampleStretchPort&&) = delete;
	virtual ~SampleStretchPort() = default;

	// The source
	[[nodiscard]] virtual SourceFacts facts() const = 0;
	/// @brief S6's frames played since the start marker; nullopt unless the voice is synced with a clock running.
	[[nodiscard]] virtual std::optional<int64_t> synced_position() const = 0;
	/// @brief The frames readable from @p frame in @p direction, or nullopt where @p frame is not resident.
	[[nodiscard]] virtual std::optional<dsp::fixed::sample_read::FrameRun> run(int32_t frame,
	                                                                           Direction direction) const = 0;
	// The perc analysis
	[[nodiscard]] virtual std::optional<dsp::fixed::timestretch::PercWindow> perc_at(int32_t pixel,
	                                                                                 Direction direction) = 0;
	virtual PercFill fill_perc(int32_t from, int32_t to, Direction direction, int32_t max_frames) = 0;
	// The heads, by role
	/// @brief The spare head becomes a level copy of the newer, its width last time 0 (H1, R10).
	virtual void shadow() = 0;
	/// @brief Seats the spare at @p frame with @p phase (H1) and, only once it has started, swaps the roles.
	virtual SeatResult seat_and_swap(int32_t frame, dsp::UQ<24> phase, LoopType loop) = 0;
	/// @brief Mixes @p frames of @p head into @p out at @p gain (R11). False once the head has stopped.
	virtual bool read(Head head, std::span<dsp::Q<31>> out, int32_t frames, HeadGain gain) = 0;
	[[nodiscard]] virtual HeadMark mark(Head head) const = 0;
	/// @brief E1: the newer head's cursor equals the older's.
	[[nodiscard]] virtual bool cursors_meet() const = 0;
	// Residency the stretcher holds, by handle
	virtual void keep(Reservation& reservation, int32_t frame, Direction direction) = 0;
	virtual void let_go(Reservation& reservation) = 0;
	virtual void hold(PercChunk chunk) = 0;
	virtual void drop(PercChunk chunk) = 0;
};

} // namespace deluge::synth
