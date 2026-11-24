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
#include "dsp/fixed/timestretch/cache_tail.h"
#include "dsp/fixed/timestretch/crossfade.h"
#include "synth/sources/sample/hop_clock.h"
#include "synth/sources/sample/perc_fill.h"
#include "synth/sources/sample/sample_stretch_port.h"
#include "synth/sources/sample/stretch_context.h"
#include "synth/sources/sample/stretch_leases.h"
#include "synth/sources/sample/stretch_mix.h"
#include "synth/sources/sample/stretch_position.h"

#include <cstdint>
#include <optional>
#include <span>
#include <variant>

namespace deluge::synth {

/// @file
/// @brief The playback-side time stretcher (section 2.7 of docs/dev/time_stretcher_design.md): one per stretched
/// voice-sample, pooled. It owns the source position, the hop clock, the head phase, the perc-fill memo and every lease
/// handle it takes, drives the DSP components, and reaches the sample only through its port. It owns no read head: the
/// voice-sample owns both, and the stretcher owns which role each plays and how loud each is.

class SampleStretcher {
public:
	/// @brief How the source moves: the stretch ratio, the pitch's increment, the direction.
	struct Motion {
		Ratio ratio;
		Increment increment;
		Direction direction;
	};
	/// @brief A plain begin: the spare head a level copy of the newer, read once at level 0 (S1, X3).
	struct Plain {};
	/// @brief A plain begin whose older head is the cached audio it leaves (C1 to C3).
	struct FromCacheTail {
		dsp::fixed::timestretch::CacheWindow window;
		int32_t channels;
	};
	/// @brief A begin only to crossfade into the loop's pre-margin before a loop end @p output_frames_to_loop frames
	/// away (S2).
	struct Fudge {
		int32_t output_frames_to_loop;
	};
	using Opening = std::variant<Plain, FromCacheTail, Fudge>;

	/// @brief One part of a block: the voice cuts a block at Q4's loop end and copies each part to its cache (W1).
	struct Part {
		int32_t frames;
		Motion motion;
		LoopType loop;
		bool writing_cache;
		bool cache_in_use; ///< A cache is written or read: a loop crossfade's head cannot move to align (B7).
		int32_t out_channels;
	};
	/// @brief A part's outcome: whether the note ended, and the amplitude stepped on.
	struct PartOutcome {
		bool note_ended;
		StretchAmplitude amplitude;
	};

	/// @brief Begins stretching at @p position. False: a fudge's seat failed (the audio clip then lets the voice go).
	[[nodiscard]] bool begin(SampleStretchPort& port, StretchContext& context, SourcePosition position, Motion motion,
	                         LoopType loop, Opening opening);
	/// @brief The block's perc fill (P4), once a block before its parts.
	void prepare(SampleStretchPort& port, StretchContext& context, const Motion& motion, int32_t block_frames,
	             bool writing_cache);
	/// @brief One part's segments (S3, X2 to X4, Q1) into @p out, its frames times its output channels.
	[[nodiscard]] PartOutcome render(SampleStretchPort& port, StretchContext& context, const Part& part,
	                                 std::span<dsp::Q<31>> out, StretchAmplitude amplitude);
	/// @brief The voice's loop check moved the position (Q4); a hop now if the newer head has stopped (S5).
	void wrap_to(SampleStretchPort& port, StretchContext& context, SourcePosition position, const Part& part);
	/// @brief Gives back every reservation, perc chunk and tail buffer. The object is pooled and begun again in place;
	/// it allocates nothing.
	void end(SampleStretchPort& port, StretchContext& context);
	/// @brief Whether stretching may end now (E1).
	[[nodiscard]] bool may_end(const SampleStretchPort& port, Ratio ratio, bool synced, int32_t sync_lag) const;
	[[nodiscard]] int32_t frame(Direction direction) const { return position_.frame(direction); }
	[[nodiscard]] SourcePosition position() const { return position_; }
	[[nodiscard]] bool newer_reading() const;
	/// @brief Either head reads: a block whose cache is written takes zeros when neither does.
	[[nodiscard]] bool sounding() const;
	[[nodiscard]] bool is_fudge() const { return fudge_; }
	[[nodiscard]] const HopClock& clock() const { return clock_; }

private:
	struct SpareHead {}; ///< The older head is the voice-sample's spare.
	/// The older head is still in the newer's place: a hop past the end leaves the newer fading there, unseated (B6).
	struct NewerSlot {};
	using Older = std::variant<SpareHead, NewerSlot, dsp::fixed::timestretch::CacheTail>;
	struct Shadowed {};  ///< A plain begin before its first segment ends (X3).
	struct Solo {};      ///< The newer head reads alone.
	struct Crossfading { ///< Both read; the older fades out.
		dsp::fixed::timestretch::CrossfadeRamp ramp;
		Older older;
		std::optional<TailLoan> tail;
	};
	struct Fading { ///< The newer has stopped; the older fades out alone.
		dsp::fixed::timestretch::CrossfadeRamp ramp;
		Older older;
		std::optional<TailLoan> tail;
	};
	struct Silent {}; ///< Neither reads.
	using HeadPhase = std::variant<Shadowed, Solo, Crossfading, Fading, Silent>;

	/// @brief The phase as the legacy's flags and its older head's state, for one segment.
	struct Heads {
		bool newer;
		bool older;
		dsp::fixed::timestretch::CrossfadeRamp ramp;
		Older older_head;
		std::optional<TailLoan> tail;
	};
	[[nodiscard]] Heads heads() const;
	void set_heads(Heads heads, StretchContext& context);
	void hop(SampleStretchPort& port, StretchContext& context, const Part& part);
	/// @brief One segment's reads: the newer head, then the older, at the crossfade's gains (X2, X3, C4).
	void read_heads(SampleStretchPort& port, StretchContext& context, const Part& part, std::span<dsp::Q<31>> segment,
	                std::span<dsp::Q<31>> to_part_end, int32_t frames, HeadLevel level);
	void give_back_tail(StretchContext& context);
	void remember(SampleStretchPort& port, PercChunk chunk);
	void keep_hop_back(SampleStretchPort& port, const SourceFacts& facts, int64_t position_frames,
	                   const Motion& motion);

	SourcePosition position_{0};
	HopClock clock_{};
	HeadPhase phase_{Silent{}};
	PercFillMemo perc_fill_{};
	StretchLeases leases_{};
	bool fudge_ = false;
	bool begun_this_block_ = false;
};

} // namespace deluge::synth
