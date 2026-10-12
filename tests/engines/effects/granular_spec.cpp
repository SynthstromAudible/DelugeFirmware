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

// effects::Granular against today's granular (reference_granular.h) over every workload (granular_workloads.h),
// played as an owner plays them, with a counting allocator over two static blocks: each block's frames, post-FX volume
// and reverb feed, and the State at the end; the block following the type; a pass-through without a block; and the two
// owner kinds' entry into and exit from GRAIN.

#include "engines/effects/granular.h"

#include "dsp-next/core/stereo_sample.h"
#include "granular_workloads.h"
#include "reference_granular.h"
#include "util/waves.h" // jcong

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <tuple>
#include <vector>

using namespace deluge::effects;
using deluge::dsp::StereoSample;

namespace {

namespace gw = deluge::dsp::test::granular_workloads;
using deluge::dsp::test::ReferenceGranular;

constexpr std::size_t kRingBytes = 600000; ///< Room for the 524,288-byte ring.
constexpr std::size_t kStateBytes = 1024;  ///< Room for the kernel's State, about 350 bytes.

/// The allocator's blocks, and what has been asked of it: a request up to kStateBytes is a State, a larger one a ring.
struct TestHeap {
	alignas(16) std::array<std::byte, kRingBytes> rings[2]{};
	alignas(16) std::array<std::byte, kStateBytes> states[2]{};
	bool ring_in_use[2]{};
	bool state_in_use[2]{};
	int allocations = 0;
	int state_allocations = 0;
	int ring_allocations = 0;
	int releases = 0;
	bool fail = false;       ///< Fails every request.
	bool fail_rings = false; ///< Fails the large requests.

	void reset() {
		ring_in_use[0] = ring_in_use[1] = state_in_use[0] = state_in_use[1] = false;
		allocations = state_allocations = ring_allocations = releases = 0;
		fail = fail_rings = false;
		std::memset(rings, 0xA5, sizeof(rings)); // stale audio a fresh ring must not carry
		std::memset(states, 0xA5, sizeof(states));
	}
};
TestHeap heap;

void* test_allocate(std::size_t bytes) {
	if (heap.fail || bytes > kRingBytes) {
		return nullptr;
	}
	const bool is_state = bytes <= kStateBytes;
	if (!is_state && heap.fail_rings) {
		return nullptr;
	}
	for (int i = 0; i < 2; ++i) {
		bool& in_use = is_state ? heap.state_in_use[i] : heap.ring_in_use[i];
		if (!in_use) {
			in_use = true;
			++heap.allocations;
			++(is_state ? heap.state_allocations : heap.ring_allocations);
			return is_state ? static_cast<void*>(heap.states[i].data()) : static_cast<void*>(heap.rings[i].data());
		}
	}
	return nullptr;
}

void test_release(void* block) {
	for (int i = 0; i < 2; ++i) {
		if (block == heap.rings[i].data()) {
			heap.ring_in_use[i] = false;
			++heap.releases;
		}
		else if (block == heap.states[i].data()) {
			heap.state_in_use[i] = false;
			++heap.releases;
		}
	}
}

constexpr RingAllocator kAllocator{.allocate = test_allocate, .release = test_release};

/// @brief The component with the reference's calls, on the type GRAIN from the start.
class Owner {
public:
	explicit Owner(EnterGrain enter = EnterGrain::CLEAR) : fx_(kAllocator, enter) { change(ModFXType::GRAIN); }

	void clearGrainFXBuffer() { fx_.clear(); }
	void startSkippingRendering() { fx_.startSkippingRendering(); }

	void processGrainFX(std::span<StereoSample> frames, int32_t rate, int32_t mix, int32_t density, int32_t randomness,
	                    int32_t* volume, bool sound, q31_t reverb_amount, std::span<q31_t> send) {
		fx_.render(frames,
		           {.rate = rate,
		            .mix = mix,
		            .density = density,
		            .randomness = randomness,
		            .send_amount = reverb_amount,
		            .sound = sound},
		           *volume, send);
	}

	void change(ModFXType type) {
		GranularBlockChange c = fx_.prepare(type);
		fx_.commit(type, c);
		fx_.finish(c);
	}

