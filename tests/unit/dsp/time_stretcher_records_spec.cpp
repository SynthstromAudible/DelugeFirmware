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

// The time stretcher's contract records (time_stretcher_records.h), taken from the legacy code: that each array the
// components' specs read holds rows, that every span lies inside its pool, that the read head's rows cover the byte
// depths, layouts and directions the goldens play, and that the quirks the contracts name as pinned are among them.

#include "time_stretcher_records.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "support/spec_main.h"

namespace {
namespace r = time_stretcher_records;

constexpr bool inside(r::Span span, std::size_t pool) {
	return span.offset + span.count <= pool;
}

// Every span of every row, against its pool: the number of spans outside.
std::size_t spans_outside() {
	std::size_t bad = 0;
	auto words = [&](r::Span s) { bad += inside(s, r::kWords.size()) ? 0 : 1; };
	auto perc = [&](r::Span s) { bad += inside(s, r::kPercBytes.size()) ? 0 : 1; };
	auto source = [&](r::Span s) { bad += inside(s, r::kSourceBytes.size()) ? 0 : 1; };
	auto window = [&](const r::FrameWindow& w) {
		words(w.words);
		bad += w.words.count == static_cast<uint32_t>((w.last_frame - w.first_frame + 1) * w.channels) ? 0 : 1;
	};
	for (const auto& row : r::kCacheTailCopyGoldens) {
		source(row.cache_bytes);
		words(row.words);
	}
	for (const auto& row : r::kCacheTailMixGoldens) {
		words(row.tail_words);
		words(row.out_before);
		words(row.out_after);
	}
	for (const auto& row : r::kSlopeGoldens) {
		window(row.frames);
		words(row.slopes);
	}
	for (const auto& row : r::kPercAnalysisGoldens) {
		window(row.frames);
		perc(row.pixels_before);
		perc(row.pixels_after);
	}
	for (const auto& row : r::kZoneForFillGoldens) {
		words(row.zones);
	}
	for (const auto& row : r::kBeamGoldens) {
		perc(row.perc);
	}
	for (const auto& row : r::kHopPlanGoldens) {
		perc(row.perc);
	}
	for (const auto& row : r::kAveragesGoldens) {
		window(row.frames);
	}
	for (const auto& row : r::kAlignGoldens) {
		window(row.frames);
	}
	for (const auto& row : r::kAlignFaults) {
		window(row.frames);
	}
	for (const auto& reach : r::kQuirkReach) {
		bad += inside(reach.cases, r::kCaseIndex.size()) ? 0 : 1;
	}
	return bad;
}

// The second half's spans, against their pools: the number outside, and of taps windows not 32 long.
std::size_t playback_spans_outside() {
	std::size_t bad = 0;
	auto words = [&](r::Span s) { bad += inside(s, r::kPlaybackWords.size()) ? 0 : 1; };
	auto bytes = [&](r::Span s) { bad += inside(s, r::kReadBytes.size()) ? 0 : 1; };
	auto head = [&](const r::HeadRecord& h) {
		bad += inside(h.taps, r::kTaps.size()) ? 0 : 1;
		bad += h.taps.count == 32 ? 0 : 1;
	};
	auto source = [&](const r::ReadSourceRecord& src) {
		if (!inside(src.chunks, r::kChunkImages.size())) {
			bad++;
			return;
		}
		for (uint32_t i = 0; i < src.chunks.count; i++) {
			bytes(r::kChunkImages[src.chunks.offset + i].bytes);
		}
	};
	for (const auto& row : r::kCacheWriteGoldens) {
		words(row.mix);
		words(row.out_before);
		words(row.out_after);
		bytes(row.cache);
	}
	for (const auto& row : r::kPlayByteGoldens) {
		head(row.head);
	}
	for (const auto& row : r::kReadSeatGoldens) {
		source(row.source);
		head(row.before);
		head(row.after);
	}
	for (const auto& row : r::kWindowCheckGoldens) {
		source(row.source);
		head(row.before);
		head(row.after);
	}
	for (const auto& row : r::kReadGoldens) {
		source(row.source);
		head(row.before);
		head(row.after);
		words(row.out_before);
		words(row.out_after);
		bytes(row.cache_bytes);
	}
	for (const auto* rows : {&r::kReadLoopGoldens}) {
		for (const auto& row : *rows) {
			source(row.source);
			head(row.before);
			head(row.after);
			words(row.out_before);
			words(row.out_after);
		}
	}
	for (const auto& row : r::kReadHeadFaults) {
		source(row.source);
		head(row.before);
		head(row.after);
	}
	for (const auto& image : r::kChunkImages) {
		bytes(image.bytes);
	}
	return bad;
}

// Which byte depths (bit d set for depth d), channel counts (bit c) and directions (bit 0 forwards, bit 1 reversed)
// some row of @p rows reads.
struct Layouts {
	uint32_t depths = 0;
	uint32_t channels = 0;
	uint32_t directions = 0;
};
template <class Rows>
Layouts layouts(const Rows& rows) {
	Layouts l;
	for (const auto& row : rows) {
		l.depths |= 1u << row.source.byte_depth;
		l.channels |= 1u << row.source.channels;
		l.directions |= row.direction == 1 ? 1u : 2u;
	}
	return l;
}

std::size_t reach_of(std::string_view quirk) {
	for (const auto& reach : r::kQuirkReach) {
		if (reach.quirk == quirk) {
			return reach.cases.count;
		}
	}
	return 0;
}
} // namespace

