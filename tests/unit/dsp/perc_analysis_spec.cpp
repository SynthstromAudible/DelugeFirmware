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

// The time stretcher's perc analysis (dsp/fixed/timestretch/perc_analysis.h) against the legacy stretcher's records:
// the perc store's size (P1), the shared slope follower over the words the legacy fill read (P2), each pixel's byte
// (P3), whole recorded runs whole and split, and the zones a fill continues or opens (P5).

#include "dsp/fixed/timestretch/perc_analysis.h"

#include "time_stretcher_frames.h"
#include "time_stretcher_records.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "support/spec_main.h"

namespace {
using deluge::dsp::Q;
using deluge::dsp::fixed::pitch_shift::percussiveness;
using deluge::dsp::fixed::sample_read::Direction;
using deluge::dsp::fixed::sample_read::FrameRun;
namespace ts = deluge::dsp::fixed::timestretch;
namespace r = time_stretcher_records;
using time_stretcher_frames::RecordFrames;

static_assert(std::is_trivially_copyable_v<ts::PercState> && sizeof(ts::PercState) == 16);

Direction dir(int32_t d) {
	return d == 1 ? Direction::FORWARD : Direction::REVERSE;
}

ts::PercState state_of(const std::array<int32_t, 4>& s) {
	ts::PercState st{};
	st.follower.last = Q<31>::from_raw(s[0]);
	st.follower.poles[0] = Q<31>::from_raw(s[1]);
	st.follower.poles[1] = Q<31>::from_raw(s[2]);
	st.previous_slope = Q<31>::from_raw(s[3]);
	return st;
}

std::vector<uint8_t> pixels_of(r::Span s) {
	return {r::kPercBytes.begin() + s.offset, r::kPercBytes.begin() + s.offset + s.count};
}

// Runs @p row's frames through analyse, the runs split every @p split frames; the rows that differ.
int32_t analysis_mismatches(int32_t split) {
	int32_t bad = 0;
	for (const auto& row : r::kPercAnalysisGoldens) {
		const RecordFrames frames(row.frames, split);
		ts::PercState st = state_of(row.state_before);
		std::vector<uint8_t> pixels = pixels_of(row.pixels_before);
		int32_t at = row.first_frame;
		int32_t left = row.frame_count;
		while (left > 0) {
			const auto run = frames.run(at, dir(row.direction));
			if (!run) {
				bad++;
				break;
			}
			const int32_t n = std::min(left, run->frames);
			ts::analyse(*run, at, n, dir(row.direction), st, pixels, row.pixel_origin);
			at += n * row.direction;
			left -= n;
		}
		const ts::PercState want = state_of(row.state_after);
		bad += (st.follower.last != want.follower.last || st.follower.poles != want.follower.poles
		        || st.previous_slope != want.previous_slope || pixels != pixels_of(row.pixels_after))
		           ? 1
		           : 0;
	}
	return bad;
}

std::vector<ts::PercZoneSpan> zones_of(r::Span s) {
	std::vector<ts::PercZoneSpan> zones;
	for (uint32_t i = 0; i + 2 < s.count; i += 3) {
		zones.push_back({r::kWords[s.offset + i], r::kWords[s.offset + i + 1], r::kWords[s.offset + i + 2]});
	}
	return zones;
}
} // namespace

