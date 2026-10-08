/*
 * Copyright © 2015-2026 Synthstrom Audible Limited
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

#include "dsp-next/fixed/delay/delay_tape.h"

#include <algorithm>
#include <cmath>

namespace deluge::dsp::fixed {

namespace {
/// The neutral rate's tape length times the neutral rate: a tape's size times its native rate, ideally.
constexpr double kNeutralSpan = static_cast<double>(DelayTape::kNeutralSize) * static_cast<double>(kMaxSampleValue);
/// The slowest a tape resamples, 8.24 of its native rate (2^-15). Below it the slowed-down write's widening no longer
/// fits 32 bits: at 2^-16 the rate multiple times the read's slowness is 2^32, and below that the inverse rate is a
/// division by zero. A rate of 0 crawls at this spin.
constexpr uint32_t kMinSpinRate = 512;
} // namespace

DelayTape::DelayTape(std::span<StereoSample> storage, size_t size, uint32_t native_rate)
    : native_rate_{native_rate}, start_{storage.data()}, end_{storage.data() + storage.size()}, size_{size},
      size_including_extra_{storage.size()} {
	clear();
}

std::pair<int32_t, bool> DelayTape::idealSizeForRate(uint32_t rate) {
	if (rate == 0) {
		return {static_cast<int32_t>(kMaxSize), true};
	}
	const uint64_t ideal = static_cast<uint64_t>(kNeutralSize) * kMaxSampleValue / rate;
	const uint64_t size = std::clamp<uint64_t>(ideal, kMinSize, kMaxSize);
	return {static_cast<int32_t>(size), size != ideal};
}

void DelayTape::makeNativeRatePrecise() {
	native_rate_ = static_cast<uint32_t>(std::round(kNeutralSpan / static_cast<double>(size_)));
}

void DelayTape::makeNativeRatePreciseRelativeTo(const DelayTape& other) {
	const double other_amount_too_fast =
	    static_cast<double>(other.native_rate_) * static_cast<double>(other.size_) / kNeutralSpan;
	native_rate_ = static_cast<uint32_t>(std::round(kNeutralSpan * other_amount_too_fast / static_cast<double>(size_)));
}

void DelayTape::clear() {
	std::fill_n(start_, kWriteLag + 2, StereoSample{.l = 0, .r = 0});
	current_ = start_ + kWriteLag;
	resample_ = std::nullopt;
}

void DelayTape::setupResample() {
	long_pos_ = 0;
	last_short_pos_ = 0;

	// The triangles are four samples wide, two either side of the write head: a quarter of the sample last written
	// belongs one sample on.
	StereoSample* written = writePosition(0);
	StereoSample* next = writePosition(1);
	*next = {.l = written->l >> 2, .r = written->r >> 2};
	written->l -= next->l;
	written->r -= next->r;
}

void DelayTape::setupForRender(int32_t rate) {
	if (!resampling()) {
		if (static_cast<uint32_t>(rate) == native_rate_ || start_ == nullptr) {
			return;
		}
		setupResample();
	}

	const auto actual_spin_rate =
	    std::max(static_cast<uint32_t>(static_cast<uint64_t>(static_cast<double>(static_cast<uint64_t>(rate) << 24)
	                                                         / static_cast<double>(native_rate_))),
	             kMinSpinRate);
	auto divide_by_rate =
	    static_cast<uint32_t>(static_cast<double>(0xFFFFFFFF) / static_cast<double>(actual_spin_rate >> 8));

	Resample resample{.actual_spin_rate = actual_spin_rate,
	                  .spin_rate_for_sped_up_writing = 0,
	                  .divide_by_rate = 0,
	                  .rate_multiple = 0,
	                  .write_size_adjustment = 0};

	if (actual_spin_rate < static_cast<uint32_t>(kMaxSampleValue)) {
		// The smallest multiple of the rate that is at least one step, so the triangles meet and each is at least a
		// sample wide; the write size adjustment falls as their area grows with it.
		const uint32_t times_slower_read = divide_by_rate >> 16;
		resample.rate_multiple = (actual_spin_rate >> 8) * (times_slower_read + 1);
		resample.write_size_adjustment = static_cast<uint32_t>(
		    static_cast<double>(0xFFFFFFFF) / static_cast<double>(resample.rate_multiple * (times_slower_read + 1)));
	}
	else {
		// Sped-up writes stay exact up to 8 times native; beyond that they would cost too much. The triangle is
		// twice as wide as the rate needs, so at the native rate it matches the slowed-down write's width.
		resample.spin_rate_for_sped_up_writing = std::min(actual_spin_rate, static_cast<uint32_t>(kMaxSampleValue) * 8)
		                                         << 1;
		divide_by_rate >>= 1;
	}
	resample.divide_by_rate = divide_by_rate;
	resample_ = resample;
}

} // namespace deluge::dsp::fixed
