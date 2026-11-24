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
// The sample read's native read (dsp/fixed/sample_read/native_quads.h through ReadHead::read_native) against the scalar
// kernel it replaced: each sample's bytes masked from the 32-bit read ending at its top byte, for every byte depth,
// start byte, direction and layout, across runs that end mid-quad.

#include "dsp/fixed/sample_read/native_quads.h"

#include "dsp/fixed/noise/cong.h"
#include "dsp/fixed/sample_read/read_head.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <string>

#include "read_head_chunks.h"
#include "read_kernels_oracle.h"

#include "support/spec_main.h"

namespace {
using deluge::dsp::Q;
using deluge::dsp::UQ;
using deluge::dsp::fixed::Cong;
using deluge::dsp::fixed::sample_read::Direction;
using deluge::dsp::fixed::sample_read::ReadHead;
using deluge::dsp::fixed::sample_read::Residency;
using read_head_chunks::geometry;
using read_head_chunks::HandChunks;
using read_kernels_oracle::Oracle;

constexpr int32_t kFrames = 40'000;
constexpr int32_t kMaxRead = 256;

/// Native reads of @p frames frames, @p reads times, through ReadHead and the oracle: "" or the first difference.
std::string native_reads(int8_t depth, int8_t channels, int8_t out_channels, int32_t start, int32_t chunk, Direction d,
                         int32_t frames, int32_t reads) {
	auto g = geometry(start, channels, depth, kFrames, d);
	g.chunk_size = chunk;
	HandChunks a_source(g, kFrames, true);
	HandChunks b_source(g, kFrames, true);
	const int32_t at = d == Direction::FORWARD ? 100 : kFrames - 101;
	ReadHead::State a{};
	ReadHead::State b{};
	if (ReadHead::seat(a, a_source, at, UQ<24>{}, d) != Residency::READY
	    || ReadHead::seat(b, b_source, at, UQ<24>{}, d) != Residency::READY) {
		return "seat failed";
	}
	Cong noise{0xBEEF'0000U ^ static_cast<uint32_t>(frames)};
	Q<31> amp_a = Q<31>::from_raw(0x2800'0000);
	Q<31> amp_b = amp_a;
	const std::string where = "depth " + std::to_string(depth) + " " + std::to_string(channels) + "->"
	                          + std::to_string(out_channels) + " start " + std::to_string(start) + " chunk "
	                          + std::to_string(chunk) + (d == Direction::FORWARD ? " forward" : " reverse") + " frames "
	                          + std::to_string(frames);
	const ReadHead::ReadBounds bounds{.loop_start_frame = std::nullopt, .endless_silence = false};
	for (int32_t r = 0; r < reads; r++) {
		std::array<Q<31>, kMaxRead * 2> out_a{};
		for (auto& w : out_a) {
			w = Q<31>::from_raw(static_cast<int32_t>(noise.next()) >> 2);
		}
		std::array<Q<31>, kMaxRead * 2> out_b = out_a;
		// A window check bounds each read to the chunk held, as the head's callers do
		const ReadHead::Window wa = ReadHead::check(
		    a, a_source, bounds, frames, deluge::dsp::fixed::sample_read::Increment::from_raw(1u << 24), 2, d);
		const ReadHead::Window wb = ReadHead::check(
		    b, b_source, bounds, frames, deluge::dsp::fixed::sample_read::Increment::from_raw(1u << 24), 2, d);
		if (wa.kind != wb.kind || wa.frames != wb.frames) {
			return "checks differ: " + where;
		}
		if (wa.kind == ReadHead::Window::Kind::STOPPED) {
			break;
		}
		const auto words = static_cast<std::size_t>(wa.frames * out_channels);
		ReadHead::Output oa{.out = std::span<Q<31>>(out_a.data(), words),
		                    .out_channels = out_channels,
		                    .amplitude = amp_a,
		                    .step = Q<31>::from_raw(-4321)};
		ReadHead::Output ob{.out = std::span<Q<31>>(out_b.data(), words),
		                    .out_channels = out_channels,
		                    .amplitude = amp_b,
		                    .step = Q<31>::from_raw(-4321)};
		ReadHead::read_native(a, a_source, oa, wa.frames, d);
		Oracle::read_native(b, b_source, ob, wb.frames, d);
		if (out_a != out_b) {
			return "output differs at read " + std::to_string(r) + ": " + where;
		}
		if (oa.amplitude.raw() != ob.amplitude.raw()) {
			return "amplitude differs: " + where;
		}
		if (std::get<ReadHead::Reading>(a.cursor).frame != std::get<ReadHead::Reading>(b.cursor).frame) {
			return "cursor differs: " + where;
		}
		amp_a = oa.amplitude;
		amp_b = ob.amplitude;
	}
	return "";
}

} // namespace

// clang-format off
describe sample_read_native_spec_body("deluge::dsp::fixed::sample_read native reads", ${
	it("masks every byte depth's words as the native read does, forward and reversed, every start byte", _ {
		std::string bad;
		int32_t checked = 0;
		for (int8_t depth = 1; depth <= 4; depth++) {
			for (int8_t channels = 1; channels <= 2; channels++) {
				for (const int32_t start : {44, 45, 46, 47}) {
					for (const Direction d : {Direction::FORWARD, Direction::REVERSE}) {
						for (const int32_t chunk : {4096, 32768}) {
							for (const int32_t frames : {1, 3, 5, 9, 128, 256}) {
								bad += native_reads(depth, channels, channels, start, chunk, d, frames, 8);
								checked++;
							}
						}
					}
				}
			}
		}
		expect(bad).to_equal(std::string{});
		expect(checked).to_equal(4 * 2 * 4 * 2 * 2 * 6);
	});

	it("condenses a stereo source as (l >> 1) + (r >> 1)", _ {
		std::string bad;
		for (int8_t depth = 1; depth <= 4; depth++) {
			for (const Direction d : {Direction::FORWARD, Direction::REVERSE}) {
				for (const int32_t frames : {3, 7, 128}) {
					bad += native_reads(depth, 2, 1, 45, 4096, d, frames, 8);
				}
			}
		}
		expect(bad).to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(sample_read_native_spec, sample_read_native_spec_body)
