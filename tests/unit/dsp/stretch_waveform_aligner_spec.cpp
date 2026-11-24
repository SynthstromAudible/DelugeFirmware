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

// The time stretcher's waveform aligner (dsp/fixed/timestretch/waveform_aligner.h) against the legacy stretcher's
// records: the search's setup (A1), the moving averages (A2), the search's limit (A3), and each recorded alignment
// (A4 to A6), whole and with its frames split at chunk boundaries; and an abandoned search starting at phase 0
// (decision 7).

#include "dsp/fixed/timestretch/waveform_aligner.h"

#include "time_stretcher_frames.h"
#include "time_stretcher_records.h"

#include <climits>
#include <cppspec.hpp>
#include <cstdint>
#include <optional>
#include <vector>

#include "support/spec_main.h"

namespace {
using deluge::dsp::UQ;
using deluge::dsp::fixed::sample_read::Direction;
using deluge::dsp::fixed::sample_read::FrameRun;
using deluge::dsp::fixed::sample_read::Increment;
namespace ts = deluge::dsp::fixed::timestretch;
namespace r = time_stretcher_records;
using time_stretcher_frames::RecordFrames;

Direction dir(int32_t d) {
	return d == 1 ? Direction::FORWARD : Direction::REVERSE;
}

ts::AlignRequest request_of(const r::AlignRow& row) {
	return {.start_frame = row.start,
	        .direction = dir(row.direction),
	        .increment = Increment::from_raw(row.increment),
	        .setup = {row.each, row.crossfade_source},
	        .limit = row.limit,
	        .old_totals = row.old_totals,
	        .older_phase = UQ<24>::from_raw(row.older_phase)};
}

// The rows whose alignment differs from the record's, the frames split every @p split.
int32_t align_mismatches(int32_t split) {
	int32_t bad = 0;
	for (const auto& row : r::kAlignGoldens) {
		const RecordFrames frames(row.frames, split);
		const std::optional<ts::Alignment> a = ts::align(frames, request_of(row));
		bad += (!a || a->offset_frames != row.offset || a->phase.raw() != row.phase) ? 1 : 0;
	}
	return bad;
}

// A mono source of 64 frames that notes the lowest frame asked for.
struct Edge {
	std::vector<int32_t> words;
	mutable int32_t lowest = INT32_MAX;
	mutable int32_t runs = 0;
	int32_t missing_from = INT32_MAX;
	Edge() : words(64) {
		for (int32_t f = 0; f < 64; f++) {
			words[f] = ((f * 37) % 11 - 5) << 20;
		}
	}
	[[nodiscard]] std::optional<FrameRun> run(int32_t frame, Direction d) const {
		lowest = std::min(lowest, frame);
		runs++;
		if (frame < 0 || frame >= 64 || frame >= missing_from) {
			return std::nullopt;
		}
		const bool fwd = d == Direction::FORWARD;
		return FrameRun{reinterpret_cast<const std::byte*>(&words[frame]),
		                fwd ? std::min(64, missing_from) - frame : frame + 1, fwd ? 4 : -4, 1, 4};
	}
	[[nodiscard]] int32_t channels() const { return 1; }
	[[nodiscard]] int32_t length_frames() const { return 64; }
};
static_assert(deluge::dsp::fixed::sample_read::frame_source<Edge>);

// An 8-bit mono source of 64 frames, a byte each, with the three bytes before frame 0 that an unmasked read reaches.
struct EightBit {
	std::vector<uint8_t> bytes;
	EightBit() : bytes(3 + 64) {
		for (int32_t f = 0; f < 64; f++) {
			bytes[3 + f] = static_cast<uint8_t>(f * 3);
		}
	}
	[[nodiscard]] std::optional<FrameRun> run(int32_t frame, Direction d) const {
		if (frame < 0 || frame >= 64) {
			return std::nullopt;
		}
		const bool fwd = d == Direction::FORWARD;
		// The read origin is the frame's byte less three: the word ends at the frame's byte
		return FrameRun{reinterpret_cast<const std::byte*>(&bytes[frame]), fwd ? 64 - frame : frame + 1, fwd ? 1 : -1,
		                1, 1};
	}
	[[nodiscard]] int32_t channels() const { return 1; }
	[[nodiscard]] int32_t length_frames() const { return 64; }
};
} // namespace

