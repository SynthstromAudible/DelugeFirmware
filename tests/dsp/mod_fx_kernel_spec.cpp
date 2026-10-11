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

// The ModFx kernel: the mod FX as one moded frame kernel under Mono, rendered in place. Driven as its owner drives it
// (TestModFx below: the ring handed over and taken back by event, the knobs configured each block, the post-FX volume
// from the coefficients, one in-place block unless the effect is off, the block silent or a ring mode has no ring), it
// must equal today's mod FX (reference_mod_fx.h) bit for bit on every workload (mod_fx_workloads.h): each block's
// output, post-FX volume and ring, and at the end the ring's write index, both LFO phases and the phaser's memories.
// RESEAT_HELD, local to this spec, returns to the warble with the ring full, so a missed warbler restart shows.

#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/mod_fx/mod_fx_kernel.h"
#include "dsp-next/fixed/noise/cong_noise.h"
#include "mod_fx_workloads.h"
#include "reference_mod_fx.h"

#include <array>
#include <climits>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <vector>

using deluge::dsp::Mono;
using deluge::dsp::StereoSample;
using deluge::dsp::fixed::CongNoise;
using ModFx = deluge::dsp::fixed::ModFx<CongNoise>;

static_assert(deluge::dsp::processor_kernel<ModFx> && deluge::dsp::moded_kernel<ModFx>);

namespace {

namespace mw = deluge::dsp::test::mod_fx_workloads;
using deluge::dsp::test::ReferenceModFx;

/// @return The words of @p frames, left then right per frame.
[[nodiscard]] std::vector<uint32_t> frame_words(std::span<const StereoSample> frames) {
	static_assert(sizeof(StereoSample) == 2 * sizeof(uint32_t));
	std::vector<uint32_t> words(frames.size() * 2);
	std::memcpy(words.data(), frames.data(), frames.size_bytes());
	return words;
}

/// @return The kernel's mode for @p type, rendering in stereo when @p stereo; @p type is a comb-filter type.
[[nodiscard]] ModFx::Mode mode_of(ModFXType type, bool stereo) {
	switch (type) {
	case ModFXType::FLANGER:
		return stereo ? ModFx::Mode::FLANGER : ModFx::Mode::FLANGER_MONO;
	case ModFXType::WARBLE:
		return stereo ? ModFx::Mode::WARBLE : ModFx::Mode::WARBLE_MONO;
	case ModFXType::DIMENSION:
		return ModFx::Mode::DIMENSION;
	case ModFXType::PHASER:
		return ModFx::Mode::PHASER;
	default: // CHORUS, CHORUS_STEREO
		return stereo ? ModFx::Mode::CHORUS : ModFx::Mode::CHORUS_MONO;
	}
}

/// @brief `Mono<ModFx<CongNoise>>` as its owner drives it, holding the ring it hands the kernel.
class TestModFx {
public:
	/// @brief Applies the owner's lifecycle @p event; @p rate is the block's LFO rate, UQ0.32.
	void event(mw::Event event, int32_t rate) {
		switch (event) {
		case mw::Event::NONE:
			break;
		case mw::Event::RESET_MEMORY:
			mono_.on<&ModFx::clear_memory>();
			break;
		case mw::Event::TICK_512:
			mono_.on<&ModFx::tick>(uint32_t{512}, static_cast<uint32_t>(rate));
			break;
		case mw::Event::SETUP:
			// The owner allocates, and so zeroes, a ring only when it holds none.
			if (mono_.state().ring == nullptr) {
				mono_.on<&ModFx::set_ring>(ring_.data());
				mono_.on<&ModFx::zero_ring>();
			}
			break;
		case mw::Event::DISABLE:
			mono_.on<&ModFx::clear_ring>();
			break;
		}
	}

	/// @brief Renders @p frames in place for @p block, scaling @p volume (Q31) as the owner does.
	void process(const mw::ModFxBlock& block, std::span<StereoSample> frames, int32_t& volume) {
		if (!block.sound || block.type == ModFXType::NONE || block.type == ModFXType::GRAIN) {
			return;
		}
		const ModFx::Mode mode = mode_of(block.type, block.stereo);
		mono_.configure({.mode = mode,
		                 .rate = block.rate,
		                 .depth = block.depth,
		                 .offset = block.offset,
		                 .feedback = block.feedback});
		volume = ModFx::post_fx_volume(mono_.coefficients(), volume);
		if (mode != ModFx::Mode::PHASER && mono_.state().ring == nullptr) {
			return;
		}
		modes_.insert(mode);
		mono_.renderBlock(frames, frames);
	}

