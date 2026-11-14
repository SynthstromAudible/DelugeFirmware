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

// The live pitch shifter's play head (dsp-next/fixed/pitch_shift/play_head.h) against the old play head in the oracle
// (reference_live_pitch_shifter.h), over the same history: placed at a hop, then rendered segment after segment with
// changing ramps, direct and repitching through each interpolation, mono and stereo, at shifts up and down. Every
// output word, the cursor and the phase must match after each segment.

#include "pitch_shift_workloads.h"
#include "reference_live_pitch_shifter.h"

#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/fixed/interpolation/windowed_sinc_kernel.h"
#include "dsp-next/fixed/pitch_shift/play_head.h"

#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <type_traits>
#include <vector>

namespace {
namespace pw = pitch_shift_workloads;
namespace ref = deluge::dsp::test::reference_pitch_shift;
namespace ps = deluge::dsp::fixed::pitch_shift;
using deluge::dsp::Q;
using deluge::dsp::StereoSample;
using deluge::synth::InputChannel;

// A history of @p frames of the percussive signal, and the oracle's buffer holding the same frames
template <typename Frame>
struct Histories {
	ps::InputHistory<Frame> history{};
	pw::PooledBuffer<ref::ReferenceInputBuffer> reference;
	explicit Histories(InputChannel channel, std::size_t frames) : reference(channel) {
		pw::InputRing ring;
		uint32_t time = 0;
		for (std::size_t done = 0; done < frames; done += 128) {
			const int32_t* read_pos = ring.write(pw::Signal::PERCUSSIVE, time, 128);
			std::vector<Frame> block(128);
			for (std::size_t i = 0; i < 128; ++i) {
				const int32_t* frame = read_pos + (i * NUM_MONO_INPUT_CHANNELS);
				if (frame >= ring.end()) {
					frame -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
				}
				if constexpr (std::is_same_v<Frame, StereoSample>) {
					block[i] = {.l = Q<31>::from_raw(frame[0]), .r = Q<31>::from_raw(frame[1])};
				}
				else {
					block[i] = Q<31>::from_raw(frame[0]);
				}
			}
			history.write(block);
			reference.get()->give_input(128, ref::ReferencePort{.time = time, .read_pos = read_pos, .end = ring.end()},
			                            channel);
			time += 128;
		}
	}
};

// Places both heads at @p cursor with @p phase, then renders segments of random lengths and ramps through both,
// comparing everything after each
template <typename Frame>
bool renders_as_the_old_head(ps::HeadKind kind, bool linear, uint32_t phase_increment, uint32_t seed) {
	constexpr int32_t kChannels = std::is_same_v<Frame, StereoSample> ? 2 : 1;
	const InputChannel channel = kChannels == 2 ? InputChannel::STEREO : InputChannel::LEFT;
	Histories<Frame> h(channel, 20000);
	std::mt19937 rng(seed);
	const auto cursor = static_cast<uint32_t>(std::uniform_int_distribution<int>(0, 2000)(rng));
	const auto phase = static_cast<uint32_t>(std::uniform_int_distribution<int>(0, (1 << 24) - 1)(rng));
	const int32_t which_kernel =
	    deluge::dsp::fixed::WindowedSincKernel::variant_for(static_cast<int32_t>(phase_increment));
	const int32_t interpolation_size = linear ? 2 : kInterpolationMaxNumSamples;

	ref::ReferencePlayHead old;
	old.mode = kind == ps::HeadKind::DIRECT ? ref::PlayHeadMode::RAW_DIRECT : ref::PlayHeadMode::RAW_REPITCHING;
	old.raw_buffer_read_pos = static_cast<int32_t>(cursor);
	if (kind == ps::HeadKind::REPITCHING) {
		old.fill_interpolation_buffer(h.reference.get(), kChannels);
		old.osc_pos = phase;
	}

	using Kernel = ps::PlayHead<Frame>;
	deluge::dsp::Mono<Kernel> head;
	const auto configure = [&] {
		head.configure({.history = &h.history,
		                .phase_increment = phase_increment,
		                .kernel_variant = which_kernel,
		                .kind = head.state().kind,
		                .linear = linear});
	};
	configure();
	head.template on<&Kernel::begin_hop>(
	    typename Kernel::Hop{.cursor = ps::RingPosition{cursor}, .phase = phase, .kind = kind});
	configure();

	std::uniform_int_distribution<int32_t> length(1, 128);
	// Ramps that stay in range over a segment, as a voice's do: the old head's ramp is a plain signed sum
	std::uniform_int_distribution<int32_t> amplitude(0x1000'0000, 0x6000'0000);
	std::uniform_int_distribution<int32_t> step(-0x40000, 0x40000);
	for (int segment = 0; segment < 60; ++segment) {
		const int32_t n = length(rng);
		const int32_t a = amplitude(rng);
		const int32_t inc = step(rng);
		std::vector<Q<31>> got(static_cast<std::size_t>(n * kChannels));
		for (auto& w : got) {
			w = Q<31>::from_raw(std::uniform_int_distribution<int32_t>()(rng));
		}
		std::vector<Q<31>> want = got;
		old.render(want.data(), n, kChannels, static_cast<int32_t>(phase_increment), a, inc, nullptr,
		           h.reference.get()->raw_buffer, which_kernel, interpolation_size);
		head.template on<&Kernel::start_segment>(Q<31>::from_raw(a), Q<31>::from_raw(inc));
		if constexpr (kChannels == 2) {
			const auto frames = deluge::dsp::as_frames(got);
			head.render_block(frames, frames);
		}
		else {
			head.render_block(got, got);
		}
		for (std::size_t i = 0; i < got.size(); ++i) {
			if (got[i].raw() != want[i].raw()) {
				return false;
			}
		}
		if (head.state().cursor.index() != static_cast<uint32_t>(old.raw_buffer_read_pos)) {
			return false;
		}
		if (kind == ps::HeadKind::REPITCHING && head.state().phase != old.osc_pos) {
			return false;
		}
	}
	return true;
}
} // namespace

