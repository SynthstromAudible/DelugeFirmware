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

// The live pitch shifter's hop scheduler (dsp-next/fixed/pitch_shift/hop_scheduler.h) against the oracle's shifter
// (reference_live_pitch_shifter.h) over every workload: after each block, the hop's progress (the samples until the
// hop ends and into it, the crossfade's progress and step, the next crossfade, the onset threshold) and the block's
// hop count must equal the oracle's, and the block's segments must tile it.

#include "pitch_shift_workloads.h"
#include "reference_live_pitch_shifter.h"

#include "dsp-next/core/stereo.h"
#include "dsp-next/fixed/interpolation/windowed_sinc_kernel.h"
#include "dsp-next/fixed/pitch_shift/hop_scheduler.h"

#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace {
namespace pw = pitch_shift_workloads;
namespace ps = deluge::dsp::fixed::pitch_shift;
namespace ref = deluge::dsp::test::reference_pitch_shift;
using deluge::dsp::Q;
using deluge::dsp::StereoSample;
using deluge::synth::InputChannel;

struct Tally {
	std::size_t blocks = 0;
	std::size_t split_blocks = 0; ///< Blocks cut into more than one segment.
	std::size_t hops = 0;
};

// The scheduler and its heads, rendered as the shifter renders them, beside the oracle, over workload @p w
template <typename Frame>
bool schedules_as_the_oracle(const pw::Workload& w, Tally& tally) {
	constexpr std::size_t kChannels = std::is_same_v<Frame, StereoSample> ? 2 : 1;
	using Kernel = ps::PlayHead<Frame>;
	pw::InputRing ring;
	auto history = std::make_unique<ps::InputHistory<Frame>>();
	pw::PooledBuffer<ref::ReferenceInputBuffer> reference_buffer(w.channel);
	auto oracle = std::make_unique<ref::ReferenceLivePitchShifter>(w.channel, w.phase_at(0));
	auto scheduler = std::make_unique<ps::HopScheduler<Frame>>(w.phase_at(0));
	auto heads = std::make_unique<ps::PlayHeads<Frame>>();
	heads->newer.template on<&Kernel::note_percussiveness_entry>(ps::kNoPercussivenessEntry);
	deluge::dsp::fixed::Cong noise_a(pw::kNoiseSeed);
	deluge::dsp::fixed::Cong noise_b(pw::kNoiseSeed);
	std::vector<Q<31>> out_a(static_cast<std::size_t>(w.block_length) * kChannels);
	std::vector<Q<31>> out_b(out_a.size());
	uint32_t time = 0;
	const auto configure = [&](ps::PlayHeads<Frame>& h, int32_t phase, int32_t kernel) {
		for (auto* head : {&h.newer, &h.older}) {
			head->configure({.history = history.get(),
			                 .phase_increment = static_cast<uint32_t>(phase),
			                 .kernel_variant = kernel,
			                 .kind = head->state().kind,
			                 .linear = w.interpolation_size <= 2});
		}
	};
	for (int32_t b = 0; b < w.blocks; ++b) {
		const int32_t phase = w.phase_at(b);
		if (phase == pw::kUnity) {
			return true; // The workloads that reach unity remove the shifter there: the schedule is compared up to it
		}
		const int32_t* read_pos = ring.write(w.signal, time, static_cast<std::size_t>(w.block_length));
		// Both histories ingest the block as their shifters do
		std::vector<Frame> frames(static_cast<std::size_t>(w.block_length));
		for (std::size_t i = 0; i < frames.size(); ++i) {
			const int32_t* f = read_pos + (i * NUM_MONO_INPUT_CHANNELS);
			if (f >= ring.end()) {
				f -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
			}
			if constexpr (std::is_same_v<Frame, StereoSample>) {
				frames[i] = {.l = Q<31>::from_raw(f[0]), .r = Q<31>::from_raw(f[1])};
			}
			else {
				frames[i] = Q<31>::from_raw(f[w.channel == InputChannel::RIGHT ? 1 : 0]);
			}
		}
		history->write(frames);
		std::fill(out_a.begin(), out_a.end(), Q<31>{});
		std::fill(out_b.begin(), out_b.end(), Q<31>{});
		const int32_t hops_b = oracle->render(
		    out_b.data(), w.block_length, phase, w.amplitude, w.amplitude_increment, w.interpolation_size,
		    ref::ReferencePort{.time = time, .read_pos = read_pos, .end = ring.end()}, reference_buffer.get(), noise_b);
		const int32_t kernel = deluge::dsp::fixed::WindowedSincKernel::variant_for(phase);
		configure(*heads, phase, kernel);
		scheduler->begin_block({.length = w.block_length,
		                        .phase_increment = phase,
		                        .amplitude = Q<31>::from_raw(w.amplitude),
		                        .amplitude_step = Q<31>::from_raw(w.amplitude_increment)},
		                       *history);
		int32_t covered = 0;
		int32_t segments = 0;
		while (const auto s = scheduler->next(*history, *heads, noise_a)) {
			if (s->length <= 0) {
				return false;
			}
			std::span<Q<31>> words{out_a.data() + (static_cast<std::size_t>(covered) * kChannels),
			                       static_cast<std::size_t>(s->length) * kChannels};
			const auto render = [&](deluge::dsp::Mono<Kernel>& head, Q<31> a, Q<31> step) {
				configure(*heads, phase, kernel);
				head.template on<&Kernel::start_segment>(a, step);
				if constexpr (kChannels == 2) {
					const auto f = deluge::dsp::as_frames(words);
					head.render_block(f, f);
				}
				else {
					head.render_block(words, words);
				}
			};
			render(heads->newer, s->newer, s->newer_step);
			if (s->older_sounding) {
				render(heads->older, s->older, s->older_step);
			}
			covered += s->length;
			++segments;
		}
		time += static_cast<uint32_t>(w.block_length);
		const auto& p = scheduler->progress();
		if (covered != w.block_length || scheduler->hops() != hops_b
		    || p.samples_til_hop_end != oracle->samples_til_hop_end || p.samples_into_hop != oracle->samples_into_hop
		    || p.crossfade_progress != oracle->crossfade_progress
		    || p.crossfade_increment != oracle->crossfade_increment
		    || p.next_crossfade_length != oracle->next_crossfade_length
		    || p.percussive_threshold != oracle->perc_threshold_for_cut || noise_a.state() != noise_b.state()) {
			return false;
		}
		for (std::size_t i = 0; i < out_a.size(); ++i) {
			if (out_a[i].raw() != out_b[i].raw()) {
				return false;
			}
		}
		++tally.blocks;
		tally.split_blocks += segments > 1 ? 1 : 0;
		tally.hops += static_cast<std::size_t>(hops_b);
	}
	return true;
}
} // namespace

// clang-format off
describe hop_scheduler("deluge::dsp::fixed::pitch_shift::HopScheduler", ${
	it("schedules every workload's hops and crossfades as the oracle did, block by block", _ {
		Tally tally;
		for (const pw::Workload& w : pw::all_workloads()) {
			if (w.shared) {
				continue; // One shifter per scheduler: the shared workloads are the component's to check
			}
			const bool same = w.channel == InputChannel::STEREO ? schedules_as_the_oracle<StereoSample>(w, tally)
			                                                    : schedules_as_the_oracle<Q<31>>(w, tally);
			expect(same).to_be_true();
		}
		expect(tally.split_blocks).to_be_greater_than(std::size_t{100});
		expect(tally.hops).to_be_greater_than(std::size_t{1000});
	});
});
// clang-format on

CPPSPEC_MAIN(hop_scheduler);
