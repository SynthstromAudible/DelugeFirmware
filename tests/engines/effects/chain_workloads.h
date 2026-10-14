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

// The workloads the chain runtimes run against their reference (reference_chains.h): per home, blocks of 1 to 9 and
// 128 frames, in stereo and in mono, sounding and silent, each mod FX path, the delay off and ringing, the compressor
// on and reset, the stutter off and playing, and the sidechain ducking on hits; and what the test needs to build two
// chains alike (allocators, a delay arena over test memory, the settings and hand-overs an owner makes).

#pragma once

#include "dsp-next/core/stereo_sample.h"
#include "engines/effects/chain_core.h"
#include "engines/effects/delay_arena.h"
#include "engines/effects/state.h"
#include "engines/effects/stutter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace chain_workloads {

using deluge::dsp::StereoSample;
namespace fx = deluge::effects;

// ---- Memory ---------------------------------------------------------------------------------------------------------

inline void* ring_allocate(std::size_t bytes) {
	return ::operator new(bytes, std::align_val_t{16}, std::nothrow);
}
inline void ring_release(void* block) {
	::operator delete(block, std::align_val_t{16});
}
/// The mod FX's, the granular's and the stutter's blocks: the general heap.
inline constexpr fx::RingAllocator kAllocator{.allocate = ring_allocate, .release = ring_release};

/// A first-fit heap over one lent region, as the arena's regions are.
struct Heap {
	std::byte* base = nullptr;
	std::size_t size = 0;
	std::vector<std::pair<std::size_t, std::size_t>> blocks; ///< (offset, size), sorted by offset

	void* alloc(std::size_t bytes) {
		const std::size_t need = (bytes + 15) & ~std::size_t{15};
		std::size_t at = 0;
		for (auto next = blocks.begin();; ++next) {
			const std::size_t end = next == blocks.end() ? size : next->first;
			if (end >= at && end - at >= need) {
				blocks.insert(next, {at, need});
				return base + at;
			}
			if (next == blocks.end()) {
				return nullptr;
			}
			at = next->first + next->second;
		}
	}
	void free(void* block) {
		const auto offset = static_cast<std::size_t>(static_cast<std::byte*>(block) - base);
		std::erase_if(blocks, [&](const auto& b) { return b.first == offset; });
	}
};

inline std::vector<std::unique_ptr<Heap>>& heaps() {
	static std::vector<std::unique_ptr<Heap>> all;
	return all;
}
inline void* lend(std::size_t bytes) {
	return ::operator new(bytes, std::align_val_t{16});
}
inline void unlend(void* region) {
	::operator delete(region, std::align_val_t{16});
}
inline void* create_heap(void* base, std::size_t bytes) {
	heaps().push_back(std::make_unique<Heap>(Heap{static_cast<std::byte*>(base), bytes, {}}));
	return heaps().back().get();
}
inline void* heap_alloc(void* heap, std::size_t bytes) {
	return static_cast<Heap*>(heap)->alloc(bytes);
}
inline void heap_free(void* heap, void* block) {
	static_cast<Heap*>(heap)->free(block);
}
inline void request_maintenance() {
}
inline constexpr fx::DelayArenaMemory kArenaMemory{lend,       unlend,    create_heap,
                                                   heap_alloc, heap_free, request_maintenance};

// ---- Workloads ------------------------------------------------------------------------------------------------------

/// One workload: the settings and the per-block conditions both sides run.
struct Workload {
	std::string_view name;
	fx::ModFXType mod_fx; ///< The owner's type.
	bool knob_off;        ///< A host chain's soft off: the type in effect is NONE while the setting is not.
	bool stereo;
	bool silent;       ///< Silence comes in (a clip chain's content did not render; a sound has no voices).
	bool delay;        ///< A digital delay with its feedback up.
	int32_t threshold; ///< The compressor threshold param: 0 resets it.
	bool stutter;      ///< The owner is the one stuttering, from the first block.
	bool sidechain;    ///< A clip chain's sidechain is on, hit every seventh block.
	bool post;         ///< The master's post-reverb half renders (false: a stem export without song FX).
};

