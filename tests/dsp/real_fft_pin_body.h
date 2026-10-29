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

// NE10's int32 real FFT as the oracle for fixed::fft: its factor and twiddle tables and its forward and inverse
// transforms at every size the firmware transforms, pinned by digest; and where its 16-point forward writes past its
// configuration.

#include "real_fft_records.h"
#include "real_fft_workloads.h"
extern "C" {
#include "NE10.h"
}
#include <cppspec.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <span>
#include <tuple>
#include <vector>

// NE10's allocator and cluster-loader hooks as this executable provides them. Each block is followed by a guard the
// spec owns, so a write past a configuration's end is seen rather than corrupting the heap.
namespace ne10_host {
inline constexpr std::size_t kGuardBytes = 4096;
inline constexpr unsigned char kGuardFill = 0xA5;
inline std::map<void*, std::size_t>& blocks() {
	static std::map<void*, std::size_t> b;
	return b;
}
[[nodiscard]] inline bool guard_intact(void* block) {
	const auto* end = static_cast<const unsigned char*>(block) + blocks().at(block);
	for (std::size_t i = 0; i < kGuardBytes; ++i) {
		if (end[i] != kGuardFill) {
			return false;
		}
	}
	return true;
}
} // namespace ne10_host

extern "C" void* delugeAlloc(unsigned int requiredSize, bool /*mayUseOnChipRam*/) {
	auto* p = static_cast<unsigned char*>(std::malloc(requiredSize + ne10_host::kGuardBytes));
	std::memset(p + requiredSize, ne10_host::kGuardFill, ne10_host::kGuardBytes);
	ne10_host::blocks()[p] = requiredSize;
	return p;
}
extern "C" void delugeDealloc(void* p) {
	ne10_host::blocks().erase(p);
	std::free(p);
}
extern "C" void routineWithClusterLoading() {
}
// NE10_fft.c's float32 allocator names this; nothing in the tree defines it, and the int32 transforms never reach it.
extern "C" ne10_fft_cfg_float32_t ne10_fft_alloc_c2c_float32_c(ne10_int32_t /*nfft*/) {
	std::abort();
}

namespace {

using namespace real_fft_workloads;
#if REAL_FFT_PIN_SHIPPING
// built as the firmware builds float code: its own records
constexpr const auto& kRecords = real_fft_records::kRecordsShipping;
constexpr const auto& kTwiddleRecords = real_fft_records::kTwiddleRecordsShipping;
#else
using real_fft_records::kRecords;
using real_fft_records::kTwiddleRecords;
#endif

constexpr std::size_t kPadWords = 64; // transforms run on copies padded so NE10's reads past an input stay inside

const char* direction_name(Direction d) {
	return d == Direction::Forward ? "Forward" : "Inverse";
}

const char* set_name(InputSet s) {
	switch (s) {
	case InputSet::Random:
		return "Random";
	case InputSet::Impulses:
		return "Impulses";
	case InputSet::FullScale:
		return "FullScale";
	case InputSet::RealCycles:
		return "RealCycles";
	}
	return "?";
}

/// Runs (d, m, set) on @p cfg and returns output_digest of its outputs: forward 2^(m-1)+1 bins as (r, i) words,
/// inverse 2^m samples.
uint64_t ne10_digest(ne10_fft_r2c_cfg_int32_t cfg, Direction d, int32_t m, InputSet set) {
	const auto n = std::size_t{1} << m;
	if (d == Direction::Forward) {
		std::vector<int32_t> in = forward_input(set, m);
		in.resize(in.size() + kPadWords, 0);
		std::vector<int32_t> out(2 * ((n / 2) + 1) + kPadWords, 0);
		ne10_fft_r2c_1d_int32_neon(reinterpret_cast<ne10_fft_cpx_int32_t*>(out.data()), in.data(), cfg, 0);
		return output_digest(std::span{out}.first(2 * ((n / 2) + 1)));
	}
	std::vector<int32_t> in = inverse_input(set, m);
	in.resize(in.size() + kPadWords, 0);
	std::vector<int32_t> out(n + kPadWords, 0);
	ne10_fft_c2r_1d_int32_neon(out.data(), reinterpret_cast<ne10_fft_cpx_int32_t*>(in.data()), cfg, 0);
	return output_digest(std::span{out}.first(n));
}

/// One configuration per m, allocated on first use and kept, for the record checks only.
ne10_fft_r2c_cfg_int32_t cached_cfg(int32_t m) {
	static std::map<int32_t, ne10_fft_r2c_cfg_int32_t> cfgs;
	auto& cfg = cfgs[m];
	if (cfg == nullptr) {
		cfg = ne10_fft_alloc_r2c_int32(1 << m);
	}
	return cfg;
}

bool printing() {
	return std::getenv("REAL_FFT_PIN_PRINT") != nullptr;
}

uint64_t table_digest(ne10_fft_r2c_cfg_int32_t cfg) {
	const std::span<const int32_t> factors{cfg->factors, static_cast<std::size_t>(2 * (cfg->factors[0] + 1) + 1)};
	const auto stage_words = static_cast<std::size_t>(2 * written_stage_twiddles(factors));
	const std::span<const int32_t> stage{reinterpret_cast<const int32_t*>(cfg->twiddles), stage_words};
	const std::span<const int32_t> super{reinterpret_cast<const int32_t*>(cfg->super_twiddles),
	                                     static_cast<std::size_t>(cfg->ncfft)}; // ncfft / 2 entries, two words each
	return twiddle_digest(factors, stage, super);
}

} // namespace

