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

// The playback-side time stretcher (synth/sources/sample/sample_stretcher.h) over a fake port: its three begins, its
// head phase, its hops in hopEnd's order, its failed hops (S4), its end past the waveform (B6), its restart (S5), its
// reservations, its lent cache tails, its end from any phase, a saturated ratio, and its exit (E1).

#include "synth/sources/sample/sample_stretcher.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include "fake_stretch_port.h"
#include "time_stretcher_records.h"

#include "support/spec_main.h"

namespace {
namespace r = time_stretcher_records;
namespace ts = deluge::dsp::fixed::timestretch;
using deluge::dsp::Q;
using deluge::dsp::fixed::Cong;
using deluge::synth::Direction;
using deluge::synth::HopBudget;
using deluge::synth::HopClock;
using deluge::synth::Increment;
using deluge::synth::Ratio;
using deluge::synth::SampleStretcher;
using deluge::synth::SeatResult;
using deluge::synth::SourcePosition;
using deluge::synth::StretchAmplitude;
using deluge::synth::StretchContext;
using deluge::synth::StretchLeases;
using deluge::synth::TailBuffers;
using fake_stretch_port::FakeStretchPort;
using Head = deluge::synth::SampleStretchPort::Head;

static_assert(std::is_trivially_destructible_v<StretchLeases>);
#if defined(__arm__)
static_assert(sizeof(SampleStretcher) <= 208, "no larger than the legacy object, which held the older reader too");
#endif

constexpr uint32_t kSeed = 0x1234'5678;
constexpr int32_t kBlock = 128;

struct Rig {
	explicit Rig(int32_t length = 40'000) : port(length) {}
	FakeStretchPort port;
	HopBudget budget;
	Cong noise{kSeed};
	TailBuffers tails;
	StretchContext ctx{.budget = &budget, .noise = &noise, .tails = &tails};
	std::array<Q<31>, kBlock * 2> out{};
};

SampleStretcher::Motion motion(uint32_t ratio = 3u << 23, uint32_t increment = 1u << 24,
                               Direction d = Direction::FORWARD) {
	return {.ratio = Ratio::from_raw(ratio), .increment = Increment::from_raw(increment), .direction = d};
}

SampleStretcher::Part part(SampleStretcher::Motion m, int32_t frames = kBlock, LoopType loop = LoopType::NONE) {
	return {
	    .frames = frames, .motion = m, .loop = loop, .writing_cache = false, .cache_in_use = false, .out_channels = 2};
}

StretchAmplitude amplitude() {
	return {.amplitude = Q<31>::from_raw(1 << 26), .step = Q<31>::from_raw(0)};
}

/// Renders @p blocks blocks, each a part of kBlock frames with the budget starting afresh.
SampleStretcher::PartOutcome blocks(SampleStretcher& s, Rig& rig, SampleStretcher::Motion m, int32_t count,
                                    LoopType loop = LoopType::NONE) {
	SampleStretcher::PartOutcome outcome{.note_ended = false, .amplitude = amplitude()};
	for (int32_t i = 0; i < count && !outcome.note_ended; i++) {
		rig.budget.new_routine();
		outcome = s.render(rig.port, rig.ctx, part(m, kBlock, loop), rig.out, amplitude());
	}
	return outcome;
}

uint32_t draw(uint32_t seed, int32_t n) {
	Cong c{seed};
	uint32_t v = 0;
	for (int32_t i = 0; i < n; i++) {
		v = c.next();
	}
	return v;
}

ts::CacheWindow cache_window(const std::vector<std::byte>& cache, int32_t bytes_ahead) {
	return {.frame_origin = cache.data(), .chunk_offset = 0, .chunk_size = 32768, .bytes_written_ahead = bytes_ahead};
}
} // namespace

// clang-format off
describe sample_stretcher_spec_body("deluge::synth::SampleStretcher", ${
	it("begins plain with one draw, a shadow and a first hop at 200 ± 32 (S1, X3)", _ {
		Rig rig;
		SampleStretcher s;
		const bool begun = s.begin(rig.port, rig.ctx, SourcePosition(int64_t{5000} << 24), motion(), LoopType::NONE,
		                           SampleStretcher::Plain{});
		expect(begun ? 1 : 0).to_equal(1);
		expect(rig.noise.state()).to_equal(draw(kSeed, 1));
		expect(rig.port.shadows).to_equal(1);
		expect(s.clock().to_hop()).to_equal(HopClock::first_hop(draw(kSeed, 1)).to_hop());
		const int32_t back = deluge::synth::hop_back_frame(5000, Ratio::from_raw(3u << 23), Increment::from_raw(1u << 24),
		                                                   ts::hop_shape(Ratio::from_raw(3u << 23)), 44100,
		                                                   Direction::FORWARD, 40'000);
		expect(static_cast<int32_t>(rig.port.keep_calls.size())).to_equal(1);
		expect(rig.port.keep_calls[0].frame).to_equal(back);
	});

	it("reads the shadow once at level 0 in its first segment, then plays solo", _ {
		Rig rig;
		SampleStretcher s;
		(void)s.begin(rig.port, rig.ctx, SourcePosition(int64_t{5000} << 24), motion(), LoopType::NONE,
		              SampleStretcher::Plain{});
		(void)blocks(s, rig, motion(), 1);
		expect(static_cast<int32_t>(rig.port.reads.size())).to_equal(2);
		expect(static_cast<int32_t>(rig.port.reads[0].head)).to_equal(static_cast<int32_t>(Head::NEWER));
		expect(rig.port.reads[0].gain.level.raw()).to_equal((1 << 26) >> 1);
		expect(static_cast<int32_t>(rig.port.reads[1].head)).to_equal(static_cast<int32_t>(Head::OLDER));
		expect(rig.port.reads[1].gain.level.raw()).to_equal(0);
		expect(rig.port.reads[1].gain.step.raw()).to_equal(0);
		// 32 frames more: still before the first hop, 168 frames in at the earliest
		(void)s.render(rig.port, rig.ctx, part(motion(), 32), rig.out, amplitude());
		expect(static_cast<int32_t>(rig.port.reads.size())).to_equal(3);
		expect(static_cast<int32_t>(rig.port.reads[2].head)).to_equal(static_cast<int32_t>(Head::NEWER));
	});

	it("begins a fudge with no draw and no later hop (S2)", _ {
		Rig rig;
		rig.port.start_byte = FakeStretchPort::kDataStart + (3000 * 4);
		SampleStretcher s;
		const bool begun = s.begin(rig.port, rig.ctx, SourcePosition(int64_t{9000} << 24),
		                           motion(1u << 24, 1u << 25), LoopType::NONE, SampleStretcher::Fudge{40});
		expect(begun ? 1 : 0).to_equal(1);
		expect(rig.noise.state()).to_equal(kSeed);
		expect(rig.port.seat_calls[0].frame).to_equal(3000 - deluge::synth::fudge_into(40, Increment::from_raw(1u << 25)));
		expect(s.clock().to_hop()).to_equal(INT32_MAX);
		expect(s.is_fudge() ? 1 : 0).to_equal(1);
	});

	it("fails a fudge whose seat fails", _ {
		Rig rig;
		rig.port.start_byte = FakeStretchPort::kDataStart + (3000 * 4);
		rig.port.seats = {SeatResult::NOT_READY};
		SampleStretcher s;
		expect(s.begin(rig.port, rig.ctx, SourcePosition(int64_t{9000} << 24), motion(1u << 24), LoopType::NONE,
		               SampleStretcher::Fudge{40}) ? 1 : 0).to_equal(0);
	});

	it("hops through the planner, the aligner and the seat in hopEnd's order", _ {
		Rig rig;
		SampleStretcher s;
		(void)s.begin(rig.port, rig.ctx, SourcePosition(int64_t{5000} << 24), motion(), LoopType::NONE,
		              SampleStretcher::Plain{});
		const int32_t marks = rig.port.mark_count();
		(void)blocks(s, rig, motion(), 2); // 256 frames: past the first hop, at most 231
		expect(static_cast<int32_t>(rig.port.seat_calls.size())).to_equal(1);
		expect(rig.port.mark_count() - marks).to_equal(1);
		expect(rig.noise.state()).to_equal(draw(kSeed, 2)); // the begin's draw, and the hop's
		expect(s.newer_reading() ? 1 : 0).to_equal(1);
		// Crossfading: the next segment reads both heads, the older at a gain above 0
		const auto before = rig.port.reads.size();
		(void)blocks(s, rig, motion(), 1);
		expect(static_cast<int32_t>(rig.port.reads.size() - before) >= 2 ? 1 : 0).to_equal(1);
		expect(static_cast<int32_t>(rig.port.reads[before + 1].head)).to_equal(static_cast<int32_t>(Head::OLDER));
	});

	it("keeps the newer head and retries in 500 frames when a seat is not ready (S4)", _ {
		// The records: every failed seat leaves the older stopped, the ramp still, and the hop 500 frames on
		std::size_t bad = 0;
		for (const auto& row : r::kSeatFaults) {
			if (!row.fudge && (row.outcome == 1 || row.outcome == 2)) {
				bad += (row.to_hop == 500 && row.ramp_increment == 0 && !row.older_reading_after) ? 0 : 1;
			}
		}
		expect(bad).to_equal(std::size_t{0});
		Rig rig;
		rig.port.seats = {SeatResult::NOT_READY};
		SampleStretcher s;
		(void)s.begin(rig.port, rig.ctx, SourcePosition(int64_t{5000} << 24), motion(), LoopType::NONE,
		              SampleStretcher::Plain{});
		rig.port.missing_from = 0; // the start's frame is not resident: the retry reservation is kept there
		(void)blocks(s, rig, motion(), 2);
		expect(static_cast<int32_t>(rig.port.seat_calls.size())).to_equal(1);
		expect(s.newer_reading() ? 1 : 0).to_equal(1);
		expect(s.clock().to_hop() > 500 - 2 * kBlock && s.clock().to_hop() <= 500 ? 1 : 0).to_equal(1);
		expect(rig.port.keep_calls[1].frame).to_equal(rig.port.seat_calls[0].frame);
		const auto reads = rig.port.reads.size();
		(void)blocks(s, rig, motion(), 1);
		expect(static_cast<int32_t>(rig.port.reads.size() - reads)).to_equal(1); // the newer alone
		rig.port.missing_from.reset();
		(void)blocks(s, rig, motion(), 4); // the retry: a second attempt, a second draw
		expect(static_cast<int32_t>(rig.port.seat_calls.size())).to_equal(2);
		expect(rig.noise.state()).to_equal(draw(kSeed, 3));
	});

	it("silences the new head past the end (B6)", _ {
		Rig rig(6000);
		SampleStretcher s;
		const auto fast = motion(2u << 24);
		(void)s.begin(rig.port, rig.ctx, SourcePosition(int64_t{5900} << 24), fast, LoopType::NONE,
		              SampleStretcher::Plain{});
		(void)blocks(s, rig, fast, 2);
		expect(static_cast<int32_t>(rig.port.seat_calls.size())).to_equal(0);
		expect(s.newer_reading() ? 1 : 0).to_equal(0);
		expect(s.sounding() ? 1 : 0).to_equal(1); // Fading
		const SampleStretcher::PartOutcome ended = blocks(s, rig, fast, 400);
		expect(ended.note_ended ? 1 : 0).to_equal(1);
		expect(s.sounding() ? 1 : 0).to_equal(0); // Silent
	});

	it("restarts at the loop and hops at once if the newer has stopped (S5)", _ {
		std::size_t bad = 0;
		for (const auto& row : r::kRestartGoldens) {
			bad += row.hopped == !row.newer_reading ? 0 : 1;
		}
		expect(bad).to_equal(std::size_t{0});
		Rig rig;
		SampleStretcher s;
		(void)s.begin(rig.port, rig.ctx, SourcePosition(int64_t{5000} << 24), motion(), LoopType::LOW_LEVEL,
		              SampleStretcher::Plain{});
		s.wrap_to(rig.port, rig.ctx, SourcePosition(int64_t{100} << 24), part(motion(), kBlock, LoopType::LOW_LEVEL));
		expect(static_cast<int32_t>(rig.port.seat_calls.size())).to_equal(0); // the newer reads: the next hop will do
		rig.port.newer_alive = false;
		(void)blocks(s, rig, motion(), 1, LoopType::LOW_LEVEL);
		expect(s.newer_reading() ? 1 : 0).to_equal(0);
		rig.port.newer_alive = true;
		s.wrap_to(rig.port, rig.ctx, SourcePosition(int64_t{100} << 24), part(motion(), kBlock, LoopType::LOW_LEVEL));
		expect(static_cast<int32_t>(rig.port.seat_calls.size())).to_equal(1);
		expect(s.newer_reading() ? 1 : 0).to_equal(1);
	});

	it("keeps each reservation where the legacy did", _ {
		std::size_t bad = 0;
		std::size_t hop_back = 0;
		for (const auto& row : r::kReservationGoldens) {
			if (row.kind != 0) {
				continue;
			}
			hop_back++;
			const ts::HopShape shape{.min_width = row.min_width, .max_width = row.max_width,
			                         .crossfade_share = Q<31>::from_raw(0), .crossfade_quarters = 0,
			                         .jitter_share = Q<23>::from_raw(0)};
			bad += deluge::synth::hop_back_frame(row.position_frames, Ratio::from_raw(row.ratio),
			                                     Increment::from_raw(row.increment), shape, row.rate,
			                                     row.direction == 1 ? Direction::FORWARD : Direction::REVERSE,
			                                     row.length_frames)
			               == row.frame
			           ? 0
			           : 1;
		}
		expect(bad).to_equal(std::size_t{0});
		expect(hop_back > 0 ? 1 : 0).to_equal(1);
	});

	it("crossfades out of a cache tail it is lent (C1 to C4)", _ {
		Rig rig;
		const std::vector<std::byte> cache(6 * 300, std::byte{0x11});
		SampleStretcher s;
		(void)s.begin(rig.port, rig.ctx, SourcePosition(int64_t{5000} << 24), motion(), LoopType::NONE,
		              SampleStretcher::FromCacheTail{.window = cache_window(cache, 6 * 200), .channels = 2});
		expect(rig.tails.free_count()).to_equal(7);
		(void)blocks(s, rig, motion(), 1);
		expect(rig.port.reads.size() == 1 ? 1 : 0).to_equal(1); // the newer read; the tail mixed, not read
		(void)blocks(s, rig, motion(), 2);
		expect(rig.tails.free_count()).to_equal(8);
		SampleStretcher none;
		(void)none.begin(rig.port, rig.ctx, SourcePosition(int64_t{5000} << 24), motion(), LoopType::NONE,
		                 SampleStretcher::FromCacheTail{.window = cache_window(cache, 0), .channels = 2});
		expect(rig.tails.free_count()).to_equal(8);
	});

	it("begins a ninth crossfade out of a cache with no tail while eight are lent", _ {
		Rig rig;
		const std::vector<std::byte> cache(6 * 300, std::byte{0x22});
		std::array<SampleStretcher, 10> s{};
		for (int32_t i = 0; i < 9; i++) {
			(void)s[i].begin(rig.port, rig.ctx, SourcePosition(int64_t{5000} << 24), motion(), LoopType::NONE,
			                 SampleStretcher::FromCacheTail{.window = cache_window(cache, 6 * 200), .channels = 2});
		}
		expect(rig.tails.free_count()).to_equal(0);
		const auto reads = rig.port.reads.size();
		rig.budget.new_routine();
		(void)s[8].render(rig.port, rig.ctx, part(motion()), rig.out, amplitude());
		// Shadowed: the newer at its level and the shadow once at 0, as a plain begin
		expect(static_cast<int32_t>(rig.port.reads.size() - reads)).to_equal(2);
		expect(rig.port.reads[reads + 1].gain.level.raw()).to_equal(0);
		s[3].end(rig.port, rig.ctx);
		expect(rig.tails.free_count()).to_equal(1);
		(void)s[9].begin(rig.port, rig.ctx, SourcePosition(int64_t{5000} << 24), motion(), LoopType::NONE,
		                 SampleStretcher::FromCacheTail{.window = cache_window(cache, 6 * 200), .channels = 2});
		expect(rig.tails.free_count()).to_equal(0);
	});

	it("ends from any phase leaving no lease, tail or reservation, and begins afresh in place", _ {
		const std::vector<std::byte> cache(6 * 300, std::byte{0x33});
		std::size_t bad = 0;
		for (int32_t phase = 0; phase < 5; phase++) {
			Rig rig(phase == 3 || phase == 4 ? 6000 : 40'000);
			SampleStretcher s;
			const auto m = phase >= 3 ? motion(2u << 24) : motion();
			const int64_t at = phase >= 3 ? 5900 : 5000;
			if (phase == 2) {
				(void)s.begin(rig.port, rig.ctx, SourcePosition(at << 24), m, LoopType::NONE,
				              SampleStretcher::FromCacheTail{.window = cache_window(cache, 6 * 200), .channels = 2});
			}
			else {
				(void)s.begin(rig.port, rig.ctx, SourcePosition(at << 24), m, LoopType::NONE, SampleStretcher::Plain{});
			}
			if (phase == 1) {
				(void)blocks(s, rig, m, 1); // Solo
			}
			if (phase == 3) {
				(void)blocks(s, rig, m, 2); // Fading
			}
			if (phase == 4) {
				(void)blocks(s, rig, m, 400); // Silent
			}
			s.end(rig.port, rig.ctx);
			bad += rig.port.live_reservations == 0 ? 0 : 1;
			bad += rig.port.holds == rig.port.drops ? 0 : 1;
			bad += rig.tails.free_count() == 8 ? 0 : 1;
			rig.noise.seed(kSeed);
			(void)s.begin(rig.port, rig.ctx, SourcePosition(int64_t{5000} << 24), motion(), LoopType::NONE,
			              SampleStretcher::Plain{});
			bad += s.clock().to_hop() == HopClock::first_hop(draw(kSeed, 1)).to_hop() ? 0 : 1;
			bad += (s.newer_reading() && s.sounding() && !s.is_fudge()) ? 0 : 1;
		}
		expect(bad).to_equal(std::size_t{0});
	});

	it("renders at a saturated ratio without a zero divide", _ {
		Rig rig(4'000'000);
		SampleStretcher s;
		const auto far = motion(UINT32_MAX, 1u << 16);
		(void)s.begin(rig.port, rig.ctx, SourcePosition(int64_t{1000} << 24), far, LoopType::NONE,
		              SampleStretcher::Plain{});
		(void)blocks(s, rig, far, 64);
		std::size_t bad = 0;
		for (std::size_t i = 1; i < rig.port.seat_calls.size(); i++) {
			bad += rig.port.seat_calls[i].frame > rig.port.seat_calls[i - 1].frame ? 0 : 1;
		}
		expect(bad).to_equal(std::size_t{0});
		expect(rig.port.seat_calls.empty() ? 0 : 1).to_equal(1);
	});

	it("ends a one-block stretch at the next block when the cursors meet (E1, decision 5)", _ {
		Rig rig;
		rig.port.meet = true;
		SampleStretcher s;
		(void)s.begin(rig.port, rig.ctx, SourcePosition(int64_t{5000} << 24), motion(), LoopType::NONE,
		              SampleStretcher::Plain{});
		expect(s.may_end(rig.port, Ratio::from_raw(1u << 24), false, 0) ? 1 : 0).to_equal(0);
		(void)blocks(s, rig, motion(), 1);
		expect(s.may_end(rig.port, Ratio::from_raw(1u << 24), false, 0) ? 1 : 0).to_equal(1);
		rig.port.meet = false;
		expect(s.may_end(rig.port, Ratio::from_raw(1u << 24), false, 0) ? 1 : 0).to_equal(0);
	});

	it("hops unsynced when the clocks are stopped", _ {
		auto first_seat = [](std::optional<int64_t> synced) {
			Rig rig;
			rig.port.synced = synced;
			SampleStretcher s;
			(void)s.begin(rig.port, rig.ctx, SourcePosition(int64_t{5000} << 24), motion(), LoopType::NONE,
			              SampleStretcher::Plain{});
			(void)blocks(s, rig, motion(), 2);
			return rig.port.seat_calls.at(0).frame;
		};
		expect(first_seat(std::nullopt)).to_equal(first_seat(std::nullopt));
		expect(first_seat(int64_t{20'000}) != first_seat(std::nullopt) ? 1 : 0).to_equal(1);
	});
});
// clang-format on

DELUGE_SPEC(sample_stretcher_spec, sample_stretcher_spec_body)