// clang-format off
describe perc_analysis_spec_body("deluge::dsp::fixed::timestretch::analyse", ${
	it("sizes the store as the legacy did", _ {
		int32_t bad = 0;
		for (const auto* rows : {&r::kPercStoreGoldens}) {
			for (const auto& row : *rows) {
				const auto l = ts::perc_store_layout(row.length_frames, row.chunk_size);
				bad += (l.bytes != row.bytes || l.chunked != row.chunked || l.chunks != row.chunks) ? 1 : 0;
			}
		}
		for (const auto& row : r::kPercStoreSweep) {
			const auto l = ts::perc_store_layout(row.length_frames, row.chunk_size);
			bad += (l.bytes != row.bytes || l.chunked != row.chunked || l.chunks != row.chunks) ? 1 : 0;
		}
		expect(bad).to_equal(0);
	});

	it("gives the hand-checked store sizes", _ {
		const auto a = ts::perc_store_layout(1, 32768);
		const auto b = ts::perc_store_layout(2097024, 32768);
		const auto c = ts::perc_store_layout(2097025, 32768);
		const auto d = ts::perc_store_layout(4194305, 32768);
		expect(a.bytes).to_equal(1);
		expect(a.chunked ? 1 : 0).to_equal(0);
		expect(b.bytes).to_equal(16383);
		expect(b.chunked ? 1 : 0).to_equal(0);
		expect(c.bytes).to_equal(16384);
		expect(c.chunked ? 1 : 0).to_equal(1);
		expect(c.chunks).to_equal(1);
		expect(d.bytes).to_equal(32769);
		expect(d.chunks).to_equal(2);
	});

	it("folds and follows the slope as the shared follower does (P2)", _ {
		int32_t bad = 0;
		for (const auto& row : r::kSlopeGoldens) {
			const RecordFrames frames(row.frames);
			const int32_t first = row.direction == 1 ? row.frames.first_frame : row.frames.last_frame;
			const auto run = frames.run(first, dir(row.direction));
			ts::PercState st = state_of({row.last, row.pole0, row.pole1, 0});
			for (uint32_t k = 0; k < row.slopes.count; k++) {
				const Q<31> slope = ts::SlopeFollower::step({}, {}, st.follower, ts::fold(*run, static_cast<int32_t>(k)));
				bad += slope.raw() == r::kWords[row.slopes.offset + k] ? 0 : 1;
			}
		}
		expect(r::kSlopeGoldens.size() > 0 ? 1 : 0).to_equal(1);
		expect(bad).to_equal(0);
	});

	it("measures each pixel's byte as the legacy did from its slopes (P3)", _ {
		int32_t bad = 0;
		for (const auto& row : r::kPercByteGoldens) {
			bad += percussiveness(Q<31>::from_raw(row.slope), Q<31>::from_raw(row.previous)) == row.byte ? 0 : 1;
		}
		expect(bad).to_equal(0);
	});

	it("analyses each recorded run as the shared follower does", _ {
		expect(r::kPercAnalysisGoldens.size() > 0 ? 1 : 0).to_equal(1);
		expect(analysis_mismatches(INT32_MAX)).to_equal(0);
	});

	it("analyses a run split at chunk boundaries as one run", _ {
		expect(analysis_mismatches(1)).to_equal(0);
		expect(analysis_mismatches(7)).to_equal(0);
		expect(analysis_mismatches(128)).to_equal(0);
	});

	it("writes a pixel at its middle frame, 64 forwards and 63 reversed, reading no frame before 0", _ {
		std::vector<int32_t> words(256, 0);
		for (int32_t f = 0; f < 256; f++) {
			words[f] = (f * 7919) << 16;
		}
		const FrameRun forward{reinterpret_cast<const std::byte*>(words.data()), 256, 4, 1, 4};
		std::vector<uint8_t> pixels(4, 0xEE);
		ts::PercState st{};
		ts::analyse(forward, 0, 256, Direction::FORWARD, st, pixels, 0);
		expect(pixels[0] != 0xEE ? 1 : 0).to_equal(1);
		expect(pixels[1] != 0xEE ? 1 : 0).to_equal(1);
		expect(static_cast<int32_t>(pixels[2])).to_equal(0xEE);
		const FrameRun reversed{reinterpret_cast<const std::byte*>(&words[255]), 256, -4, 1, 4};
		std::vector<uint8_t> back(4, 0xEE);
		ts::PercState rs{};
		ts::analyse(reversed, 255, 256, Direction::REVERSE, rs, back, 0);
		expect(back[0] != 0xEE ? 1 : 0).to_equal(1);
		expect(back[1] != 0xEE ? 1 : 0).to_equal(1);
		expect(static_cast<int32_t>(back[2])).to_equal(0xEE);
	});

	it("continues or opens zones as the legacy did (P5)", _ {
		int32_t bad = 0;
		for (const auto& row : r::kZoneForFillGoldens) {
			const auto zones = zones_of(row.zones);
			const ts::ZoneStep step = ts::zone_for_fill(zones, row.from_frame, dir(row.direction));
			bad += (static_cast<uint8_t>(step.kind) != row.kind || step.zone != row.zone || step.fill_from != row.fill_from)
			           ? 1 : 0;
		}
		for (const auto& row : r::kZoneOverwriteGoldens) {
			const ts::PercZoneSpan growing{row.growing[0], row.growing[1], row.growing[2]};
			const ts::PercZoneSpan next{row.next[0], row.next[1], row.next[2]};
			bad += ts::overwrites(growing, next, dir(row.direction)) == row.overwrites ? 0 : 1;
		}
		expect(r::kZoneOverwriteGoldens.size() > 0 ? 1 : 0).to_equal(1);
		expect(bad).to_equal(0);
	});

	it("continues a zone up to 2,048 frames past its end, both ways", _ {
		const std::vector<ts::PercZoneSpan> fwd{{0, 10000, 2048}};
		const auto a = ts::zone_for_fill(fwd, 12048, Direction::FORWARD);
		expect(static_cast<int32_t>(a.kind)).to_equal(static_cast<int32_t>(ts::ZoneStep::Kind::CONTINUE));
		expect(a.zone).to_equal(0);
		expect(a.fill_from).to_equal(10000);
		expect(static_cast<int32_t>(ts::zone_for_fill(fwd, 12049, Direction::FORWARD).kind))
		    .to_equal(static_cast<int32_t>(ts::ZoneStep::Kind::NEW));
		const std::vector<ts::PercZoneSpan> rev{{20000, 10000, 2048}};
		const auto b = ts::zone_for_fill(rev, 7952, Direction::REVERSE);
		expect(static_cast<int32_t>(b.kind)).to_equal(static_cast<int32_t>(ts::ZoneStep::Kind::CONTINUE));
		expect(b.fill_from).to_equal(10000);
		expect(static_cast<int32_t>(ts::zone_for_fill(rev, 7951, Direction::REVERSE).kind))
		    .to_equal(static_cast<int32_t>(ts::ZoneStep::Kind::NEW));
	});
});
// clang-format on

DELUGE_SPEC(perc_analysis_spec, perc_analysis_spec_body)
