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

#pragma once

// fixed::fft's factor and twiddle tables at every size the firmware transforms, against the recorded tables.

#include "dsp-next/fixed/fft/twiddles.h"
#include "real_fft_records.h"
#include "real_fft_workloads.h"
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using namespace deluge::dsp::fixed::fft;

#if TWIDDLES_SHIPPING
// built as the firmware builds float code: its own records
constexpr const auto& kTwiddleRecords = real_fft_records::kTwiddleRecordsShipping;
#else
using real_fft_records::kTwiddleRecords;
#endif

/// One 2^m transform's block, its tables built in the three stages a loader runs.
struct Built {
	std::vector<Complex> block; // Complex-typed so the block is 8-byte aligned
	Layout layout;

	explicit Built(int32_t m) : block((storage_bytes(m) + sizeof(Complex) - 1) / sizeof(Complex)) {
		layout = lay_out(std::as_writable_bytes(std::span{block}), m);
		factor(layout.twiddles);
		generate_stage_twiddles(layout.twiddles);
		generate_super_twiddles(layout.twiddles);
	}

	[[nodiscard]] std::vector<int32_t> written_factors() const {
		const std::span<const int32_t> f = layout.twiddles.factors;
		return {f.begin(), f.begin() + real_fft_workloads::written_factor_words(f)};
	}

	[[nodiscard]] uint64_t digest() const {
		const Twiddles& t = layout.twiddles;
		const std::span<const int32_t> stage{reinterpret_cast<const int32_t*>(t.stage.data()), 2 * t.stage.size()};
		const std::span<const int32_t> super{reinterpret_cast<const int32_t*>(t.super.data()), 2 * t.super.size()};
		return real_fft_workloads::twiddle_digest(t.factors, stage, super);
	}
};

} // namespace

// clang-format off
describe twiddles("fixed::fft::Twiddles", ${
	it("builds the factor and twiddle tables recorded for every size from 8 to 8192 points", _ {
		for (const auto& r : kTwiddleRecords) {
			expect(Built{r.magnitude}.digest()).to_equal(r.digest);
		}
	});

	it("factors 16 complex points as two radix-4 stages", _ {
		expect(Built{5}.written_factors()).to_equal(std::vector<int32_t>{2, 4, 4, 4, 4, 1, 0});
	});

	it("factors 8 complex points as one radix-8 stage", _ {
		expect(Built{4}.written_factors()).to_equal(std::vector<int32_t>{1, 1, 8, 1, 0});
	});

	it("lays the tables and scratch out end to end inside the block", _ {
		for (int32_t m = kMinMagnitude; m <= kMaxMagnitude; ++m) {
			const Built b{m};
			const auto* begin = reinterpret_cast<const std::byte*>(b.block.data());
			const auto* end = reinterpret_cast<const std::byte*>(b.layout.scratch.data() + b.layout.scratch.size());
			expect(static_cast<std::size_t>(end - begin)).to_equal(storage_bytes(m));
			expect(b.layout.twiddles.stage.size()).to_equal(std::size_t{1} << (m - 1));
			expect(b.layout.twiddles.super.size()).to_equal(std::size_t{1} << (m - 2));
		}
	});
});
// clang-format on
