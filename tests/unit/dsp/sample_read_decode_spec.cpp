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
// The sample read's staging (dsp/fixed/sample_read/stage.h): a run's frames decoded to planar int16 taps newest first,
// each the top halfword of the 32-bit read ending at its sample's top byte, for every byte depth, layout, start byte
// and direction; and the window's history copied in and out.

#include "dsp/fixed/sample_read/stage.h"

#include "dsp/fixed/interpolation/interpolator.h"
#include "dsp/fixed/sample_read/read_head.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "read_head_chunks.h"

#include "support/spec_main.h"

namespace {
using deluge::dsp::fixed::Interpolator;
using deluge::dsp::fixed::sample_read::decode_taps;
using deluge::dsp::fixed::sample_read::Direction;
using deluge::dsp::fixed::sample_read::FrameRun;
using deluge::dsp::fixed::sample_read::kHistoryTaps;
using deluge::dsp::fixed::sample_read::kStageTaps;
using deluge::dsp::fixed::sample_read::load_history;
using deluge::dsp::fixed::sample_read::Staging;
using deluge::dsp::fixed::sample_read::store_history;
using read_head_chunks::geometry;
using read_head_chunks::HandChunks;

constexpr int32_t kFrames = 30'000;
constexpr int32_t kWholeRun = -1;

/// The reference tap: frame @p k's @p channel sample's top halfword, as the scalar reads take it.
int16_t top_word(const FrameRun& run, int32_t k, int32_t channel) {
	int16_t word = 0;
	std::memcpy(&word, run.first + (static_cast<std::ptrdiff_t>(k) * run.stride) + (channel * run.byte_depth) + 2,
	            sizeof word);
	return word;
}

/// Decodes @p frames of the run at @p at from @p from (all the rest for kWholeRun) and compares every slot: "" or the
/// first difference.
std::string check(HandChunks& source, int32_t at, Direction d, int32_t from, int32_t frames) {
	std::ignore = source.move_to(at, d);
	const std::optional<FrameRun> run = source.run(at, d);
	if (!run) {
		return "no run at " + std::to_string(at);
	}
	if (frames == kWholeRun) {
		frames = run->frames - from;
	}
	if (frames <= 0 || from + frames > run->frames || frames > kStageTaps) {
		return ""; // the run is too short for this case
	}
	std::vector<int16_t> l(static_cast<std::size_t>(frames) + 2, int16_t{0x5A5A});
	std::vector<int16_t> r(static_cast<std::size_t>(frames) + 2, int16_t{0x5A5A});
	decode_taps(*run, from, frames, l.data() + 1, run->channels == 2 ? r.data() + 1 : nullptr);
	const std::string where = "depth " + std::to_string(run->byte_depth) + " channels " + std::to_string(run->channels)
	                          + (d == Direction::FORWARD ? " forward" : " reverse") + " at " + std::to_string(at)
	                          + " from " + std::to_string(from) + " frames " + std::to_string(frames);
	if (l.front() != 0x5A5A || l.back() != 0x5A5A || r.front() != 0x5A5A || r.back() != 0x5A5A) {
		return "wrote outside its slots: " + where;
	}
	for (int32_t f = 0; f < frames; f++) {
		const auto slot = static_cast<std::size_t>(frames - f); // newest first, after the guard
		if (l[slot] != top_word(*run, from + f, 0)) {
			return "left tap of frame " + std::to_string(f) + ": " + where;
		}
		if (run->channels == 2 && r[slot] != top_word(*run, from + f, 1)) {
			return "right tap of frame " + std::to_string(f) + ": " + where;
		}
	}
	return "";
}

} // namespace

// clang-format off
describe sample_read_decode_spec_body("deluge::dsp::fixed::sample_read staging", ${
	it("decodes every byte depth and layout newest first, forward and reversed, from every start byte", _ {
		std::string bad;
		int32_t checked = 0;
		for (int8_t depth = 1; depth <= 4 && bad.empty(); depth++) {
			for (int8_t channels = 1; channels <= 2 && bad.empty(); channels++) {
				for (const int32_t start : {44, 45, 46, 47}) {
					for (const Direction d : {Direction::FORWARD, Direction::REVERSE}) {
						HandChunks source(geometry(start, channels, depth, kFrames, d), kFrames, true);
						const int32_t at = d == Direction::FORWARD ? 1000 : kFrames - 1000;
						for (const int32_t from : {0, 1, 7}) {
							for (const int32_t frames : {1, 7, 8, 9, 63, 1008}) {
								bad = check(source, at, d, from, frames);
								checked++;
								if (!bad.empty()) {
									break;
								}
							}
							if (!bad.empty()) {
								break;
							}
						}
						if (!bad.empty()) {
							break;
						}
					}
					if (!bad.empty()) {
						break;
					}
				}
			}
		}
		expect(bad).to_equal(std::string{});
		expect(checked).to_equal(4 * 2 * 4 * 2 * 3 * 6);
	});

	it("takes an 8-bit sample's low byte from the byte before it", _ {
		std::string bad;
		for (const int8_t channels : {int8_t{1}, int8_t{2}}) {
			for (const int32_t start : {44, 45}) {
				HandChunks source(geometry(start, channels, 1, kFrames, Direction::FORWARD), kFrames, true);
				// The run's first frame is the data's first, whose low byte is the byte before the data
				source.byte(start - 1) = std::byte{0xC3};
				bad += check(source, 0, Direction::FORWARD, 0, 9);
				bad += check(source, 0, Direction::FORWARD, 0, kWholeRun);
			}
		}
		expect(bad).to_equal(std::string{});
	});

	it("reads a 24-bit stereo frame straddling a chunk from the bytes the run gives, both ways", _ {
		std::string bad;
		// 32,768 is not a multiple of 6: runs end and begin in a chunk's slack
		HandChunks forward(geometry(0, 2, 3, kFrames, Direction::FORWARD), kFrames, true);
		HandChunks reversed(geometry(0, 2, 3, kFrames, Direction::REVERSE), kFrames, true);
		for (const int32_t chunk : {1, 2, 3}) {
			const int32_t edge = (chunk * 32768) / 6;
			for (const int32_t at : {edge - 40, edge - 1, edge, edge + 1}) {
				bad += check(forward, at, Direction::FORWARD, 0, kWholeRun);
				bad += check(reversed, at, Direction::REVERSE, 0, kWholeRun);
			}
		}
		expect(bad).to_equal(std::string{});
	});

	it("round-trips the history through staging", _ {
		Interpolator window{};
		auto feed = window.fill_tail(0);
		for (int16_t i = 0; i < kHistoryTaps; i++) {
			feed.put(static_cast<int16_t>(1000 + i), static_cast<int16_t>(-1000 - i));
		}
		Staging staging{};
		load_history(staging, window, 2);
		Interpolator back{};
		store_history(staging, kStageTaps - kHistoryTaps, back, 2, kHistoryTaps);
		expect(back.window_l() == window.window_l()).to_equal(true);
		expect(back.window_r() == window.window_r()).to_equal(true);
		Interpolator two{};
		store_history(staging, kStageTaps - kHistoryTaps, two, 2, 2);
		expect(two.window_l()[1]).to_equal(window.window_l()[1]);
		expect(two.window_l()[2]).to_equal(int16_t{0});
	});
});
// clang-format on

DELUGE_SPEC(sample_read_decode_spec, sample_read_decode_spec_body)