inline constexpr std::array kWorkloads{
    Workload{"plain", fx::ModFXType::NONE, false, true, false, false, 0, false, false, true},
    Workload{"chorus", fx::ModFXType::CHORUS, false, true, false, false, 0, false, false, true},
    Workload{"phaser_mono", fx::ModFXType::PHASER, false, false, false, false, 0, false, false, true},
    Workload{"grain", fx::ModFXType::GRAIN, false, true, false, false, 0, false, false, true},
    Workload{"grain_off", fx::ModFXType::GRAIN, true, true, false, false, 0, false, false, true},
    Workload{"chorus_off", fx::ModFXType::CHORUS, true, false, false, false, 0, false, false, true},
    Workload{"delay", fx::ModFXType::NONE, false, true, false, true, 0, false, false, true},
    Workload{"compressor", fx::ModFXType::NONE, false, true, false, false, 0x40000000, false, false, true},
    Workload{"compressor_off", fx::ModFXType::CHORUS, false, true, false, true, 0, false, false, true},
    Workload{"silence", fx::ModFXType::CHORUS, false, true, true, true, 0x40000000, false, true, true},
    Workload{"stutter", fx::ModFXType::NONE, false, true, false, true, 0, true, false, true},
    Workload{"sidechain", fx::ModFXType::NONE, false, false, false, false, 0x40000000, false, true, true},
    Workload{"no_post", fx::ModFXType::CHORUS, false, true, false, true, 0x40000000, true, false, false},
};

/// The block lengths each workload runs, in order: 1 to 9 frames, then full blocks.
inline constexpr std::array<std::size_t, 16> kBlockFrames{1, 2, 3, 4, 5, 6, 7, 8, 9, 128, 128, 128, 64, 128, 32, 128};
inline constexpr int kBlocks = 96;
inline constexpr std::size_t kMaxFrames = 128;

/// @return The settings an owner holds for @p w.
inline fx::EffectStates settings(const Workload& w) {
	fx::EffectStates s;
	s.filter = {.lpf_mode = fx::FilterMode::TRANSISTOR_24DB,
	            .hpf_mode = fx::FilterMode::HPLADDER,
	            .route = fx::FilterRoute::HIGH_TO_LOW};
	s.saturation.clipping_amount = 3;
	s.mod_fx.type = w.mod_fx;
	s.delay = {.ping_pong = true,
	           .type = w.delay ? fx::DelayType::DIGITAL : fx::DelayType::OFF,
	           .sync_type = deluge::engine::SyncType::EVEN,
	           .sync_level = deluge::engine::SyncLevel::NONE};
	s.stutter = {.use_song_stutter = false, .quantized = false, .reversed = false, .ping_pong = false};
	s.compressor = {.attack = 10 << 24,
	                .release = 20 << 24,
	                .threshold = 0x30000000,
	                .ratio = 64 << 24,
	                .sidechain_hpf = 0,
	                .blend = 0x7FFFFFFF};
	s.sidechain = {.attack = 327244,
	               .release = 992,
	               .sync_type = deluge::engine::SyncType::EVEN,
	               .sync_level = deluge::engine::SyncLevel::NONE};
	return s;
}

/// The hand-overs an owner makes when its settings take effect: the mod FX's and granular's type, the delay's.
inline void hand_over(fx::ChainCore& core, const fx::EffectStates& s) {
	fx::RingChange ring = core.mod_fx().prepare(s.mod_fx.type);
	core.mod_fx().commit(s.mod_fx.type, ring);
	core.mod_fx().finish(ring);
	fx::GranularBlockChange grain = core.granular().prepare(s.mod_fx.type);
	core.granular().commit(s.mod_fx.type, grain);
	core.granular().finish(grain);
	fx::DelayChange delay = core.delay().prepare(s.delay.type);
	core.delay().commit(s.delay.type, delay);
	core.delay().finish(delay);
}

