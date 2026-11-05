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

// The live input's history (dsp-next/fixed/pitch_shift/input_history.h) and the synth's LiveInput over it, against
// the pitch shifter's input buffer in the oracle (reference_live_pitch_shifter.h): the ring, the percussiveness entries
// and the count after every block, the moving averages and the written test, for each channel and block length, and
// LiveInput's once-per-block ingest and its restart when blocks are missed.

#include "pitch_shift_workloads.h"
#include "reference_live_pitch_shifter.h"

#include "dsp-next/fixed/pitch_shift/input_history.h"
#include "synth/sources/input/audio_input_port.h"
#include "synth/sources/input/live/live_input.h"

#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <type_traits>
#include <utility>

namespace {
namespace pw = pitch_shift_workloads;
namespace ref = deluge::dsp::test::reference_pitch_shift;
namespace ps = deluge::dsp::fixed::pitch_shift;
using deluge::dsp::Q;
using deluge::dsp::StereoSample;
using deluge::synth::InputChannel;

// The codec ring and clock a block reads, as a port
class RingPort final : public deluge::synth::AudioInputPort {
public:
	void at(uint32_t time, const int32_t* read_pos, const int32_t* end) {
		time_ = time;
		read_pos_ = read_pos;
		end_ = end;
	}
	[[nodiscard]] uint32_t sample_time() const override { return time_; }
	[[nodiscard]] bool stereo_input_available() const override { return true; }
	[[nodiscard]] const int32_t* ring_read_pos() const override { return read_pos_; }
	[[nodiscard]] const int32_t* ring_end() const override { return end_; }
	void report_hops(int32_t /*hops_ended*/) override {}

private:
	uint32_t time_ = 0;
	const int32_t* read_pos_ = nullptr;
	const int32_t* end_ = nullptr;
};

template <typename Frame>
[[nodiscard]] bool frame_matches(const Frame& got, const ref::ReferenceInputBuffer& want, uint32_t index) {
	if constexpr (std::is_same_v<Frame, StereoSample>) {
		return got.l.raw() == want.raw_buffer[index * 2] && got.r.raw() == want.raw_buffer[index * 2 + 1];
	}
	else {
		return got.raw() == want.raw_buffer[index];
	}
}

// Ingests @p blocks blocks of @p block_length through LiveInput and the reference buffer alike, comparing everything
// the history holds after each.
template <typename Frame>
bool ingests_as_the_buffer(InputChannel channel, pw::Signal signal, std::size_t block_length, std::size_t blocks) {
	pw::InputRing ring;
	RingPort port;
	auto input = std::make_unique<deluge::synth::LiveInput<Frame>>(channel);
	pw::PooledBuffer<ref::ReferenceInputBuffer> reference(channel);
	const int channels = channel == InputChannel::STEREO ? 2 : 1;
	uint32_t time = 0;
	for (std::size_t b = 0; b < blocks; ++b) {
		const int32_t* read_pos = ring.write(signal, time, block_length);
		port.at(time, read_pos, ring.end());
		input->ingest(static_cast<int32_t>(block_length), port);
		reference.get()->give_input(static_cast<int32_t>(block_length),
		                            ref::ReferencePort{.time = time, .read_pos = read_pos, .end = ring.end()}, channel);
		time += static_cast<uint32_t>(block_length);
		const auto& h = input->history();
		if (h.frames_written() != reference.get()->num_raw_samples_processed) {
			return false;
		}
		for (uint32_t e = 0; e < ps::kPercussivenessEntries; ++e) {
			if (h.percussiveness_at(e) != reference.get()->perc_buffer[e]) {
				return false;
			}
		}
		if (b % 16 == 15) {
			for (uint32_t i = 0; i < ps::kHistoryFrames; ++i) {
				if (!frame_matches(h.at(ps::RingPosition{i}), *reference.get(), i)) {
					return false;
				}
			}
			std::mt19937 rng(static_cast<uint32_t>(b));
			std::uniform_int_distribution<uint32_t> start(0, ps::kHistoryFrames - 1);
			std::uniform_int_distribution<int32_t> length(1, 70);
			for (int k = 0; k < 20; ++k) {
				const uint32_t s = start(rng);
				const int32_t len = length(rng);
				std::array<int32_t, ps::kMovingAverages> want{};
				reference.get()->get_averages_for_crossfade(want.data(), static_cast<int32_t>(s), len, channels);
				if (h.moving_averages(ps::RingPosition{s}, len) != want) {
					return false;
				}
				if (h.written(ps::RingPosition{s}) != std::cmp_less(s, reference.get()->num_raw_samples_processed)) {
					return false;
				}
			}
		}
	}
	return true;
}
} // namespace

// clang-format off
describe input_history("deluge::dsp::fixed::pitch_shift::InputHistory and deluge::synth::LiveInput", ${
	it("ingests each channel as the input buffer did, for every block length", _ {
		for (const std::size_t block : {std::size_t{1}, std::size_t{7}, std::size_t{128}, std::size_t{300}}) {
			const std::size_t blocks = (3 * ps::kHistoryFrames) / block + 1; // past three laps of the ring
			expect(ingests_as_the_buffer<Q<31>>(InputChannel::LEFT, pw::Signal::PERCUSSIVE, block, blocks)).to_be_true();
			expect(ingests_as_the_buffer<Q<31>>(InputChannel::RIGHT, pw::Signal::SINE, block, blocks)).to_be_true();
			expect(ingests_as_the_buffer<StereoSample>(InputChannel::STEREO, pw::Signal::PERCUSSIVE, block, blocks))
			    .to_be_true();
		}
	});

	it("ingests a block once however many voices ask, and starts over when blocks are missed", _ {
		pw::InputRing ring;
		RingPort port;
		auto input = std::make_unique<deluge::synth::MonoLiveInput>(InputChannel::LEFT);
		const int32_t* first = ring.write(pw::Signal::SAW, 0, 128);
		port.at(0, first, ring.end());
		input->ingest(128, port);
		input->ingest(128, port);
		expect(input->history().frames_written()).to_equal(128U);
		expect(input->ingested_to()).to_equal(128U);
		// The block at 256 skips the one at 128: the history starts over
		const int32_t* later = ring.write(pw::Signal::SAW, 128, 256);
		port.at(256, later + 128 * NUM_MONO_INPUT_CHANNELS, ring.end());
		input->ingest(128, port);
		expect(input->history().frames_written()).to_equal(128U);
		expect(input->ingested_to()).to_equal(384U);
	});

	it("wraps ring positions at the ring's length", _ {
		expect(ps::RingPosition{ps::kHistoryFrames + 5}.index()).to_equal(5U);
		expect((ps::RingPosition{3} - 5).index()).to_equal(ps::kHistoryFrames - 2);
		expect((ps::RingPosition{ps::kHistoryFrames - 1} + 2).index()).to_equal(1U);
		expect(ps::RingPosition{2}.since(ps::RingPosition{ps::kHistoryFrames - 3})).to_equal(5U);
	});
});
// clang-format on

CPPSPEC_MAIN(input_history);
