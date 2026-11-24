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
// The sample read head's reads (dsp/fixed/sample_read/read_head.h) against the scalar kernels they replaced
// (read_kernels_oracle.h), word for word: every byte depth, layout, start byte, chunk size, direction, rate and width,
// through the same control plane over generated chunks.

#include "dsp/fixed/interpolation/windowed_sinc_kernel.h"
#include "dsp/fixed/noise/cong.h"
#include "dsp/fixed/sample_read/read_head.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "read_head_chunks.h"
#include "read_kernels_oracle.h"

#include "support/spec_main.h"

namespace {
using deluge::dsp::Q;
using deluge::dsp::UQ;
using deluge::dsp::fixed::Cong;
using deluge::dsp::fixed::WindowedSincKernel;
using deluge::dsp::fixed::sample_read::Direction;
using deluge::dsp::fixed::sample_read::Increment;
using deluge::dsp::fixed::sample_read::ReadHead;
using deluge::dsp::fixed::sample_read::Residency;
using read_head_chunks::geometry;
using read_head_chunks::HandChunks;
using read_kernels_oracle::Oracle;

constexpr int32_t kFrames = 60'000;
constexpr int32_t kMaxRead = 128;

/// One read sequence: a source's layout and geometry, and the reads made of it.
struct Case {
	int8_t depth;
	int8_t channels;
	int8_t out_channels;
	int32_t data_start;
	int32_t chunk_bytes;
	Direction direction;
	uint32_t increment;
	int32_t width;
	int32_t frames; ///< Per read.
	int32_t reads;
	bool cache;
	int32_t step; ///< The amplitude's step, a word.
};

std::string describe_case(const Case& c, int32_t read, const char* what) {
	return std::string(what) + " at read " + std::to_string(read) + ": depth " + std::to_string(c.depth) + ", channels "
	       + std::to_string(c.channels) + "->" + std::to_string(c.out_channels) + ", start "
	       + std::to_string(c.data_start) + ", chunk " + std::to_string(c.chunk_bytes) + ", "
	       + (c.direction == Direction::FORWARD ? "forward" : "reverse") + ", inc " + std::to_string(c.increment)
	       + ", width " + std::to_string(c.width) + ", frames " + std::to_string(c.frames);
}

bool same_state(const ReadHead::State& a, const ReadHead::State& b) {
	if (a.cursor.index() != b.cursor.index() || a.phase.raw() != b.phase.raw() || a.width_last != b.width_last) {
		return false;
	}
	if (const auto* ra = std::get_if<ReadHead::Reading>(&a.cursor)) {
		if (ra->frame != std::get<ReadHead::Reading>(b.cursor).frame) {
			return false;
		}
	}
	else if (std::get<ReadHead::Ended>(a.cursor).zero_taps != std::get<ReadHead::Ended>(b.cursor).zero_taps) {
		return false;
	}
	return a.window.window_l() == b.window.window_l() && a.window.window_r() == b.window.window_r();
}

/// Runs @p c's reads through ReadHead over @p a_source and through the oracle over @p b_source (two copies of one
/// source), each seated alike: "" when every output word, the
/// state after each read, the cache bytes and the amplitude agree, else the first difference.
std::string run_both(const Case& c, HandChunks& a_source, HandChunks& b_source) {
	ReadHead::State a{};
	const bool forward = c.direction == Direction::FORWARD;
	const int32_t at = forward ? 100 : kFrames - 101;
	if (ReadHead::seat(a, a_source, at, UQ<24>{}, c.direction) != Residency::READY) {
		return describe_case(c, -1, "seat failed");
	}
	ReadHead::State b{};
	std::ignore = ReadHead::seat(b, b_source, at, UQ<24>{}, c.direction);
	const ReadHead::ReadBounds bounds{.loop_start_frame = std::nullopt, .endless_silence = false};
	const int32_t kernel = WindowedSincKernel::variant_for(static_cast<int32_t>(c.increment));
	Cong noise{0x5eed'0000U ^ static_cast<uint32_t>(c.frames)};
	Q<31> amp_a = Q<31>::from_raw(0x3000'0000);
	Q<31> amp_b = amp_a;
	for (int32_t r = 0; r < c.reads; r++) {
		std::array<Q<31>, kMaxRead * 2> out_a{};
		for (auto& w : out_a) {
			w = Q<31>::from_raw(static_cast<int32_t>(noise.next()) >> 2);
		}
		std::array<Q<31>, kMaxRead * 2> out_b = out_a;
		std::array<std::byte, kMaxRead * 2 * 3> cache_a{};
		std::array<std::byte, kMaxRead * 2 * 3> cache_b{};
		const auto words = static_cast<std::size_t>(c.frames * c.out_channels);
		ReadHead::Output oa{.out = std::span<Q<31>>(out_a.data(), words),
		                    .out_channels = c.out_channels,
		                    .amplitude = amp_a,
		                    .step = Q<31>::from_raw(c.step)};
		ReadHead::Output ob{.out = std::span<Q<31>>(out_b.data(), words),
		                    .out_channels = c.out_channels,
		                    .amplitude = amp_b,
		                    .step = Q<31>::from_raw(c.step)};
		const bool go_a = ReadHead::read(a, a_source, bounds, oa, c.frames, Increment::from_raw(c.increment), c.width,
		                                 kernel, c.cache ? cache_a.data() : nullptr, c.direction);
		const bool go_b = Oracle::read(b, b_source, bounds, ob, c.frames, Increment::from_raw(c.increment), c.width,
		                               kernel, c.cache ? cache_b.data() : nullptr, c.direction);
		if (go_a != go_b) {
			return describe_case(c, r, "stopped differently");
		}
		if (out_a != out_b) {
			return describe_case(c, r, "output differs");
		}
		if (cache_a != cache_b) {
			return describe_case(c, r, "cache differs");
		}
		if (oa.amplitude.raw() != ob.amplitude.raw()) {
			return describe_case(c, r, "amplitude differs");
		}
		if (!same_state(a, b)) {
			return describe_case(c, r, "state differs");
		}
		if (!go_a) {
			break;
		}
		amp_a = oa.amplitude;
		amp_b = ob.amplitude;
	}
	return "";
}

constexpr std::array<uint32_t, 8> kIncrements{1u << 24,     0x0010'0000U, 0x00F0'0000U, 3u << 23,
                                              0x01F0'0000U, 4u << 24,     17u << 24,    40u << 24};

/// Every case of the matrix whose rates and frames pass @p keep, compared; "" or the first difference, and the count.
template <typename Keep>
std::string matrix(std::size_t& count, const Keep& keep) {
	for (int8_t depth = 1; depth <= 4; depth++) {
		for (int8_t channels = 1; channels <= 2; channels++) {
			for (const int32_t start : {44, 45, 46, 47}) {
				for (const int32_t chunk : {4096, 32768}) {
					for (const Direction d : {Direction::FORWARD, Direction::REVERSE}) {
						auto g = geometry(start, channels, depth, kFrames, d);
						g.chunk_size = chunk;
						HandChunks a_source(g, kFrames, true);
						HandChunks b_source(g, kFrames, true);
						for (int8_t out = 1; out <= channels; out++) {
							for (const uint32_t inc : kIncrements) {
								for (const int32_t width : {2, 8, 16}) {
									for (const int32_t frames : {1, 3, 5, 128}) {
										const Case c{.depth = depth,
										             .channels = channels,
										             .out_channels = out,
										             .data_start = start,
										             .chunk_bytes = chunk,
										             .direction = d,
										             .increment = inc,
										             .width = width,
										             .frames = frames,
										             .reads = 6,
										             .cache = width == 16,
										             .step = 12'345};
										if (!keep(c)) {
											continue;
										}
										count++;
										std::string bad = run_both(c, a_source, b_source);
										if (!bad.empty()) {
											return bad;
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
	return "";
}

} // namespace

// clang-format off
describe read_head_oracle_spec_body("deluge::dsp::fixed::sample_read::ReadHead reads against the scalar oracle", ${
	it("matches the oracle across byte depths, layouts, directions, rates and widths", _ {
		std::size_t count = 0;
		const std::string bad = matrix(count, [](const Case&) { return true; });
		expect(bad).to_equal(std::string{});
		expect(count).to_equal(std::size_t{4 * 3 * 4 * 2 * 2 * 8 * 3 * 4});
	});
});
// clang-format on

DELUGE_SPEC(read_head_oracle_spec, read_head_oracle_spec_body)
