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

// effects::ModFx against today's mod FX (reference_mod_fx.h) over every workload (mod_fx_workloads.h), played as an
// owner plays them, with a counting allocator over two static blocks: each block's frames and post-FX volume, the
// ring following the type, a pass-through without a ring, and recovery from a failed allocation.

#include "engines/effects/mod_fx.h"

#include "dsp-next/core/stereo_sample.h"
#include "mod_fx_workloads.h"
#include "reference_mod_fx.h"
#include "util/waves.h" // jcong

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

using namespace deluge::effects;
using deluge::dsp::StereoSample;

namespace {

namespace mw = deluge::dsp::test::mod_fx_workloads;
using deluge::dsp::test::ReferenceModFx;

constexpr std::size_t kRingBytes = 512 * sizeof(StereoSample);

/// The allocator's blocks, and what has been asked of it.
struct TestHeap {
	alignas(8) std::array<std::byte, kRingBytes> blocks[2]{};
	bool in_use[2]{};
	int allocations = 0;
	int releases = 0;
	bool fail = false;

	void reset() {
		in_use[0] = in_use[1] = false;
		allocations = releases = 0;
		fail = false;
		std::memset(blocks, 0xA5, sizeof(blocks)); // stale audio a fresh ring must not carry
	}
};
TestHeap heap;

void* test_allocate(std::size_t bytes) {
	if (heap.fail || bytes != kRingBytes) {
		return nullptr;
	}
	for (int i = 0; i < 2; ++i) {
		if (!heap.in_use[i]) {
			heap.in_use[i] = true;
			++heap.allocations;
			return heap.blocks[i].data();
		}
	}
	return nullptr;
}

void test_release(void* block) {
	for (int i = 0; i < 2; ++i) {
		if (block == heap.blocks[i].data()) {
			heap.in_use[i] = false;
			++heap.releases;
		}
	}
}

constexpr RingAllocator kAllocator{.allocate = test_allocate, .release = test_release};

/// @brief The component with the reference's calls. A setup or disable waits for the block's type, which the owner
/// knows when it changes the type; a block of type NONE after a disable is the chain's soft off: the type is unchanged
/// and the active type NONE.
class Owner {
public:
	Owner() : fx_(kAllocator) {}

	void resetMemory() { fx_.clearMemory(); }
	void tickLFO(int32_t frames, int32_t rate) { fx_.tick(frames, rate); }
	void setupBuffer() { pending_ = Pending::SETUP; }
	void disableBuffer() { pending_ = Pending::DISABLE; }

	void processModFX(std::span<StereoSample> buffer, const ModFXType& type, int32_t rate, int32_t depth,
	                  int32_t* volume, int32_t offset, int32_t feedback, bool stereo, bool sound) {
		ModFXType active = type;
		if (pending_ != Pending::NONE) {
			if (type == ModFXType::NONE) {
				active = ModFXType::NONE; // the soft off: the held type is untouched
			}
			else {
				change(pending_ == Pending::SETUP ? type : ModFXType::NONE);
			}
			pending_ = Pending::NONE;
		}
		fx_.render(buffer,
		           {.active = active,
		            .rate = rate,
		            .depth = depth,
		            .offset = offset,
		            .feedback = feedback,
		            .stereo = stereo,
		            .sound = sound},
		           *volume);
	}

	void change(ModFXType type) {
		RingChange c = fx_.prepare(type);
		fx_.commit(type, c);
		fx_.finish(c);
	}

	ModFx& fx() { return fx_; }

private:
	enum class Pending : uint8_t { NONE, SETUP, DISABLE };
	ModFx fx_;
	Pending pending_ = Pending::NONE;
};

/// @return The words of @p frames, left then right per frame.
[[nodiscard]] std::vector<int32_t> words_of(std::span<const StereoSample> frames) {
	std::vector<int32_t> words;
	for (const StereoSample frame : frames) {
		words.push_back(frame.l);
		words.push_back(frame.r);
	}
	return words;
}

/// @return A block of frames different for each @p seed.
[[nodiscard]] std::vector<StereoSample> frames_of(std::size_t n, std::size_t seed) {
	return mw::stereo_input(n, seed);
}

[[nodiscard]] ModFxInputs flanger_inputs() {
	return {.active = ModFXType::FLANGER,
	        .rate = mw::kRate,
	        .depth = mw::kDepth,
	        .offset = mw::kOffset,
	        .feedback = mw::kFeedback,
	        .stereo = true,
	        .sound = true};
}

/// @brief Renders one block of @p type on the reference and on @p owner's component with the same frames, as an owner
/// does when a sound comes in.
/// @return Whether the two blocks and their post-FX volumes came out equal.
[[nodiscard]] bool same_block(ReferenceModFx& reference, Owner& owner, ModFXType type, std::size_t seed) {
	const std::vector<StereoSample> input = frames_of(128, seed);
	std::vector<StereoSample> want = input;
	std::vector<StereoSample> got = input;
	int32_t want_volume = mw::kStartVolume;
	int32_t got_volume = mw::kStartVolume;
	reference.processModFX(want, type, mw::kRate, mw::kDepth, &want_volume, mw::kOffset, mw::kFeedback, true, true);
	owner.fx().render(got,
	                  {.active = type,
	                   .rate = mw::kRate,
	                   .depth = mw::kDepth,
	                   .offset = mw::kOffset,
	                   .feedback = mw::kFeedback,
	                   .stereo = true,
	                   .sound = true},
	                  got_volume);
	return words_of(want) == words_of(got) && want_volume == got_volume;
}

} // namespace