	/// @return The ring's words.
	[[nodiscard]] std::vector<uint32_t> ring_words() const { return frame_words(ring_); }

	/// @return The write index, both LFO phases and the phaser's memories, as words.
	[[nodiscard]] std::vector<uint32_t> end_words() const {
		const ModFx::State& s = mono_.state();
		std::vector<uint32_t> words{static_cast<uint32_t>(s.write), s.phase, s.phase2};
		for (const uint32_t w : frame_words(std::span{&s.phaser, 1})) {
			words.push_back(w);
		}
		for (const uint32_t w : frame_words(s.allpass)) {
			words.push_back(w);
		}
		return words;
	}

	/// @return Every mode a block rendered at.
	[[nodiscard]] const std::set<ModFx::Mode>& modes() const { return modes_; }

private:
	Mono<ModFx> mono_;
	std::array<StereoSample, ModFx::kRingFrames> ring_{};
	std::set<ModFx::Mode> modes_;
};

/// @brief The reference with the same calls as @ref TestModFx.
class TestReference {
public:
	void event(mw::Event event, int32_t rate) {
		switch (event) {
		case mw::Event::NONE:
			break;
		case mw::Event::RESET_MEMORY:
			fx_.resetMemory();
			break;
		case mw::Event::TICK_512:
			fx_.tickLFO(512, rate);
			break;
		case mw::Event::SETUP:
			fx_.setupBuffer();
			break;
		case mw::Event::DISABLE:
			fx_.disableBuffer();
			break;
		}
	}

	void process(const mw::ModFxBlock& block, std::span<StereoSample> frames, int32_t& volume) {
		fx_.processModFX(frames, block.type, block.rate, block.depth, &volume, block.offset, block.feedback,
		                 block.stereo, block.sound);
	}

	[[nodiscard]] std::vector<uint32_t> ring_words() const { return frame_words(fx_.ring); }