// clang-format off
describe time_stretcher_records_spec_body("the time stretcher's contract records", ${
	it("holds rows for every contract the goldens, sweeps and faults reach", _ {
		const std::array<std::size_t, 31> sizes{
		    r::kHopShapeGoldens.size(), r::kHopShapeSweep.size(), r::kRampGoldens.size(), r::kRampSweep.size(),
		    r::kSegmentGoldens.size(), r::kCacheTailLimitGoldens.size(), r::kCacheTailLimitSweep.size(),
		    r::kCacheTailCopyGoldens.size(), r::kCacheTailMixGoldens.size(), r::kPercStoreGoldens.size(),
		    r::kPercStoreSweep.size(), r::kSlopeGoldens.size(), r::kPercAnalysisGoldens.size(),
		    r::kPercByteGoldens.size(), r::kZoneForFillGoldens.size(), r::kZoneOverwriteGoldens.size(),
		    r::kJitterGoldens.size(), r::kRepitchGoldens.size(), r::kBeamGoldens.size(), r::kBackEdgeGoldens.size(),
		    r::kHopLengthGoldens.size(), r::kHopLengthSweep.size(), r::kLoopCrossfadeGoldens.size(),
		    r::kStartClampGoldens.size(), r::kStartClampFaults.size(), r::kHopPlanGoldens.size(),
		    r::kAlignSetupGoldens.size(), r::kAveragesGoldens.size(), r::kSearchLimitGoldens.size(),
		    r::kAlignGoldens.size(), r::kAlignFaults.size()};
		std::size_t empty = 0;
		for (std::size_t n : sizes) {
			empty += n == 0 ? 1 : 0;
		}
		expect(empty).to_equal(std::size_t{0});
		expect(spans_outside()).to_equal(std::size_t{0});
	});

	it("holds rows for every contract this half pins", _ {
		const std::array<std::size_t, 29> sizes{
		    r::kFirstHopGoldens.size(), r::kFudgeGoldens.size(), r::kHopCheckGoldens.size(), r::kSeatGoldens.size(),
		    r::kSeatFaults.size(), r::kRestartGoldens.size(), r::kSyncedHopGoldens.size(), r::kAdvanceGoldens.size(),
		    r::kWholeFrameGoldens.size(), r::kWholeFrameSweep.size(), r::kStartPositionGoldens.size(),
		    r::kStartPositionSweep.size(), r::kLoopCheckGoldens.size(), r::kExitGoldens.size(),
		    r::kCacheWriteGoldens.size(), r::kOlderHeadGoldens.size(), r::kAmplitudeGoldens.size(),
		    r::kPercFillGoldens.size(), r::kReservationGoldens.size(), r::kCullFaults.size(),
		    r::kAllocationFaults.size(), r::kEvictionFaults.size(), r::kPlayByteGoldens.size(),
		    r::kReadSeatGoldens.size(), r::kWindowCheckGoldens.size(), r::kReadGoldens.size(),
		    r::kReadLoopGoldens.size(), r::kReadHeadFaults.size(), r::kChunkImages.size()};
		std::size_t empty = 0;
		for (std::size_t n : sizes) {
			empty += n == 0 ? 1 : 0;
		}
		expect(empty).to_equal(std::size_t{0});
		expect(playback_spans_outside()).to_equal(std::size_t{0});
	});

	it("holds read-head rows at every byte depth the goldens play", _ {
		// The loader keeps an 8-bit source at one byte a sample, so depth 1 is read too
		constexpr uint32_t kDepths = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4);
		constexpr uint32_t kChannels = (1u << 1) | (1u << 2);
		for (const Layouts l : {layouts(r::kReadGoldens), layouts(r::kWindowCheckGoldens)}) {
			expect(l.depths).to_equal(kDepths);
			expect(l.channels).to_equal(kChannels);
			expect(l.directions).to_equal(3u);
		}
	});

	it("holds a straddling frame", _ {
		std::size_t found = 0;
		for (const auto& row : r::kWindowCheckGoldens) {
			if (row.source.byte_depth != 3 || row.source.channels != 2) {
				continue;
			}
			for (uint32_t i = 0; i < row.source.chunks.count; i++) {
				const r::ChunkImage& image = r::kChunkImages[row.source.chunks.offset + i];
				found += image.first_offset + static_cast<int32_t>(image.bytes.count) > row.source.chunk_size ? 1 : 0;
			}
		}
		expect(found > 0 ? 1 : 0).to_equal(1);
	});

	it("holds each fault site's rows", _ {
		uint32_t outcomes = 0;
		for (const auto& row : r::kSeatFaults) {
			outcomes |= 1u << row.outcome;
		}
		expect(outcomes & 6u).to_equal(6u); // 1, the seat not ready; 2, its walk failed
		uint32_t sites = 0;
		for (const auto& row : r::kReadHeadFaults) {
			sites |= 1u << row.site;
		}
		expect(sites).to_equal(7u); // R3, R6 and R10; R11's own exit is unreachable
		uint32_t allocations = 0;
		for (const auto& row : r::kAllocationFaults) {
			allocations |= 1u << row.site;
		}
		expect(allocations).to_equal(15u); // the stretcher pool, the tail buffer, the perc store, a cache
		uint32_t evictions = 0;
		for (const auto& row : r::kEvictionFaults) {
			evictions |= 1u << row.site;
		}
		expect(evictions & 1u).to_equal(1u); // a cache chunk; a held perc chunk cannot be stolen
		expect(r::kCullFaults.empty() ? 0 : 1).to_equal(1);
	});

	it("says which goldens reach S2's quirk", _ {
		std::size_t edges = 0;
		for (const auto& reach : r::kQuirkReach) {
			if (reach.quirk != std::string_view{"S2"}) {
				continue;
			}
			for (uint32_t i = 0; i < reach.cases.count; i++) {
				edges += r::kCaseNames[r::kCaseIndex[reach.cases.offset + i]] == std::string_view{"RS_AUDIO_STRETCH_EDGES"}
				             ? 1
				             : 0;
			}
		}
		expect(reach_of("S2") > 0 ? 1 : 0).to_equal(1);
		expect(edges).to_equal(std::size_t{1});
	});

	it("pins T3's wrapped share at the ratio sample/ts_speed_slow plays", _ {
		std::size_t found = 0;
		for (const auto& row : r::kHopShapeGoldens) {
			if (row.ratio == (1u << 23)) {
				expect(row.crossfade_share).to_equal(static_cast<int32_t>(0xA0000000u));
				found++;
			}
		}
		expect(found).to_equal(std::size_t{1});
	});

	it("pins X1's quirk where the frames divide 2^24: a loop and a fudge one apart", _ {
		std::size_t pairs = 0;
		for (const auto& loop : r::kRampSweep) {
			if (loop.kind != r::RampKind::kLoop || ((1u << 24) % static_cast<uint32_t>(loop.frames)) != 0) {
				continue;
			}
			for (const auto& fudge : r::kRampSweep) {
				if (fudge.kind == r::RampKind::kFudge && fudge.frames == loop.frames) {
					expect(fudge.increment).to_equal(loop.increment + 1);
					pairs++;
				}
			}
		}
		expect(pairs > 0 ? 1 : 0).to_equal(1);
	});

	it("holds the C1 sweep's chunk-end rows, which copy nothing", _ {
		std::size_t found = 0;
		for (const auto& row : r::kCacheTailLimitSweep) {
			if (row.chunk_size - row.chunk_offset + 3 * row.channels - 1 <= 3 * row.channels) {
				expect(row.frames).to_equal(0);
				found++;
			}
		}
		expect(found > 0 ? 1 : 0).to_equal(1);
	});

	it("holds abandoned searches, each leaving its offset 0", _ {
		for (const auto& row : r::kAlignFaults) {
			expect(row.abandoned ? 1 : 0).to_equal(1);
			expect(row.offset).to_equal(0);
		}
	});

	it("says which goldens reach each quirk, T3's wrapped share among them", _ {
		expect(r::kQuirkReach.size()).to_equal(std::size_t{12});
		std::size_t wrapped = 0;
		for (const auto& reach : r::kQuirkReach) {
			if (reach.quirk == std::string_view{"T3 share wraps negative"}) {
				wrapped = reach.cases.count;
			}
		}
		expect(wrapped > 0 ? 1 : 0).to_equal(1);
	});
});
// clang-format on

DELUGE_SPEC(time_stretcher_records_spec, time_stretcher_records_spec_body)
