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
// fp::Drift: its uniform draw (mean 0, variance 1/3), its one-pole smoothing (lag-1 autocorrelation 1 − a), its
// reproducibility from a seed, the independence of differently seeded voices, and its stream pinned by digest.

#include "dsp-next/core/flush_to_zero.h"
#include "dsp-next/float/mod/drift.h"

#include "float_kernel_records.h"

#include <bit>
#include <cmath>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace deluge::dsp;

namespace {

constexpr std::size_t kLong = 100000;

/// @p n values of a Drift configured with @p settings.
[[nodiscard]] std::vector<f32> run(const fp::Drift::Settings& settings, std::size_t n) {
	fp::Drift drift;
	drift.configure(settings);
	std::vector<f32> v(n);
	for (f32& x : v) {
		x = drift.next();
	}
	return v;
}

[[nodiscard]] double mean(const std::vector<f32>& v) {
	double s = 0.0;
	for (const f32 x : v) {
		s += static_cast<double>(x);
	}
	return s / static_cast<double>(v.size());
}

/// The correlation of @p a with @p b shifted by @p lag.
[[nodiscard]] double correlation(const std::vector<f32>& a, const std::vector<f32>& b, std::size_t lag = 0) {
	const double ma = mean(a);
	const double mb = mean(b);
	double ab = 0.0;
	double aa = 0.0;
	double bb = 0.0;
	for (std::size_t i = 0; i + lag < a.size(); ++i) {
		const double x = static_cast<double>(a[i + lag]) - ma;
		const double y = static_cast<double>(b[i]) - mb;
		ab += x * y;
		aa += x * x;
		bb += y * y;
	}
	return ab / std::sqrt(aa * bb);
}

/// FNV-1a over the values' bits: the digest a record pins.
[[nodiscard]] uint32_t digest(const std::vector<f32>& v) {
	uint32_t h = 2166136261U;
	for (const f32 x : v) {
		h = (h ^ std::bit_cast<uint32_t>(x)) * 16777619U;
	}
	return h;
}

} // namespace

// clang-format off
describe float_drift_spec("fp::Drift", ${
	it("draws uniformly over [−1, 1] at time 1: mean 0, variance 1/3", _ {
		fp::FlushToZero ftz;
		fp::Drift drift;
		drift.configure({.time_blocks = 1.F, .seed = 7});
		const f32 a = drift.coefficient();
		// At time 1 the value moves 1 − 1/e of the way to each draw; recover the draws from successive values.
		std::vector<f32> u(kLong);
		f32 last = 0.F;
		for (f32& x : u) {
			const f32 now = drift.next();
			x = last + ((now - last) / a);
			last = now;
		}
		const double m = mean(u);
		double var = 0.0;
		for (const f32 x : u) {
			var += (static_cast<double>(x) - m) * (static_cast<double>(x) - m);
		}
		var /= static_cast<double>(u.size());
		expect(std::fabs(m) < 0.01).to_be_true();
		expect(std::fabs(var - (1.0 / 3.0)) < 0.05 / 3.0).to_be_true();
	});
	it("stays within [−1, 1]", _ {
		fp::FlushToZero ftz;
		for (const f32 x : run({.time_blocks = 1.F, .seed = 3}, kLong)) {
			expect(std::fabs(x) <= 1.F).to_be_true();
		}
	});
	it("smooths with its time constant: the lag-1 autocorrelation at time 64 is 1 − a", _ {
		fp::FlushToZero ftz;
		fp::Drift drift;
		drift.configure({.time_blocks = 64.F, .seed = 11});
		const auto v = run({.time_blocks = 64.F, .seed = 11}, kLong);
		expect(std::fabs(correlation(v, v, 1) - (1.0 - static_cast<double>(drift.coefficient()))) < 0.02)
		    .to_be_true();
		expect(std::fabs(static_cast<double>(drift.coefficient()) - (1.0 - std::exp(-1.0 / 64.0))) < 1e-7)
		    .to_be_true();
	});
	it("reproduces its stream from a seed, after reseed as after configure", _ {
		fp::FlushToZero ftz;
		const auto first = run({.time_blocks = 16.F, .seed = 42}, 1000);
		fp::Drift drift;
		drift.configure({.time_blocks = 16.F, .seed = 9});
		for (int i = 0; i < 100; ++i) {
			(void)drift.next();
		}
		drift.reseed(42);
		expect(drift.value()).to_equal(0.F);
		for (const f32 x : first) {
			expect(std::bit_cast<uint32_t>(drift.next())).to_equal(std::bit_cast<uint32_t>(x));
		}
	});
	it("takes seed 0 as 1", _ {
		fp::FlushToZero ftz;
		expect(digest(run({.seed = 0}, 1000))).to_equal(digest(run({.seed = 1}, 1000)));
	});
	it("gives differently seeded voices weakly correlated streams", _ {
		fp::FlushToZero ftz;
		const auto one = run({.time_blocks = 1.F, .seed = 1}, kLong);
		const auto two = run({.time_blocks = 1.F, .seed = 2}, kLong);
		expect(std::fabs(correlation(one, two)) < 0.05).to_be_true();
	});
	it("takes NaN and times below 1 as 1", _ {
		fp::FlushToZero ftz;
		const auto one = digest(run({.time_blocks = 1.F, .seed = 5}, 1000));
		expect(digest(run({.time_blocks = std::nanf(""), .seed = 5}, 1000))).to_equal(one);
		expect(digest(run({.time_blocks = 0.25F, .seed = 5}, 1000))).to_equal(one);
	});
	it("matches its record", _ {
		fp::FlushToZero ftz;
		expect(digest(run({.time_blocks = 32.F, .seed = 12345}, 4096))).to_equal(records::kDrift);
	});
});
// clang-format on

CPPSPEC_MAIN(float_drift_spec);