	Granular& fx() { return fx_; }

private:
	Granular fx_;
};

/// @brief Appends a grain's fields to @p words, in the kernel's Grain order. A silent grain's other fields are set
/// when it starts and never read before, so they are left out as zero.
void push_grain(std::vector<uint32_t>& words, int32_t length, int32_t start, int32_t counter, uint16_t pitch,
                int32_t vol_scale, int32_t vol_scale_max, bool reverse, int32_t pan_l, int32_t pan_r) {
	if (length == 0) {
		words.insert(words.end(), 9, 0);
		return;
	}
	for (const int32_t w :
	     {length, start, counter, int32_t{pitch}, vol_scale, vol_scale_max, int32_t{reverse}, pan_l, pan_r}) {
		words.push_back(static_cast<uint32_t>(w));
	}
}

/// @return The component's State as words: the write index, whether grains may start, whether they are free of the
/// first pass, the countdown, each grain, and with @p all, the wet low-pass memories and the knobs' values.
[[nodiscard]] std::vector<uint32_t> state_words(const Granular& fx, bool all = true) {
	const Granular::Kernel::State& s = *fx.state();
	std::vector<uint32_t> words{s.write, static_cast<uint32_t>(s.full), static_cast<uint32_t>(s.initialized),
	                            static_cast<uint32_t>(s.wraps_to_shutdown)};
	for (const auto& g : s.grains) {
		push_grain(words, g.length, g.start, g.counter, g.pitch, g.vol_scale, g.vol_scale_max, g.reverse, g.pan_l,
		           g.pan_r);
	}
	if (all) {
		for (const int32_t w : {s.lpf_l.z, s.lpf_r.z, s.levels.size, s.levels.period, s.levels.wet, s.levels.dry,
		                        s.levels.feedback, s.knobs.rate, s.knobs.mix, s.knobs.density}) {
			words.push_back(static_cast<uint32_t>(w));
		}
	}
	return words;
}

/// @return The cursor a clear leaves: the write index, whether grains may start, whether they are free of the first
/// pass, and each grain's length.
[[nodiscard]] std::vector<uint32_t> cursor_words(const std::vector<uint32_t>& words) {
	std::vector<uint32_t> cursor{words[0], words[1], words[2]};
	for (std::size_t g = 0; g < Granular::Kernel::kGrains; ++g) {
		cursor.push_back(words[4 + (9 * g)]);
	}
	return cursor;
}

/// @return The reference's State as words, in @ref state_words's order.
[[nodiscard]] std::vector<uint32_t> reference_words(const ReferenceGranular& fx, bool all = true) {
	std::vector<uint32_t> words{fx.write_index(), static_cast<uint32_t>(fx.buffer_full()),
	                            static_cast<uint32_t>(fx.grain_initialized()),
	                            static_cast<uint32_t>(fx.wraps_to_shutdown())};
	for (const auto& g : fx.grain_states()) {
		push_grain(words, g.length, g.startPoint, g.counter, g.pitch, g.volScale, g.volScaleMax, g.rev, g.panVolL,
		           g.panVolR);
	}
	if (all) {
		const auto lpf = fx.low_pass();
		const ReferenceGranular::Cache c = fx.cache();
		for (const int32_t w :
		     {lpf[0].z, lpf[1].z, c.size, c.period, c.wet, c.dry, c.feedback, c.rate, c.mix, c.density}) {
			words.push_back(static_cast<uint32_t>(w));
		}
	}
	return words;
}

/// @return The words of the component's ring as it reads: a slot not written since the block was taken up is zero.
[[nodiscard]] std::vector<uint32_t> ring_words(const Granular& fx) {
	const Granular::Kernel::State& s = *fx.state();
	std::vector<uint32_t> words;
	words.reserve(2 * Granular::Kernel::kRingFrames);
	for (uint32_t i = 0; i < static_cast<uint32_t>(Granular::Kernel::kRingFrames); ++i) {
		const StereoSample frame = i < s.written ? s.ring[i] : StereoSample{0, 0};
		words.push_back(static_cast<uint32_t>(frame.l));
		words.push_back(static_cast<uint32_t>(frame.r));
	}
	return words;
}

[[nodiscard]] std::vector<uint32_t> reference_ring_words(const ReferenceGranular& fx) {
	std::vector<uint32_t> words;
	for (const StereoSample frame : fx.ring_frames()) {
		words.push_back(static_cast<uint32_t>(frame.l));
		words.push_back(static_cast<uint32_t>(frame.r));
	}
	return words;
}

/// @brief Renders 300 blocks, runs @p leave and @p enter, then renders 300 more, as an owner does that leaves GRAIN and
/// comes back; every block is the same knobs, with sound.
template <typename Fx, typename Leave, typename Enter>
[[nodiscard]] gw::GranularTrace drive_round_trip(Fx& fx, Leave leave, Enter enter) {
	jcong = gw::kCongSeed;
	gw::GranularTrace trace;
	const auto render = [&](std::size_t from, std::size_t count) {
		for (std::size_t b = from; b < from + count; ++b) {
			std::vector<StereoSample> frames = gw::stereo_input(256, b);
			std::vector<q31_t> send = gw::send_input(256, b, false);
			int32_t volume = gw::kStartVolume;
			fx.processGrainFX(frames, gw::kRateMid, gw::kMixMid, gw::kDensityMid, gw::kRandomnessMid, &volume, true,
			                  gw::kSendAmount, send);
			for (const StereoSample frame : frames) {
				trace.samples.push_back(frame.l);
				trace.samples.push_back(frame.r);
			}
			trace.sends.insert(trace.sends.end(), send.begin(), send.end());
			trace.volumes.push_back(volume);
		}
	};
	render(0, 300);
	leave(fx);
	enter(fx);
	render(300, 300);
	return trace;
}

/// @return The first @p blocks blocks of the workload named @p name.
[[nodiscard]] gw::GranularWorkload prefix_of(const char* name, std::size_t blocks) {
	for (const gw::GranularWorkload& w : gw::all_granular_workloads()) {
		if (std::string_view{w.name} == name) {
			gw::GranularWorkload cut = w;
			cut.blocks.resize(blocks);
			return cut;
		}
	}
	return {};
}

} // namespace