// clang-format off
describe mod_fx("an owner's mod FX on the dsp-next kernel", ${
	it("renders every workload as today's mod FX does: each block's frames and post-FX volume, and the write index "
	   "and LFO phases at the end", _ {
		for (const mw::ModFxWorkload& workload : mw::all_mod_fx_workloads()) {
			heap.reset();
			ReferenceModFx reference;
			const mw::ModFxTrace want = mw::play(workload, reference);
			std::vector<uint32_t> want_end{reference.modFXBufferWriteIndex, reference.modFXLFO.phase(),
			                               reference.modFXLFOStereo.phase()};
			{
				Owner owner;
				const mw::ModFxTrace got = mw::play(workload, owner);
				expect(got.samples).to_equal(want.samples);
				expect(got.volumes).to_equal(want.volumes);
				const auto& s = owner.fx().state();
				expect(std::vector<uint32_t>{static_cast<uint32_t>(s.write), s.phase, s.phase2}).to_equal(want_end);
			}
			expect(heap.releases).to_equal(heap.allocations);
		}
	});

	it("passes through with the volume gain when no ring is held", _ {
		heap.reset();
		heap.fail = true;
		Owner owner;
		owner.change(ModFXType::FLANGER);
		expect(owner.fx().holdsRing()).to_equal(false);
		std::vector<StereoSample> frames = frames_of(128, 3);
		const std::vector<StereoSample> input = frames;
		int32_t volume = mw::kStartVolume;
		owner.fx().render(frames, flanger_inputs(), volume);
		expect(words_of(frames)).to_equal(words_of(input));

		ReferenceModFx reference;
		std::vector<StereoSample> ref_frames = input;
		int32_t ref_volume = mw::kStartVolume;
		reference.processModFX(ref_frames, ModFXType::FLANGER, mw::kRate, mw::kDepth, &ref_volume, mw::kOffset,
		                       mw::kFeedback, true, true);
		expect(volume).to_equal(ref_volume);
		expect(volume).not_().to_equal(mw::kStartVolume);
	});

	it("keeps one ring across the ring types, releases it for the phaser and allocates a zeroed one after", _ {
		heap.reset();
		{
			ModFx fx(kAllocator);
			RingChange c = fx.prepare(ModFXType::CHORUS);
			expect(c.acquired).not_().to_be_null();
			expect(heap.allocations).to_equal(1);
			fx.commit(ModFXType::CHORUS, c);
			fx.finish(c);
			expect(fx.holdsRing()).to_equal(true);
			const StereoSample* ring = fx.state().ring;

			c = fx.prepare(ModFXType::FLANGER);
			expect(c.acquired).to_be_null();
			fx.commit(ModFXType::FLANGER, c);
			fx.finish(c);
			expect(heap.allocations).to_equal(1);
			expect(heap.releases).to_equal(0);
			expect(fx.state().ring).to_equal(ring);

			c = fx.prepare(ModFXType::PHASER);
			expect(c.acquired).to_be_null();
			fx.commit(ModFXType::PHASER, c);
			expect(c.released).to_equal(ring);
			expect(fx.holdsRing()).to_equal(false);
			expect(heap.releases).to_equal(0);
			fx.finish(c);
			expect(heap.releases).to_equal(1);

			c = fx.prepare(ModFXType::CHORUS);
			expect(c.acquired).not_().to_be_null();
			std::vector<uint32_t> words(kRingBytes / sizeof(uint32_t));
			std::memcpy(words.data(), c.acquired, kRingBytes);
			expect(words).to_equal(std::vector<uint32_t>(words.size(), 0));
			fx.commit(ModFXType::CHORUS, c);
			fx.finish(c);
			expect(heap.allocations).to_equal(2);
		}
		expect(heap.releases).to_equal(2);
	});

	it("recovers at the next ring type after a failed allocation", _ {
		heap.reset();
		ModFx fx(kAllocator);
		heap.fail = true;
		RingChange c = fx.prepare(ModFXType::WARBLE);
		expect(c.acquired).to_be_null();
		fx.commit(ModFXType::WARBLE, c);
		fx.finish(c);
		expect(fx.holdsRing()).to_equal(false);
		heap.fail = false;
		c = fx.prepare(ModFXType::WARBLE);
		fx.commit(ModFXType::WARBLE, c);
		fx.finish(c);
		expect(fx.holdsRing()).to_equal(true);
	});

	it("frees an acquired ring that a commit did not take", _ {
		heap.reset();
		ModFx fx(kAllocator);
		RingChange c = fx.prepare(ModFXType::CHORUS);
		fx.finish(c);
		expect(heap.releases).to_equal(1);
		expect(fx.holdsRing()).to_equal(false);
	});

	it("clears the phaser's memories in a soft off, as today's mod FX does once it has freed its ring", _ {
		heap.reset();
		ReferenceModFx reference;
		Owner owner;
		reference.setupBuffer();
		owner.change(ModFXType::CHORUS);
		expect(same_block(reference, owner, ModFXType::PHASER, 1)).to_equal(true);
		expect(same_block(reference, owner, ModFXType::PHASER, 2)).to_equal(true);
		// The soft off: the knob at minimum. The reference has freed its ring; the component keeps its own.
		reference.disableBuffer();
		std::vector<StereoSample> idle = frames_of(16, 0);
		int32_t volume = mw::kStartVolume;
		owner.fx().render(idle, {.active = ModFXType::NONE, .sound = false}, volume);
		reference.resetMemory();
		owner.resetMemory();
		expect(same_block(reference, owner, ModFXType::PHASER, 3)).to_equal(true);
	});

	it("does not zero a ring it has just been handed, which prepare zeroed already", _ {
		heap.reset();
		Owner owner;
		owner.change(ModFXType::PHASER);
		std::vector<StereoSample> frames = frames_of(16, 4);
		int32_t volume = mw::kStartVolume;
		ModFxInputs phaser = flanger_inputs();
		phaser.active = ModFXType::PHASER;
		owner.fx().render(frames, phaser, volume);
		owner.change(ModFXType::CHORUS);
		std::byte* ring = heap.in_use[0] ? heap.blocks[0].data() : heap.blocks[1].data();
		std::memset(ring, 0x5A, 16); // a marker where a second zeroing would erase it
		ModFxInputs chorus = flanger_inputs();
		chorus.active = ModFXType::CHORUS;
		chorus.sound = false;
		owner.fx().render(frames, chorus, volume);
		expect(static_cast<int>(ring[0])).to_equal(0x5A);
	});

	it("keeps the ring it holds when a second one is acquired, and frees the spare", _ {
		heap.reset();
		{
			ModFx fx(kAllocator);
			RingChange first = fx.prepare(ModFXType::CHORUS);
			RingChange second = fx.prepare(ModFXType::CHORUS);
			expect(heap.allocations).to_equal(2);
			const StereoSample* held = first.acquired;
			fx.commit(ModFXType::CHORUS, first);
			fx.finish(first);
			fx.commit(ModFXType::CHORUS, second);
			fx.finish(second);
			expect(fx.state().ring).to_equal(held);
			expect(heap.releases).to_equal(1);
		}
		expect(heap.releases).to_equal(2);
	});

	it("maps each type to its kernel mode", _ {
		using Mode = ModFx::Kernel::Mode;
		expect(mod_fx_mode(ModFXType::CHORUS, true)).to_equal(Mode::CHORUS);
		expect(mod_fx_mode(ModFXType::CHORUS_STEREO, false)).to_equal(Mode::CHORUS_MONO);
		expect(mod_fx_mode(ModFXType::DIMENSION, false)).to_equal(Mode::DIMENSION);
		expect(mod_fx_mode(ModFXType::FLANGER, false)).to_equal(Mode::FLANGER_MONO);
		expect(mod_fx_mode(ModFXType::WARBLE, true)).to_equal(Mode::WARBLE);
		expect(mod_fx_mode(ModFXType::PHASER, false)).to_equal(Mode::PHASER);
	});
});
// clang-format on

CPPSPEC_SPEC(mod_fx)
