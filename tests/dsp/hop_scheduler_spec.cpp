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

// The live pitch shifter's hop scheduler (dsp-next/fixed/pitch_shift/hop_scheduler.h) over every workload, rendering
// its heads as the shifter does: each block's hop count and the hop's progress after it (the samples until the hop
// ends and into it, the crossfade's progress and step, the next crossfade, the onset threshold) are hashed against the
// scheduler's records (hop_scheduler_records.h; HOP_SCHEDULER_PRINT_RECORDS=1 prints them), and on every block the
// segments must tile the block and no head may pass now, the frame not yet written (Q14).

#include "hop_scheduler_records.h"
#include "pitch_shift_workloads.h"

#include "dsp-next/core/stereo.h"
#include "dsp-next/fixed/interpolation/windowed_sinc_kernel.h"
#include "dsp-next/fixed/pitch_shift/hop_scheduler.h"

#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <type_traits>
#include <vector>

namespace {
namespace pw = pitch_shift_workloads;
namespace ps = deluge::dsp::fixed::pitch_shift;
using deluge::dsp::Q;
using deluge::dsp::StereoSample;
using deluge::synth::InputChannel;

struct Tally {
	std::size_t blocks = 0;
	std::size_t split_blocks = 0; ///< Blocks cut into more than one segment.
	std::size_t hops = 0;
};

// The scheduler and its heads, rendered as the shifter renders them, over workload @p w: false if a block's segments do
// not tile it or a head passes now; @p record hashes each block's hops and progress.
template <typename Frame>
bool schedules(const pw::Workload& w, Tally& tally, pw::Fnv& record) {
	constexpr std::size_t kChannels = std::is_same_v<Frame, StereoSample> ? 2 : 1;
	using Kernel = ps::PlayHead<Frame>;
	pw::InputRing ring;
	auto history = std::make_unique<ps::InputHistory<Frame>>();
	auto scheduler = std::make_unique<ps::HopScheduler<Frame>>(w.phase_at(0));
	auto heads = std::make_unique<ps::PlayHeads<Frame>>();
	heads->newer.template on<&Kernel::note_percussiveness_entry>(ps::kNoPercussivenessEntry);
	deluge::dsp::fixed::Cong noise_a(pw::kNoiseSeed);
	std::vector<Q<31>> out_a(static_cast<std::size_t>(w.block_length) * kChannels);
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
			return true; // The workloads that reach unity remove the shifter there: the schedule is followed up to it
		}
		const int32_t* read_pos = ring.write(w.signal, time, static_cast<std::size_t>(w.block_length));
		// The history ingests the block as the shifter's input does
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
			// A repitching head reads frames before its cursor: past now it would read the input a ring ago. The older
			// head counts only while it sounds; after its crossfade it stands still
			const auto passed_now = [&](const deluge::dsp::Mono<Kernel>& head) {
				return head.state().kind == ps::HeadKind::REPITCHING
				       && history->now().since(head.state().cursor) > ps::kHistoryFrames / 2;
			};
			if (passed_now(heads->newer) || (s->older_sounding && passed_now(heads->older))) {
				return false;
			}
			covered += s->length;
			++segments;
		}
		time += static_cast<uint32_t>(w.block_length);
		const auto& p = scheduler->progress();
		if (covered != w.block_length) {
			return false;
		}
		for (const uint32_t word :
		     {static_cast<uint32_t>(scheduler->hops()), static_cast<uint32_t>(p.samples_til_hop_end),
		      static_cast<uint32_t>(p.samples_into_hop), p.crossfade_progress, p.crossfade_increment,
		      static_cast<uint32_t>(p.next_crossfade_length), static_cast<uint32_t>(p.percussive_threshold),
		      noise_a.state()}) {
			record.add(word);
		}
		for (const Q<31> word : out_a) {
			record.add(static_cast<uint32_t>(word.raw()));
		}
		++tally.blocks;
		tally.split_blocks += segments > 1 ? 1 : 0;
		tally.hops += static_cast<std::size_t>(scheduler->hops());
	}
	return true;
}
} // namespace

// clang-format off
describe hop_scheduler("deluge::dsp::fixed::pitch_shift::HopScheduler", ${
	it("schedules every workload as recorded, tiling each block, no head passing now", _ {
		const char* print = std::getenv("HOP_SCHEDULER_PRINT_RECORDS");
		const bool printing = print != nullptr && std::strcmp(print, "1") == 0;
		Tally tally;
		for (const pw::Workload& w : pw::all_workloads()) {
			if (w.shared) {
				continue; // One shifter per scheduler: the shared workloads are the component's to check
			}
			pw::Fnv record;
			const bool held = w.channel == InputChannel::STEREO ? schedules<StereoSample>(w, tally, record)
			                                                    : schedules<Q<31>>(w, tally, record);
			expect(held).to_be_true();
			if (printing) {
				std::cout << "    0x" << std::hex << record.value() << std::dec << "ULL, // " << w.name << "\n";
			}
			else {
				expect(record.value()).to_equal(kSchedulerRecords.at(static_cast<std::size_t>(w.id)));
			}
		}
		expect(tally.split_blocks).to_be_greater_than(std::size_t{100});
		expect(tally.hops).to_be_greater_than(std::size_t{1000});
	});
});
// clang-format on

CPPSPEC_MAIN(hop_scheduler);