	[[nodiscard]] std::vector<uint32_t> end_words() const {
		std::vector<uint32_t> words{fx_.modFXBufferWriteIndex, fx_.modFXLFO.phase(), fx_.modFXLFOStereo.phase()};
		for (const uint32_t w : frame_words(std::span{&fx_.phaserMemory, 1})) {
			words.push_back(w);
		}
		for (const uint32_t w : frame_words(fx_.allpassMemory)) {
			words.push_back(w);
		}
		return words;
	}

private:
	ReferenceModFx fx_;
};

/// @brief One block of a run: its output, the post-FX volume it leaves, and the ring after it.
struct BlockTrace {
	std::vector<uint32_t> out;
	int32_t volume;
	std::vector<uint32_t> ring;
};

/// @brief A whole run: every block, then the end words.
struct RunTrace {
	std::vector<BlockTrace> blocks;
	std::vector<uint32_t> end;
};

/// @return What @p workload renders through @p fx, the generator seeded to the workload's seed first.
template <typename Fx>
[[nodiscard]] RunTrace run(const mw::ModFxWorkload& workload, Fx& fx) {
	jcong = workload.cong_seed;
	RunTrace trace;
	for (std::size_t b = 0; b < workload.blocks.size(); ++b) {
		const mw::ModFxBlock& block = workload.blocks[b];
		fx.event(block.event, block.rate);
		std::vector<StereoSample> frames = mw::stereo_input(block.n, b);
		if (block.extremes) {
			for (std::size_t i = 0; i < frames.size(); ++i) {
				const bool even = i % 2 == 0;
				frames[i] = {even ? INT32_MAX : INT32_MIN, even ? INT32_MIN : INT32_MAX};
			}
		}
		int32_t volume = mw::kStartVolume;
		fx.process(block, frames, volume);
		trace.blocks.push_back({.out = frame_words(frames), .volume = volume, .ring = fx.ring_words()});
	}
	trace.end = fx.end_words();
	return trace;
}

/// @return A warble, a chorus and a warble again on one held ring, at an LFO rate whose warbler draws every 16 frames:
/// the return to the warble restarts the main warbler while its taps read a full ring, so a warbler that is not
/// restarted moves the output at once.
[[nodiscard]] mw::ModFxWorkload reseat_held() {
	constexpr int32_t kFastRate = 0x08000000;
	mw::ModFxWorkload w{.name = "RESEAT_HELD", .blocks = {}, .cong_seed = mw::kCongSeed};
	for (const ModFXType type : {ModFXType::WARBLE, ModFXType::CHORUS, ModFXType::WARBLE}) {
		for (std::size_t i = 0; i < 3; ++i) {
			w.blocks.push_back({.type = type,
			                    .stereo = true,
			                    .rate = kFastRate,
			                    .depth = mw::kDepth,
			                    .offset = mw::kOffset,
			                    .feedback = mw::kFeedback,
			                    .n = 128,
			                    .sound = true,
			                    .event = w.blocks.empty() ? mw::Event::SETUP : mw::Event::NONE});
		}
	}
	return w;
}

/// @return Every workload of mod_fx_workloads.h, then @ref reseat_held.
[[nodiscard]] const std::vector<mw::ModFxWorkload>& workloads() {
	static const std::vector<mw::ModFxWorkload> all = [] {
		std::vector<mw::ModFxWorkload> w = mw::all_mod_fx_workloads();
		w.push_back(reseat_held());
		return w;
	}();
	return all;
}

/// @return Each workload where the kernel parts from the reference, named with the block and what parted.
[[nodiscard]] std::vector<std::string> partings() {
	std::vector<std::string> parted;
	for (const mw::ModFxWorkload& workload : workloads()) {
		TestReference reference;
		TestModFx kernel;
		const RunTrace want = run(workload, reference);
		const RunTrace got = run(workload, kernel);
		const auto part = [&](std::size_t b, const char* what) {
			parted.push_back(std::string{workload.name} + " block " + std::to_string(b) + ": " + what);
		};
		bool blocks_equal = true;
		for (std::size_t b = 0; b < want.blocks.size() && blocks_equal; ++b) {
			if (got.blocks[b].out != want.blocks[b].out) {
				part(b, "output");
				blocks_equal = false;
			}
			else if (got.blocks[b].volume != want.blocks[b].volume) {
				part(b, "volume");
				blocks_equal = false;
			}
			else if (got.blocks[b].ring != want.blocks[b].ring) {
				part(b, "ring");
				blocks_equal = false;
			}
		}
		if (blocks_equal && got.end != want.end) {
			part(want.blocks.size(), "end state");
		}
	}
	return parted;
}

} // namespace

// clang-format off
describe mod_fx_kernel("the ModFx kernel", ${
	it("renders every workload as today's mod FX does, in place: each block's output, post-FX volume and ring, and "
	   "the write index, LFO phases and phaser memories at the end", _ {
		expect(partings()).to_equal(std::vector<std::string>{});
	});

	it("renders every block of every workload equal to the reference, compared word for word", _ {
		for (const mw::ModFxWorkload& workload : workloads()) {
			TestReference reference;
			TestModFx kernel;
			const RunTrace want = run(workload, reference);
			const RunTrace got = run(workload, kernel);
			for (std::size_t b = 0; b < want.blocks.size(); ++b) {
				expect(got.blocks[b].out).to_equal(want.blocks[b].out);
				expect(got.blocks[b].volume).to_equal(want.blocks[b].volume);
				expect(got.blocks[b].ring).to_equal(want.blocks[b].ring);
			}
			expect(got.end).to_equal(want.end);
		}
	});

	it("renders at every mode across the workloads", _ {
		std::set<ModFx::Mode> modes;
		for (const mw::ModFxWorkload& workload : workloads()) {
			TestModFx kernel;
			std::ignore = run(workload, kernel);
			modes.insert(kernel.modes().begin(), kernel.modes().end());
		}
		expect(modes.size()).to_equal(std::size_t{8});
	});
});
// clang-format on

CPPSPEC_MAIN(mod_fx_kernel);