// clang-format off
describe play_head("deluge::dsp::fixed::pitch_shift::PlayHead", ${
	it("renders direct as the old head did, mono and stereo", _ {
		expect(renders_as_the_old_head<Q<31>>(ps::HeadKind::DIRECT, false, 1U << 24, 1)).to_be_true();
		expect(renders_as_the_old_head<StereoSample>(ps::HeadKind::DIRECT, false, 1U << 24, 2)).to_be_true();
	});

	it("renders repitching through the windowed sinc as the old head did, up and down, mono and stereo", _ {
		for (const uint32_t shift : {pw::kDown24, pw::kDown12, pw::kDown1, pw::kUpHalf, pw::kUp7, pw::kUp24}) {
			expect(renders_as_the_old_head<Q<31>>(ps::HeadKind::REPITCHING, false, shift, shift)).to_be_true();
			expect(renders_as_the_old_head<StereoSample>(ps::HeadKind::REPITCHING, false, shift, shift + 1)).to_be_true();
		}
	});

	it("renders repitching linearly as the old head did, mono and stereo", _ {
		for (const uint32_t shift : {pw::kDown12, pw::kUp7, pw::kUp24}) {
			expect(renders_as_the_old_head<Q<31>>(ps::HeadKind::REPITCHING, true, shift, shift + 2)).to_be_true();
			expect(renders_as_the_old_head<StereoSample>(ps::HeadKind::REPITCHING, true, shift, shift + 3)).to_be_true();
		}
	});

	it("jumps by the last 16 frames of a jump past the window, as the old head did (quirk Q7)", _ {
		// Twenty frames per sample: every step jumps past the 16-tap window
		expect(renders_as_the_old_head<Q<31>>(ps::HeadKind::REPITCHING, false, 20U << 24, 9)).to_be_true();
		expect(renders_as_the_old_head<StereoSample>(ps::HeadKind::REPITCHING, true, 20U << 24, 10)).to_be_true();
	});

	it("satisfies the processor kernel concept, mono and stereo", _ {
		expect(deluge::dsp::processor_kernel<ps::PlayHead<Q<31>>>).to_be_true();
		expect(deluge::dsp::processor_kernel<ps::PlayHead<StereoSample>>).to_be_true();
	});
});
// clang-format on

CPPSPEC_MAIN(play_head);