// clang-format off
describe stretch_waveform_aligner_spec_body("deluge::dsp::fixed::timestretch::align", ${
	it("sets the search up as the legacy did (A1)", _ {
		int32_t bad = 0;
		for (const auto& row : r::kAlignSetupGoldens) {
			const ts::AlignSetup s = ts::align_setup(Increment::from_raw(row.increment), row.crossfade);
			bad += (s.each != row.each || s.crossfade_source_frames != row.crossfade_source) ? 1 : 0;
		}
		expect(bad).to_equal(0);
		expect(ts::align_setup(Increment::from_raw(1u << 24), 300).each).to_equal(35);
		expect(ts::align_setup(Increment::from_raw(1u << 24), 300).crossfade_source_frames).to_equal(300);
		expect(ts::align_setup(Increment::from_raw(1u << 26), 300).each).to_equal(70);
		expect(ts::align_setup(Increment::from_raw(1u << 16), 300).each).to_equal(1);
	});

	it("totals the moving averages as the legacy did (A2)", _ {
		int32_t bad = 0;
		int32_t refused = 0;
		for (const auto& row : r::kAveragesGoldens) {
			const RecordFrames frames(row.frames);
			const auto t = ts::moving_averages(frames, row.start, row.crossfade_source, dir(row.direction), row.each);
			if (!row.ok) {
				refused++;
				bad += t ? 1 : 0;
			}
			else {
				bad += (t && *t == row.totals) ? 0 : 1;
			}
		}
		expect(refused > 0 ? 1 : 0).to_equal(1);
		expect(bad).to_equal(0);
	});

	it("keeps A2's reversed quirk: no half-crossfade margin", _ {
		const Edge edge;
		// Forwards from frame 0: the read starts at 0 + 20 - 1 = 19, under half the crossfade (20) from frame 0: refused.
		expect(ts::moving_averages(edge, 0, 40, Direction::FORWARD, 1).has_value() ? 1 : 0).to_equal(0);
		// Mirrored, reversed from frame 63: the read starts at 63 - 20 + 1 = 44, within half the crossfade of the end,
		// which forwards would refuse; reversed checks no margin, so it totals.
		expect(ts::moving_averages(edge, 63, 40, Direction::REVERSE, 1).has_value() ? 1 : 0).to_equal(1);
	});

	it("allows A2's byte of slack before the data on a one-byte frame: reversed averages may end at frame -1", _ {
		const EightBit source;
		// Reversed from frame 1, no crossfade, averages of one frame: the read starts at 2 and ends at -1, reading 2, 1, 0
		const auto t = ts::moving_averages(source, 1, 0, Direction::REVERSE, 1);
		expect(t.has_value() ? 1 : 0).to_equal(1);
		// A wider frame has no such slack: the same averages end a frame's bytes before the data, and are refused
		expect(ts::moving_averages(Edge{}, 1, 0, Direction::REVERSE, 1).has_value() ? 1 : 0).to_equal(0);
	});

	it("limits the search as the legacy did (A3)", _ {
		int32_t bad = 0;
		for (const auto& row : r::kSearchLimitGoldens) {
			bad += ts::search_limit(row.hop_frames, Increment::from_raw(row.increment), row.rate) == row.limit ? 0 : 1;
		}
		expect(bad).to_equal(0);
		expect(ts::search_limit(1000, Increment::from_raw(1u << 24), 44100)).to_equal(156);
		expect(ts::search_limit(100000, Increment::from_raw(1u << 24), 44100)).to_equal(490);
	});

	it("aligns each recorded hop as the legacy did (A4 to A6)", _ {
		expect(r::kAlignGoldens.size() > 0 ? 1 : 0).to_equal(1);
		expect(align_mismatches(INT32_MAX)).to_equal(0);
	});

	it("refuses the recorded faults, whose missing frame lies in the head's own averages (A2)", _ {
		// The probe forced each fault at a search's first read, which is a frame of the new head's averages: a frame
		// missing there refuses the averages, before any search.
		int32_t bad = 0;
		for (const auto& row : r::kAlignFaults) {
			const RecordFrames frames(row.frames);
			bad += ts::align(frames, request_of(row)).has_value() ? 1 : 0;
		}
		expect(r::kAlignFaults.size() > 0 ? 1 : 0).to_equal(1);
		expect(bad).to_equal(0);
	});

	it("starts an abandoned search at phase 0 (decision 7)", _ {
		// The averages' frames are resident, a frame the search reaches is not: it abandons, planned start,
		// phase 0, though the older head's phase would otherwise be added.
		Edge edge;
		edge.missing_from = 30;
		edge.words.assign(64, 1 << 20); // flat: no sign flip and no better match, so it slides on to frame 30
		const ts::AlignRequest req{.start_frame = 10, .direction = Direction::FORWARD, .increment = Increment::from_raw(3u << 23),
		                           .setup = {2, 4}, .limit = 20, .old_totals = {0, 0, 0}, .older_phase = UQ<24>::from_raw(12345)};
		const std::optional<ts::Alignment> a = ts::align(edge, req);
		expect(a.has_value() ? 1 : 0).to_equal(1);
		expect(a->offset_frames).to_equal(0);
		expect(static_cast<int64_t>(a->phase.raw())).to_equal(int64_t{0});
	});

	it("aligns a run split at chunk boundaries as one run", _ {
		expect(align_mismatches(1)).to_equal(0);
		expect(align_mismatches(7)).to_equal(0);
		expect(align_mismatches(128)).to_equal(0);
	});

	it("asks its source for a run per chunk, not per frame read", _ {
		// The whole source is one chunk: the averages and both passes of the search read on within the runs they have,
		// asking again only to turn round
		const Edge edge;
		const ts::AlignRequest req{.start_frame = 30, .direction = Direction::FORWARD, .increment = Increment::from_raw(1u << 24),
		                           .setup = {2, 4}, .limit = 12, .old_totals = {0, 0, 0}, .older_phase = UQ<24>::from_raw(0)};
		(void)ts::align(edge, req);
		expect(edge.runs <= 8 ? 1 : 0).to_equal(1);
	});

	it("stops a reversed pass at frame 0", _ {
		const Edge edge;
		const ts::AlignRequest req{.start_frame = 10, .direction = Direction::REVERSE, .increment = Increment::from_raw(1u << 24),
		                           .setup = {1, 2}, .limit = 50, .old_totals = {0, 0, 0}, .older_phase = UQ<24>::from_raw(0)};
		(void)ts::align(edge, req);
		expect(edge.lowest >= 0 ? 1 : 0).to_equal(1);
	});

	it("starts the head at the waveform's first frame if the alignment moves it before (A6)", _ {
		expect(ts::aligned_start(5, {-9, UQ<24>::from_raw(0)}, Direction::FORWARD, 100)).to_equal(0);
		expect(ts::aligned_start(95, {9, UQ<24>::from_raw(0)}, Direction::REVERSE, 100)).to_equal(99);
		expect(ts::aligned_start(50, {3, UQ<24>::from_raw(0)}, Direction::FORWARD, 100)).to_equal(53);
	});
});
// clang-format on

DELUGE_SPEC(stretch_waveform_aligner_spec, stretch_waveform_aligner_spec_body)