// clang-format off
describe ne10_real_fft("NE10's int32 real FFT, the oracle for fixed::fft", ${
	it("builds the factor and twiddle tables recorded for every size from 8 to 8192 points", _ {
		for (const auto& r : kTwiddleRecords) {
			const uint64_t digest = table_digest(cached_cfg(r.magnitude));
			if (printing()) {
				std::printf("    {%d, 0x%016llxULL},\n", r.magnitude, static_cast<unsigned long long>(digest));
			}
			expect(digest).to_equal(r.digest);
		}
	});

	it("transforms forward as recorded, from 16 to 8192 points, over every input set", _ {
		for (const auto& r : kRecords) {
			if (r.direction != Direction::Forward) {
				continue;
			}
			const uint64_t digest = ne10_digest(cached_cfg(r.magnitude), r.direction, r.magnitude, r.set);
			if (printing()) {
				std::printf("    {Direction::%s, %d, InputSet::%s, 0x%016llxULL},\n", direction_name(r.direction),
				            r.magnitude, set_name(r.set), static_cast<unsigned long long>(digest));
			}
			expect(digest).to_equal(r.digest);
		}
	});

	it("transforms inverse as recorded, from 8 to 8192 points, over every input set", _ {
		for (const auto& r : kRecords) {
			if (r.direction != Direction::Inverse) {
				continue;
			}
			const uint64_t digest = ne10_digest(cached_cfg(r.magnitude), r.direction, r.magnitude, r.set);
			if (printing()) {
				std::printf("    {Direction::%s, %d, InputSet::%s, 0x%016llxULL},\n", direction_name(r.direction),
				            r.magnitude, set_name(r.set), static_cast<unsigned long long>(digest));
			}
			expect(digest).to_equal(r.digest);
		}
	});

	it("writes past its configuration's end on a forward transform of 16 points", _ {
		ne10_fft_r2c_cfg_int32_t cfg = ne10_fft_alloc_r2c_int32(16);
		std::ignore = ne10_digest(cfg, Direction::Forward, 4, InputSet::Random);
		expect(ne10_host::guard_intact(cfg)).to_equal(false);
		delugeDealloc(cfg);
	});

	it("writes nothing past its configuration's end at any other size it transforms", _ {
		const auto check = [&](Direction d, int32_t m) {
			ne10_fft_r2c_cfg_int32_t cfg = ne10_fft_alloc_r2c_int32(1 << m);
			std::ignore = ne10_digest(cfg, d, m, InputSet::Random);
			const bool intact = ne10_host::guard_intact(cfg);
			if (!intact) {
				std::printf("RFP-OVERRUN %s m=%d\n", direction_name(d), m);
			}
			delugeDealloc(cfg);
			return intact;
		};
		for (int32_t m = kMinForwardMagnitude + 1; m <= kMaxMagnitude; ++m) {
			expect(check(Direction::Forward, m)).to_be_true();
		}
		for (int32_t m = kMinInverseMagnitude; m <= kMaxMagnitude; ++m) {
			expect(check(Direction::Inverse, m)).to_be_true();
		}
	});
});
// clang-format on
