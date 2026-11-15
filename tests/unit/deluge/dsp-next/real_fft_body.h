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

// fixed::fft::RealFft against the recorded transforms: forward and inverse at every size the firmware transforms, over
// every input set, bit for bit; and the smallest forward kept inside the spans it is lent.

#include "dsp-next/fixed/fft/real_fft.h"
#include "dsp-next/fixed/fft/twiddles.h"
#include "real_fft_records.h"
#include "real_fft_workloads.h"
#include <algorithm>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace {

using namespace deluge::dsp::fixed::fft;
using real_fft_workloads::Direction;

static_assert(!std::is_polymorphic_v<RealFft>);

#if REAL_FFT_SHIPPING
// built as the firmware builds float code, so its twiddle tables are the device's: the device's records
constexpr const auto& kRecords = real_fft_records::kRecordsShipping;
#else
using real_fft_records::kRecords;
#endif

constexpr Complex kGuard{0x5A5A5A5A, -0x5A5A5A5B};
constexpr std::size_t kGuardEntries = 16;

/// A 2^m transform's tables built in a block, with kGuardEntries guard entries after its scratch.
struct Built {
	std::vector<Complex> block;
	Layout layout;

	explicit Built(int32_t m) : block((storage_bytes(m) / sizeof(Complex)) + kGuardEntries, kGuard) {
		layout = lay_out(std::as_writable_bytes(std::span{block}).first(storage_bytes(m)), m);
		factor(layout.twiddles);
		generate_stage_twiddles(layout.twiddles);
		generate_super_twiddles(layout.twiddles);
	}

	[[nodiscard]] RealFft fft() const { return RealFft{layout.twiddles, layout.scratch}; }

	[[nodiscard]] bool guard_intact() const {
		return std::ranges::all_of(std::span{block}.last(kGuardEntries),
		                           [](Complex c) { return c.r == kGuard.r && c.i == kGuard.i; });
	}
};

uint64_t forward_digest(int32_t m, real_fft_workloads::InputSet set) {
	const Built built{m};
	const std::vector<int32_t> in = real_fft_workloads::forward_input(set, m);
	std::vector<Complex> out((std::size_t{1} << (m - 1)) + 1);
	built.fft().forward(in, out);
	return real_fft_workloads::output_digest({reinterpret_cast<const int32_t*>(out.data()), 2 * out.size()});
}

uint64_t inverse_digest(int32_t m, real_fft_workloads::InputSet set) {
	const Built built{m};
	const std::vector<int32_t> words = real_fft_workloads::inverse_input(set, m);
	std::vector<Complex> in(words.size() / 2);
	for (std::size_t k = 0; k < in.size(); ++k) {
		in[k] = {words[2 * k], words[(2 * k) + 1]};
	}
	std::vector<int32_t> out(std::size_t{1} << m);
	built.fft().inverse(in, out);
	return real_fft_workloads::output_digest(out);
}

} // namespace

// clang-format off
describe real_fft("fixed::fft::RealFft", ${
	it("transforms forward as recorded, from 16 to 8192 points, over every input set", _ {
		for (const auto& r : kRecords) {
			if (r.direction == Direction::Forward) {
				expect(forward_digest(r.magnitude, r.set)).to_equal(r.digest);
			}
		}
	});

	it("transforms inverse as recorded, from 8 to 8192 points, over every input set", _ {
		for (const auto& r : kRecords) {
			if (r.direction == Direction::Inverse) {
				expect(inverse_digest(r.magnitude, r.set)).to_equal(r.digest);
			}
		}
	});

	it("writes nothing past its output or its scratch on a forward transform of 16 points", _ {
		const Built built{4};
		const std::vector<int32_t> in =
		    real_fft_workloads::forward_input(real_fft_workloads::InputSet::Random, 4);
		std::vector<Complex> out(9 + kGuardEntries, kGuard);
		built.fft().forward(in, std::span{out}.first(9));
		expect(std::ranges::all_of(std::span{out}.last(kGuardEntries),
		                           [](Complex c) { return c.r == kGuard.r && c.i == kGuard.i; }))
		    .to_equal(true);
		expect(built.guard_intact()).to_equal(true);
	});
});
// clang-format on