// clang-format off
describe granular("an owner's granular effect on the dsp-next kernel", ${
	it("renders every workload as today's granular does: each block's frames, post-FX volume and reverb feed, and "
	   "the State and the ring at the end", _ {
		for (const gw::GranularWorkload& workload : gw::all_granular_workloads()) {
			heap.reset();
			const auto reference = std::make_unique<ReferenceGranular>();
			const gw::GranularTrace want = gw::play(workload, *reference);
			{
				Owner owner;
				const gw::GranularTrace got = gw::play(workload, owner);
				expect(got.samples).to_equal(want.samples);
				expect(got.sends).to_equal(want.sends);
				expect(got.volumes).to_equal(want.volumes);
				expect(state_words(owner.fx())).to_equal(reference_words(*reference));
				expect(ring_words(owner.fx())).to_equal(reference_ring_words(*reference));
				expect(owner.fx().samplesToShutdown()).to_equal(reference->getSamplesToShutdown());
			}
			expect(heap.releases).to_equal(heap.allocations);
		}
	});

	it("passes through, leaving the volume and the reverb feed, when no block is held", _ {
		heap.reset();
		heap.fail = true;
		Owner owner;
		expect(owner.fx().holdsBlock()).to_equal(false);
		expect(owner.fx().samplesToShutdown()).to_equal(0);
		const std::vector<StereoSample> input = gw::stereo_input(128, 3);
		std::vector<StereoSample> frames = input;
		std::vector<q31_t> send = gw::send_input(128, 3, false);
		const std::vector<q31_t> send_in = send;
		int32_t volume = gw::kStartVolume;
		owner.processGrainFX(frames, gw::kRateMid, gw::kMixMid, gw::kDensityMid, gw::kRandomnessMid, &volume, true,
		                     gw::kSendAmount, send);
		for (std::size_t i = 0; i < frames.size(); ++i) {
			expect(frames[i].l).to_equal(input[i].l);
			expect(frames[i].r).to_equal(input[i].r);
		}
		expect(send).to_equal(send_in);
		expect(volume).to_equal(gw::kStartVolume);
		owner.clearGrainFXBuffer();
		owner.startSkippingRendering();
	});

	it("holds one zeroed block while the type is GRAIN and none otherwise, with no leak", _ {
		heap.reset();
		{
			Granular fx(kAllocator, EnterGrain::CLEAR);
			expect(fx.prepare(ModFXType::CHORUS).acquired_ring).to_be_null();
			expect(fx.prepare(ModFXType::NONE).acquired_ring).to_be_null();
			expect(heap.allocations).to_equal(0);

			GranularBlockChange c = fx.prepare(ModFXType::GRAIN);
			expect(c.acquired_ring).not_().to_be_null();
			expect(c.acquired_state).not_().to_be_null();
			expect(heap.allocations).to_equal(2);
			fx.commit(ModFXType::GRAIN, c);
			fx.finish(c);
			expect(fx.holdsBlock()).to_equal(true);
			const std::vector<uint32_t> zero(2 * Granular::Kernel::kRingFrames, 0);
			const Granular::Kernel::State& s = *fx.state();
			std::vector<uint32_t> raw(zero.size());
			std::memcpy(raw.data(), s.ring, raw.size() * sizeof(uint32_t));
			expect(raw).to_equal(zero);
			expect(s.written).to_equal(0u);

			c = fx.prepare(ModFXType::GRAIN); // held already
			expect(c.acquired_ring).to_be_null();
			fx.commit(ModFXType::GRAIN, c);
			fx.finish(c);
			expect(heap.allocations).to_equal(2);

			c = fx.prepare(ModFXType::CHORUS);
			fx.commit(ModFXType::CHORUS, c);
			expect(fx.holdsBlock()).to_equal(false);
			expect(heap.releases).to_equal(0);
			fx.finish(c);
			expect(heap.releases).to_equal(1);

			c = fx.prepare(ModFXType::GRAIN);
			fx.commit(ModFXType::GRAIN, c);
			fx.finish(c);
			expect(c.acquired_state).to_be_null();
			expect(heap.allocations).to_equal(3);
			expect(heap.state_allocations).to_equal(1);
			expect(fx.holdsBlock()).to_equal(true);
		}
		expect(heap.releases).to_equal(3);
	});

	it("recovers at the next GRAIN after a failed allocation", _ {
		heap.reset();
		Granular fx(kAllocator, EnterGrain::CLEAR);
		heap.fail = true;
		GranularBlockChange c = fx.prepare(ModFXType::GRAIN);
		expect(c.acquired_ring).to_be_null();
		fx.commit(ModFXType::GRAIN, c);
		fx.finish(c);
		expect(fx.holdsBlock()).to_equal(false);
		heap.fail = false;
		c = fx.prepare(ModFXType::GRAIN);
		fx.commit(ModFXType::GRAIN, c);
		fx.finish(c);
		expect(fx.holdsBlock()).to_equal(true);
	});

	it("keeps the block it holds when a second is acquired, and frees the spare", _ {
		heap.reset();
		{
			Granular fx(kAllocator, EnterGrain::CLEAR);
			GranularBlockChange first = fx.prepare(ModFXType::GRAIN);
			GranularBlockChange second = fx.prepare(ModFXType::GRAIN);
			expect(heap.allocations).to_equal(4);
			fx.commit(ModFXType::GRAIN, first);
			fx.finish(first);
			const Granular::Kernel::State* held = fx.state();
			fx.commit(ModFXType::GRAIN, second);
			expect(second.released_ring).not_().to_be_null();
			fx.finish(second);
			expect(fx.state()).to_equal(held);
			expect(heap.releases).to_equal(2);
		}
		expect(heap.releases).to_equal(4);
	});

	it("frees an acquired block that a commit did not take", _ {
		heap.reset();
		Granular fx(kAllocator, EnterGrain::CLEAR);
		GranularBlockChange c = fx.prepare(ModFXType::GRAIN);
		fx.finish(c);
		expect(heap.releases).to_equal(2);
		expect(fx.holdsBlock()).to_equal(false);
		expect(fx.state()).to_be_null();
	});

	it("keeps its State when only the ring cannot be allocated, and passes through until it can", _ {
		heap.reset();
		Granular fx(kAllocator, EnterGrain::CLEAR);
		heap.fail_rings = true;
		GranularBlockChange c = fx.prepare(ModFXType::GRAIN);
		expect(c.acquired_ring).to_be_null();
		fx.commit(ModFXType::GRAIN, c);
		fx.finish(c);
		expect(fx.holdsBlock()).to_equal(false);
		expect(fx.state()).not_().to_be_null();
		heap.fail_rings = false;
		c = fx.prepare(ModFXType::GRAIN);
		expect(c.acquired_state).to_be_null();
		fx.commit(ModFXType::GRAIN, c);
		fx.finish(c);
		expect(fx.holdsBlock()).to_equal(true);
		expect(heap.state_allocations).to_equal(1);
	});

	it("starts a sound's entry into GRAIN as today's clearGrainFXBuffer leaves the effect: grains stopped, first "
	   "pass begun, nothing written", _ {
		heap.reset();
		const gw::GranularWorkload run = prefix_of("KNOB_SWEEP", 299);
		const auto reference = std::make_unique<ReferenceGranular>();
		std::ignore = gw::play(run, *reference);
		Owner owner;
		std::ignore = gw::play(run, owner);
		expect(owner.fx().state()->grains[0].length).not_().to_equal(0); // grains were sounding
		reference->clearGrainFXBuffer();
		owner.change(ModFXType::CHORUS); // leaves GRAIN: the block and its state are freed
		owner.change(ModFXType::GRAIN);  // enters: a fresh block
		expect(cursor_words(state_words(owner.fx(), false)))
		    .to_equal(cursor_words(reference_words(*reference, false)));
		expect(cursor_words(state_words(owner.fx(), false)))
		    .to_equal(std::vector<uint32_t>{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
	});

	it("keeps a sound's State across GRAIN, chorus, GRAIN, clearing on the way back in as today's does", _ {
		heap.reset();
		const auto reference = std::make_unique<ReferenceGranular>();
		const gw::GranularTrace want = drive_round_trip(
		    *reference, [](auto& fx) { fx.startSkippingRendering(); }, [](auto& fx) { fx.clearGrainFXBuffer(); });
		{
			Owner owner;
			const gw::GranularTrace got = drive_round_trip(
			    owner, [](Owner& fx) { fx.change(ModFXType::CHORUS); }, [](Owner& fx) { fx.change(ModFXType::GRAIN); });
			expect(got.samples).to_equal(want.samples);
			expect(got.sends).to_equal(want.sends);
			expect(got.volumes).to_equal(want.volumes);
			expect(state_words(owner.fx())).to_equal(reference_words(*reference));
			expect(heap.state_allocations).to_equal(1);
			expect(heap.ring_allocations).to_equal(2);
		}
		expect(heap.releases).to_equal(heap.allocations);
	});

	it("keeps a chain's State across GRAIN, soft off, GRAIN, without a clear", _ {
		heap.reset();
		const auto reference = std::make_unique<ReferenceGranular>();
		const gw::GranularTrace want = drive_round_trip(
		    *reference, [](auto& fx) { fx.startSkippingRendering(); }, [](auto&) {});
		{
			Owner owner(EnterGrain::KEEP);
			const gw::GranularTrace got = drive_round_trip(
			    owner, [](Owner& fx) { fx.startSkippingRendering(); }, [](Owner&) {});
			expect(got.samples).to_equal(want.samples);
			expect(got.sends).to_equal(want.sends);
			expect(got.volumes).to_equal(want.volumes);
			expect(state_words(owner.fx())).to_equal(reference_words(*reference));
			expect(heap.state_allocations).to_equal(1);
			expect(heap.ring_allocations).to_equal(1);
		}
		expect(heap.releases).to_equal(heap.allocations);
	});

	it("keeps a chain's State across GRAIN, chorus, GRAIN, without a clear", _ {
		heap.reset();
		const auto reference = std::make_unique<ReferenceGranular>();
		const gw::GranularTrace want = drive_round_trip(
		    *reference, [](auto& fx) { fx.startSkippingRendering(); }, [](auto&) {});
		{
			Owner owner(EnterGrain::KEEP);
			const gw::GranularTrace got = drive_round_trip(
			    owner, [](Owner& fx) { fx.change(ModFXType::CHORUS); }, [](Owner& fx) { fx.change(ModFXType::GRAIN); });
			expect(got.samples).to_equal(want.samples);
			expect(got.sends).to_equal(want.sends);
			expect(got.volumes).to_equal(want.volumes);
			expect(state_words(owner.fx())).to_equal(reference_words(*reference));
			expect(heap.state_allocations).to_equal(1);
			expect(heap.ring_allocations).to_equal(2);
		}
		expect(heap.releases).to_equal(heap.allocations);
	});

	it("keeps a chain's grains and first pass across a soft off, as today's startSkippingRendering does", _ {
		heap.reset();
		const gw::GranularWorkload run = prefix_of("KNOB_SWEEP", 299);
		const auto reference = std::make_unique<ReferenceGranular>();
		std::ignore = gw::play(run, *reference);
		Owner owner(EnterGrain::KEEP);
		std::ignore = gw::play(run, owner);
		reference->startSkippingRendering();
		owner.startSkippingRendering();
		const std::vector<uint32_t> got = state_words(owner.fx(), false);
		expect(got).to_equal(reference_words(*reference, false));
		expect(owner.fx().state()->grains[0].length).not_().to_equal(0);
		expect(owner.fx().holdsBlock()).to_equal(true);
		expect(owner.fx().state()->initialized).to_equal(true);
		expect(owner.fx().state()->written).to_equal(0u);
	});

	it("reports the samples to shutdown as laps times the ring, as today's does", _ {
		heap.reset();
		const gw::GranularWorkload run = prefix_of("TAIL", 100);
		const auto reference = std::make_unique<ReferenceGranular>();
		std::ignore = gw::play(run, *reference);
		Owner owner;
		std::ignore = gw::play(run, owner);
		expect(owner.fx().samplesToShutdown()).to_equal(reference->getSamplesToShutdown());
		expect(owner.fx().samplesToShutdown()).not_().to_equal(0);
	});
});
// clang-format on

CPPSPEC_SPEC(granular)
