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

/// @file
/// @brief The mod FX's workloads: deterministic runs of blocks, each with its type, knobs and the lifecycle event an
/// owner applies before it, and what a mod FX renders from them.
///
/// A run is generic over the mod FX class: the reference (reference_mod_fx.h) or the kernel's wrapper, which take the
/// same calls. A block of the same index gets the same input in every run.

#pragma once

#include "definitions_cxx.hpp"
#include "dsp-next/core/stereo_sample.h"
#include "util/waves.h" // jcong

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace deluge::dsp::test::mod_fx_workloads {

/// @brief What an owner does to the mod FX before a block.
enum class Event : uint8_t {
	NONE,
	RESET_MEMORY, ///< `resetMemory()`: the owner's clear.
	TICK_512,     ///< `tickLFO(512, rate)`: 512 samples skipped, the phase advancing without rendering.
	SETUP,        ///< `setupBuffer()`: the ring allocated, as an owner's change to a ring type does.
	DISABLE,      ///< `disableBuffer()`: the ring released, as an owner's change to the phaser or off does.
};

/// @brief One block of a workload.
struct ModFxBlock {
	ModFXType type;
	bool stereo;
	int32_t rate;          ///< The LFO's phase increment, UQ0.32.
	int32_t depth;         ///< The depth param, Q31.
	int32_t offset;        ///< The offset param, Q31.
	int32_t feedback;      ///< The feedback param, Q31.
	uint16_t n;            ///< Frames in the block.
	bool sound;            ///< Whether anything rendered into the block; else the effect returns at once.
	Event event;           ///< Applied before the block.
	bool extremes = false; ///< The input is INT32_MAX and INT32_MIN alternating, rather than @ref stereo_input.
};

/// @brief A named run of blocks and the synth-wide noise generator's state it starts from.
struct ModFxWorkload {
	const char* name;
	std::vector<ModFxBlock> blocks;
	uint32_t cong_seed;
};

/// @brief The synth-wide generator's state at boot (util/waves.cpp), where every workload starts it.
inline constexpr uint32_t kCongSeed = 380116160;

inline constexpr int32_t kRate = 0x00a00000;
inline constexpr int32_t kDepth = 0x40000000;
inline constexpr int32_t kOffset = 0x20000000;
inline constexpr int32_t kFeedback = 0x20000000;

/// @brief The post-FX volume a run starts each block's chain from, Q31.
inline constexpr int32_t kStartVolume = 0x30000000;

/// @brief The block lengths a workload cycles through: 1 to 9, then 128.
inline constexpr std::array<uint16_t, 10> kLengths{1, 2, 3, 4, 5, 6, 7, 8, 9, 128};

/// @return @p n frames of a full-range sequence, a linear congruential generator per channel, different for each
/// @p block.
[[nodiscard]] inline std::vector<StereoSample> stereo_input(std::size_t n, std::size_t block) {
	std::vector<StereoSample> v(n);
	uint32_t l = 0x9e3779b9U * static_cast<uint32_t>(block + 1);
	uint32_t r = 0x85ebca6bU * static_cast<uint32_t>(block + 1) + 1;
	for (StereoSample& frame : v) {
		l = (l * 1664525U) + 1013904223U;
		r = (r * 22695477U) + 1U;
		frame = {static_cast<q31_t>(l), static_cast<q31_t>(r)};
	}
	return v;
}

namespace detail {

/// @brief Appends blocks to a workload, cycling the lengths from the workload's block count.
class Builder {
public:
	/// @brief Appends @p count blocks of @p type, the first carrying @p first_event.
	Builder& run(ModFXType type, bool stereo, std::size_t count, Event first_event = Event::NONE,
	             int32_t feedback = kFeedback, bool extremes = false) {
		for (std::size_t i = 0; i < count; ++i) {
			blocks_.push_back({.type = type,
			                   .stereo = stereo,
			                   .rate = kRate,
			                   .depth = kDepth,
			                   .offset = kOffset,
			                   .feedback = feedback,
			                   .n = kLengths[blocks_.size() % kLengths.size()],
			                   .sound = true,
			                   .event = i == 0 ? first_event : Event::NONE,
			                   .extremes = extremes});
		}
		return *this;
	}

	/// @brief Appends one block with @p event and @p sound.
	Builder& one(ModFXType type, Event event, bool sound) {
		run(type, true, 1, event);
		blocks_.back().sound = sound;
		return *this;
	}

	[[nodiscard]] ModFxWorkload build(const char* name) { return {name, std::move(blocks_), kCongSeed}; }

private:
	std::vector<ModFxBlock> blocks_;
};

} // namespace detail

/// @brief Every workload.
[[nodiscard]] inline const std::vector<ModFxWorkload>& all_mod_fx_workloads() {
	static const std::vector<ModFxWorkload> all = [] {
		using detail::Builder;
		std::vector<ModFxWorkload> w;

		struct Each {
			ModFXType type;
			const char* stereo_name;
			const char* mono_name;
		};
		constexpr std::array<Each, 6> each{{
		    {ModFXType::FLANGER, "EACH_FLANGER_STEREO", "EACH_FLANGER_MONO"},
		    {ModFXType::CHORUS, "EACH_CHORUS_STEREO", "EACH_CHORUS_MONO"},
		    {ModFXType::PHASER, "EACH_PHASER_STEREO", "EACH_PHASER_MONO"},
		    {ModFXType::CHORUS_STEREO, "EACH_CHORUS_STEREO_STEREO", "EACH_CHORUS_STEREO_MONO"},
		    {ModFXType::WARBLE, "EACH_WARBLE_STEREO", "EACH_WARBLE_MONO"},
		    {ModFXType::DIMENSION, "EACH_DIMENSION_STEREO", "EACH_DIMENSION_MONO"},
		}};
		for (const Each& e : each) {
			w.push_back(Builder{}.run(e.type, true, 40, Event::SETUP).build(e.stereo_name));
			w.push_back(Builder{}.run(e.type, false, 40, Event::SETUP).build(e.mono_name));
		}

		w.push_back(Builder{}.run(ModFXType::PHASER, true, 40).build("PHASER"));

		{
			Builder b;
			bool first = true;
			for (const ModFXType type : {ModFXType::FLANGER, ModFXType::CHORUS, ModFXType::CHORUS_STEREO,
			                             ModFXType::WARBLE, ModFXType::DIMENSION}) {
				b.run(type, true, 10, first ? Event::SETUP : Event::NONE, INT32_MAX, true);
				first = false;
			}
			w.push_back(b.build("FULL_SCALE"));
		}

		w.push_back(Builder{}
		                .run(ModFXType::WARBLE, true, 10, Event::SETUP)
		                .run(ModFXType::CHORUS, true, 10, Event::SETUP)
		                .run(ModFXType::WARBLE, true, 10, Event::SETUP)
		                .run(ModFXType::PHASER, true, 10, Event::DISABLE)
		                .run(ModFXType::WARBLE, true, 10, Event::SETUP)
		                .build("RESEAT"));

		w.push_back(Builder{}
		                .run(ModFXType::CHORUS, true, 10, Event::SETUP)
		                .run(ModFXType::NONE, true, 5, Event::DISABLE)
		                .run(ModFXType::CHORUS, true, 10, Event::SETUP)
		                .build("SOFT_OFF"));

		w.push_back(Builder{}.run(ModFXType::FLANGER, true, 10).build("NO_RING"));

		w.push_back(Builder{}
		                .run(ModFXType::CHORUS, true, 5, Event::SETUP)
		                .run(ModFXType::CHORUS, true, 1, Event::TICK_512)
		                .run(ModFXType::CHORUS, true, 4)
		                .run(ModFXType::CHORUS, true, 1, Event::RESET_MEMORY)
		                .run(ModFXType::CHORUS, true, 4)
		                .run(ModFXType::PHASER, true, 5, Event::DISABLE)
		                .run(ModFXType::PHASER, true, 1, Event::RESET_MEMORY)
		                .run(ModFXType::PHASER, true, 4)
		                .build("TICK_AND_RESET"));

		{
			Builder b;
			b.run(ModFXType::CHORUS, true, 1, Event::SETUP);
			for (std::size_t i = 1; i < 30; ++i) {
				b.one(ModFXType::CHORUS, Event::NONE, i % 3 != 0);
			}
			w.push_back(b.build("SILENT_BLOCKS"));
		}
		return w;
	}();
	return all;
}

/// @brief What a run renders: every block's output, and the post-FX volume after every block.
struct ModFxTrace {
	std::vector<int32_t> samples; ///< Each block's frames, left then right, one block after another.
	std::vector<int32_t> inputs;  ///< The same layout, before the effect.
	std::vector<int32_t> volumes; ///< The volume each block's call leaves; each block starts from @ref kStartVolume.
};

/// @brief Plays @p workload through @p fx: the generator restored to the workload's seed, each block's event applied,
/// then the block rendered.
/// @tparam Fx A mod FX with the reference's calls.
template <typename Fx>
[[nodiscard]] ModFxTrace play(const ModFxWorkload& workload, Fx& fx) {
	jcong = workload.cong_seed;
	ModFxTrace trace;
	for (std::size_t b = 0; b < workload.blocks.size(); ++b) {
		const ModFxBlock& block = workload.blocks[b];
		switch (block.event) {
		case Event::NONE:
			break;
		case Event::RESET_MEMORY:
			fx.resetMemory();
			break;
		case Event::TICK_512:
			fx.tickLFO(512, block.rate);
			break;
		case Event::SETUP:
			fx.setupBuffer();
			break;
		case Event::DISABLE:
			fx.disableBuffer();
			break;
		}
		std::vector<StereoSample> frames = stereo_input(block.n, b);
		if (block.extremes) {
			for (std::size_t i = 0; i < frames.size(); ++i) {
				const bool even = i % 2 == 0;
				frames[i] = {even ? INT32_MAX : INT32_MIN, even ? INT32_MIN : INT32_MAX};
			}
		}
		for (const StereoSample frame : frames) {
			trace.inputs.push_back(frame.l);
			trace.inputs.push_back(frame.r);
		}
		int32_t volume = kStartVolume;
		fx.processModFX(frames, block.type, block.rate, block.depth, &volume, block.offset, block.feedback,
		                block.stereo, block.sound);
		for (const StereoSample frame : frames) {
			trace.samples.push_back(frame.l);
			trace.samples.push_back(frame.r);
		}
		trace.volumes.push_back(volume);
	}
	return trace;
}

} // namespace deluge::dsp::test::mod_fx_workloads