/// @brief Gives the delay's tapes back as an owner's teardown does.
inline void release(fx::ChainCore& core) {
	fx::DelayChange off = core.delay().prepare(fx::DelayType::OFF);
	core.delay().commit(fx::DelayType::OFF, off);
	core.delay().finish(off);
}

/// A stutter begun as its song begins it for an owner, at a rate whose tape (about 1,024 frames) fills and plays back
/// within the workload.
inline constexpr uint32_t kStutterRate = 16777216 * 16;
inline void begin(fx::Stutter& stutter, const fx::EffectStates& s) {
	fx::StutterChange change = stutter.prepare(kStutterRate);
	std::ignore = stutter.commit(change, s.stutter);
	stutter.finish(change);
}

/// @return The block's input: a deterministic signal, or silence.
inline std::vector<StereoSample> input(const Workload& w, int block, std::size_t frames) {
	std::vector<StereoSample> out(frames, StereoSample{.l = 0, .r = 0});
	if (w.silent && block >= 4) {
		return out;
	}
	uint32_t state = 0x1234567u + static_cast<uint32_t>(block) * 7919u;
	for (StereoSample& s : out) {
		state = state * 1103515245u + 12345u;
		const auto noise = static_cast<int32_t>(state >> 4) - (1 << 27);
		s.l = noise;
		s.r = (noise >> 1) + static_cast<int32_t>(block << 20);
	}
	return out;
}

/// @return Whether sound comes in on @p block.
inline bool sounding(const Workload& w, int block) {
	return !(w.silent && block >= 4);
}

/// @return The mod FX, EQ and delay inputs both sides read on @p block (the delay's working state is set up by the
/// caller, on each side's delay).
inline fx::FxInputs fx_inputs(const Workload& w, const fx::EffectStates& s) {
	return fx::FxInputs{
	    .settings = &s,
	    .mod_fx_type = w.knob_off ? fx::ModFXType::NONE : w.mod_fx,
	    .mod_fx_rate = 0x00800000,
	    .mod_fx_depth = 0x20000000,
	    .mod_fx_offset = 0x08000000,
	    .mod_fx_feedback = 0x10000000,
	    .eq = {.bass = 0x20000000,
	           .treble = -0x10000000,
	           .bass_frequency = 0x10000000,
	           .treble_frequency = -0x10000000},
	    .delay = {},
	    .stereo = w.stereo,
	    .grain_send = 0x08000000,
	};
}

/// @brief The block's delay working state on @p core's delay, as an owner sets it up before rendering.
inline fx::Delay::State delay_state(fx::ChainCore& core, const Workload& w, const fx::EffectStates& s, bool sound) {
	fx::Delay::State state{};
	state.delayFeedbackAmount = w.delay ? 0x30000000 : 0;
	state.userDelayRate = 16777216 * 16; // a 1,024-frame tape: echoes within the workload
	core.delay().setupWorkingState(state, s.delay, 0x00800000, sound);
	return state;
}

inline constexpr fx::BitcrushParams kBitcrush{.bitcrush = -0x40000000, .sample_rate_reduction = -0x40000000};

inline constexpr fx::FilterParams kFilter{
    .lpf_frequency = 0x30000000,
    .lpf_resonance = 0x10000000,
    .lpf_morph = 0,
    .hpf_frequency = 0x04000000,
    .hpf_resonance = 0x08000000,
    .hpf_morph = 0,
    .lpf_frequency_param = 0x30000000,
    .lpf_morph_param = -0x7FFFFFFF - 1,
    .hpf_frequency_param = -0x40000000,
    .hpf_morph_param = -0x7FFFFFFF - 1,
    .allow_oversampling = true,
};

} // namespace chain_workloads
