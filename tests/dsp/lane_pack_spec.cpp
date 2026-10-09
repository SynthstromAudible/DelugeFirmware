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

// LanePack<q31, N>: every operation is its scalar counterpart applied lane by lane. Each is pinned against that
// counterpart at N = 1, 2, 3, 4 and 8, over every pairing (and, for the accumulate, every triple) of q31's extremes,
// with each lane given a different pairing, and over full-scale seeded noise. The extremes include the pairs whose sum
// or difference leaves q31, and the products at INT32_MIN. Then one generic expression each of OnePole's step,
// ShiftTracker's step and AntialiasedTanh's clamp, written once over V, compiles for q31 and for the pack and gives
// each lane the scalar result.

#include "dsp-next/core/lane_pack.h"

#include "dsp-next/core/frame.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/filter/one_pole.h"
#include "dsp-next/fixed/filter/shift_tracker.h"
#include "dsp-next/fixed/noise/noise.h"
#include "dsp-next/fixed/shaping/antialiased_tanh.h"
#include "util/fixedpoint.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

using namespace deluge::dsp;

/// Each kernel step, written once over its value type V, as a kernel that opts in to op-major stepping writes it. They
/// sit in the kernels' namespace and call the operations unqualified, as the kernels do.
namespace deluge::dsp::fixed::lane_pack_probe {

/// OnePole's ALL_PASS step: every operation its three modes use.
template <class V>
[[nodiscard]] V one_pole_step(q31 m, V& z, V x) {
	const V v = multiply_32x32_rshift32_rounded(wrapping_sub(x, z), m) << OnePole::kProductToQ31Shift;
	const V y = wrapping_add(v, z);
	z = wrapping_add(y, v);
	return wrapping_sub(wrapping_add(y, y), x);
}

/// ShiftTracker<11>'s REMOVE step.
template <class V>
[[nodiscard]] V shift_tracker_step(V& y, V x) {
	y = wrapping_add(y, wrapping_sub(x, y) >> 11);
	return wrapping_sub(x, y);
}

/// AntialiasedTanh's clamp: below the floor to the floor, then above the ceiling to the ceiling.
template <class V>
[[nodiscard]] V clamp(V x, q31 floor, q31 ceiling) {
	return min(max(x, floor), ceiling);
}

/// The same clamp through the comparisons and select.
template <class V>
[[nodiscard]] V clamp_by_select(V x, q31 floor, q31 ceiling) {
	const V low_clamped = select(x < floor, V{floor}, x);
	return select(low_clamped > ceiling, V{ceiling}, low_clamped);
}

} // namespace deluge::dsp::fixed::lane_pack_probe

namespace {

using deluge::dsp::fixed::AntialiasedTanh;
using deluge::dsp::fixed::OnePole;
using deluge::dsp::fixed::SeededNoise;
namespace probe = deluge::dsp::fixed::lane_pack_probe;

static_assert(std::is_trivially_copyable_v<LanePack<q31, 1>>);
static_assert(std::is_trivially_copyable_v<LanePack<q31, 8>>);
static_assert(sizeof(LanePack<q31, 4>) == 4 * sizeof(q31));
static_assert(sizeof(LanePack<q31, 2>) == sizeof(Frame<q31, 2>));

constexpr q31 kMin = std::numeric_limits<q31>::min();
constexpr q31 kMax = std::numeric_limits<q31>::max();

/// q31's extremes and the values next to them: every pairing overflows a sum or a difference somewhere.
constexpr std::array<q31, 9> kExtremes{kMin, kMin + 1, kMin / 2, -1, 0, 1, kMax / 2, kMax - 1, kMax};
/// The full-scale noise trials after the extremes.
constexpr std::size_t kNoiseTrials = 2'000;
/// The seed of the noise trials.
constexpr uint32_t kSeed = 0x1A4E'5EED;

/// Three packs an operation takes: a and b for a binary operation, c as the accumulate's sum or a select's sign.
template <std::size_t N>
struct Trial {
	LanePack<q31, N> a;
	LanePack<q31, N> b;
	LanePack<q31, N> c;
};

/// Every triple of extremes, lane i offset by i in each operand so the lanes of one trial differ; then full-scale
/// seeded noise, every lane drawn separately.
template <std::size_t N>
std::vector<Trial<N>> trials() {
	std::vector<Trial<N>> out;
	constexpr std::size_t e = kExtremes.size();
	for (std::size_t j = 0; j < e; ++j) {
		for (std::size_t k = 0; k < e; ++k) {
			for (std::size_t l = 0; l < e; ++l) {
				Trial<N> t{};
				for (std::size_t i = 0; i < N; ++i) {
					t.a.lane[i] = kExtremes[(j + i) % e];
					t.b.lane[i] = kExtremes[(k + (2 * i)) % e];
					t.c.lane[i] = kExtremes[(l + (3 * i)) % e];
				}
				out.push_back(t);
			}
		}
	}
	SeededNoise noise{kSeed + static_cast<uint32_t>(N)};
	for (std::size_t n = 0; n < kNoiseTrials; ++n) {
		Trial<N> t{};
		for (std::size_t i = 0; i < N; ++i) {
			t.a.lane[i] = noise.render();
			t.b.lane[i] = noise.render();
			t.c.lane[i] = noise.render();
		}
		out.push_back(t);
	}
	return out;
}

/// What a comparison over every trial found: the lanes compared, and those whose pack result differs from the scalar.
struct Tally {
	std::size_t compared = 0;
	std::size_t mismatched = 0;
	Tally& operator+=(Tally t) {
		compared += t.compared;
		mismatched += t.mismatched;
		return *this;
	}
};

/// For every trial, the pack @p op returns against the scalar @p want for each lane i, given the trial and i. A
/// trial @p use rejects is skipped.
template <std::size_t N, class Op, class Want, class Use>
Tally tally(Op op, Want want, Use use) {
	Tally t;
	for (const Trial<N>& trial : trials<N>()) {
		if (!use(trial)) {
			continue;
		}
		const auto got = op(trial);
		for (std::size_t i = 0; i < N; ++i) {
			++t.compared;
			t.mismatched += got.lane[i] == want(trial, i) ? 0 : 1;
		}
	}
	return t;
}

template <std::size_t N, class Op, class Want>
Tally tally(Op op, Want want) {
	return tally<N>(op, want, [](const Trial<N>&) { return true; });
}

/// @p f's tallies summed over N = 1, 2, 3, 4 and 8.
template <class F>
Tally over_widths(F f) {
	Tally t;
	t += f.template operator()<1>();
	t += f.template operator()<2>();
	t += f.template operator()<3>();
	t += f.template operator()<4>();
	t += f.template operator()<8>();
	return t;
}

/// Whether @p a + @p b, or @p a - @p b when @p subtract, stays inside q31.
bool in_range(q31 a, q31 b, bool subtract) {
	const int64_t r = subtract ? int64_t{a} - int64_t{b} : int64_t{a} + int64_t{b};
	return r >= int64_t{kMin} && r <= int64_t{kMax};
}

/// Whether every lane of @p a op @p b stays inside q31, for the unwrapped + and -, whose overflow is undefined.
template <std::size_t N>
bool lanes_in_range(const LanePack<q31, N>& a, const LanePack<q31, N>& b, bool subtract) {
	for (std::size_t i = 0; i < N; ++i) {
		if (!in_range(a.lane[i], b.lane[i], subtract)) {
			return false;
		}
	}
	return true;
}

/// The lanes the extremes and noise give every operation: 9³ extreme trials and the noise trials, at 18 lanes each.
constexpr std::size_t kLanesPerWidthSum = (kExtremes.size() * kExtremes.size() * kExtremes.size() + kNoiseTrials) * 18;

} // namespace

// clang-format off
describe lane_pack_spec("LanePack", ${
	it("broadcasts a scalar to every lane, and loads and stores a frame lane for lane", _ {
		const LanePack<q31, 3> b = LanePack<q31, 3>::broadcast(kMin);
		for (const q31 x : b.lane) {
			expect(x).to_equal(kMin);
		}
		const LanePack<q31, 3> implicit = q31{-9};
		for (const q31 x : implicit.lane) {
			expect(x).to_equal(q31{-9});
		}

		const Frame<q31, 3> f{{kMin, 0, kMax}};
		const LanePack<q31, 3> p = LanePack<q31, 3>::load(f);
		expect(p.lane[0]).to_equal(kMin);
		expect(p.lane[1]).to_equal(q31{0});
		expect(p.lane[2]).to_equal(kMax);

		Frame<q31, 3> out{{1, 2, 3}};
		(p >> 1).store(out);
		expect(out[0]).to_equal(kMin >> 1);
		expect(out[1]).to_equal(q31{0});
		expect(out[2]).to_equal(kMax >> 1);

		const LanePack<q31, 2> zero{};
		expect(zero.lane[0]).to_equal(q31{0});
		expect(zero.lane[1]).to_equal(q31{0});
	});

	it("adds and subtracts lane by lane, wherever the scalar result is defined", _ {
		const Tally t = over_widths([]<std::size_t N>() {
			Tally s = tally<N>([](const Trial<N>& x) { return x.a + x.b; },
			                   [](const Trial<N>& x, std::size_t i) { return x.a.lane[i] + x.b.lane[i]; },
			                   [](const Trial<N>& x) { return lanes_in_range(x.a, x.b, false); });
			s += tally<N>([](const Trial<N>& x) { return x.a - x.b; },
			              [](const Trial<N>& x, std::size_t i) { return x.a.lane[i] - x.b.lane[i]; },
			              [](const Trial<N>& x) { return lanes_in_range(x.a, x.b, true); });
			return s;
		});
		expect(t.mismatched).to_equal(std::size_t{0});
		expect(t.compared).to_be_greater_than(std::size_t{10'000});
	});

	it("wraps its sums and differences lane by lane, as the scalar wrapping_add and wrapping_sub do", _ {
		const Tally t = over_widths([]<std::size_t N>() {
			Tally s = tally<N>([](const Trial<N>& x) { return wrapping_add(x.a, x.b); },
			                   [](const Trial<N>& x, std::size_t i) {
				                   return deluge::dsp::wrapping_add(x.a.lane[i], x.b.lane[i]);
			                   });
			s += tally<N>([](const Trial<N>& x) { return wrapping_sub(x.a, x.b); },
			              [](const Trial<N>& x, std::size_t i) {
				              return deluge::dsp::wrapping_sub(x.a.lane[i], x.b.lane[i]);
			              });
			return s;
		});
		expect(t.mismatched).to_equal(std::size_t{0});
		expect(t.compared).to_equal(2 * kLanesPerWidthSum);

		// The pairs that leave q31 are among them, and wrap.
		const auto top = LanePack<q31, 2>::load(Frame<q31, 2>{{kMax, kMin}});
		const auto one = LanePack<q31, 2>::load(Frame<q31, 2>{{1, 1}});
		const LanePack<q31, 2> sum = wrapping_add(top, one);
		const LanePack<q31, 2> difference = wrapping_sub(top, one);
		expect(sum.lane[0]).to_equal(kMin);
		expect(difference.lane[1]).to_equal(kMax);
	});

	it("shifts each lane by every amount, arithmetic right and left", _ {
		const Tally t = over_widths([]<std::size_t N>() {
			Tally s;
			for (int shift = 0; shift < 32; ++shift) {
				s += tally<N>([shift](const Trial<N>& x) { return x.a >> shift; },
				              [shift](const Trial<N>& x, std::size_t i) { return x.a.lane[i] >> shift; });
				s += tally<N>([shift](const Trial<N>& x) { return x.a << shift; },
				              [shift](const Trial<N>& x, std::size_t i) { return x.a.lane[i] << shift; });
			}
			return s;
		});
		expect(t.mismatched).to_equal(std::size_t{0});
		expect(t.compared).to_equal(64 * kLanesPerWidthSum);
	});

	it("takes each lane's high word of the product exactly as multiply_32x32_rshift32 does", _ {
		const Tally t = over_widths([]<std::size_t N>() {
			Tally s = tally<N>([](const Trial<N>& x) { return multiply_32x32_rshift32(x.a, x.b); },
			                   [](const Trial<N>& x, std::size_t i) {
				                   return ::multiply_32x32_rshift32(x.a.lane[i], x.b.lane[i]);
			                   });
			// A scalar second operand multiplies every lane, as a kernel's shared coefficient does.
			s += tally<N>([](const Trial<N>& x) { return multiply_32x32_rshift32(x.a, x.b.lane[0]); },
			              [](const Trial<N>& x, std::size_t i) {
				              return ::multiply_32x32_rshift32(x.a.lane[i], x.b.lane[0]);
			              });
			return s;
		});
		expect(t.mismatched).to_equal(std::size_t{0});
		expect(t.compared).to_equal(2 * kLanesPerWidthSum);
	});

	it("rounds each lane's high word exactly as multiply_32x32_rshift32_rounded does", _ {
		const Tally t = over_widths([]<std::size_t N>() {
			Tally s = tally<N>([](const Trial<N>& x) { return multiply_32x32_rshift32_rounded(x.a, x.b); },
			                   [](const Trial<N>& x, std::size_t i) {
				                   return ::multiply_32x32_rshift32_rounded(x.a.lane[i], x.b.lane[i]);
			                   });
			s += tally<N>([](const Trial<N>& x) { return multiply_32x32_rshift32_rounded(x.a, x.b.lane[0]); },
			              [](const Trial<N>& x, std::size_t i) {
				              return ::multiply_32x32_rshift32_rounded(x.a.lane[i], x.b.lane[0]);
			              });
			return s;
		});
		expect(t.mismatched).to_equal(std::size_t{0});
		expect(t.compared).to_equal(2 * kLanesPerWidthSum);

		// The rounding is there: 3 × 2^31 is 1.5 × 2^32, which the truncating multiply takes to 1 and this to 2. At
		// INT32_MIN squared, 2^62, neither saturates: the high word is 2^30.
		const auto a = LanePack<q31, 2>::load(Frame<q31, 2>{{3, kMin}});
		const auto b = LanePack<q31, 2>::load(Frame<q31, 2>{{q31{1} << 30, kMin}});
		const LanePack<q31, 2> doubled = multiply_32x32_rshift32_rounded(a << 1, b);
		expect(doubled.lane[0]).to_equal(q31{2});
		expect(multiply_32x32_rshift32(a << 1, b).lane[0]).to_equal(q31{1});
		expect(multiply_32x32_rshift32_rounded(a, b).lane[1]).to_equal(q31{1} << 30);
	});

	it("accumulates each lane's rounded high word exactly as multiply_accumulate_32x32_rshift32_rounded does", _ {
		const Tally t = over_widths([]<std::size_t N>() {
			Tally s = tally<N>(
			    [](const Trial<N>& x) { return multiply_accumulate_32x32_rshift32_rounded(x.c, x.a, x.b); },
			    [](const Trial<N>& x, std::size_t i) {
				    return ::multiply_accumulate_32x32_rshift32_rounded(x.c.lane[i], x.a.lane[i], x.b.lane[i]);
			    });
			s += tally<N>(
			    [](const Trial<N>& x) { return multiply_accumulate_32x32_rshift32_rounded(x.c, x.a, x.b.lane[0]); },
			    [](const Trial<N>& x, std::size_t i) {
				    return ::multiply_accumulate_32x32_rshift32_rounded(x.c.lane[i], x.a.lane[i], x.b.lane[0]);
			    });
			return s;
		});
		expect(t.mismatched).to_equal(std::size_t{0});
		expect(t.compared).to_equal(2 * kLanesPerWidthSum);
	});

	it("compares and selects lane by lane", _ {
		const Tally t = over_widths([]<std::size_t N>() {
			Tally s = tally<N>([](const Trial<N>& x) { return x.a < x.b; },
			                   [](const Trial<N>& x, std::size_t i) { return x.a.lane[i] < x.b.lane[i]; });
			s += tally<N>([](const Trial<N>& x) { return x.a > x.b; },
			              [](const Trial<N>& x, std::size_t i) { return x.a.lane[i] > x.b.lane[i]; });
			s += tally<N>([](const Trial<N>& x) { return select(x.c < q31{0}, x.a, x.b); },
			              [](const Trial<N>& x, std::size_t i) { return x.c.lane[i] < 0 ? x.a.lane[i] : x.b.lane[i]; });
			return s;
		});
		expect(t.mismatched).to_equal(std::size_t{0});
		expect(t.compared).to_equal(3 * kLanesPerWidthSum);

		// A scalar select picks as the ternary does.
		expect(select(true, kMin, kMax)).to_equal(kMin);
		expect(select(false, kMin, kMax)).to_equal(kMax);
	});

	it("takes each lane's max and min as AntialiasedTanh's two ternaries do", _ {
		const Tally t = over_widths([]<std::size_t N>() {
			Tally s = tally<N>([](const Trial<N>& x) { return max(x.a, x.b); },
			                   [](const Trial<N>& x, std::size_t i) {
				                   return x.a.lane[i] < x.b.lane[i] ? x.b.lane[i] : x.a.lane[i];
			                   });
			s += tally<N>([](const Trial<N>& x) { return min(x.a, x.b); },
			              [](const Trial<N>& x, std::size_t i) {
				              return x.a.lane[i] > x.b.lane[i] ? x.b.lane[i] : x.a.lane[i];
			              });
			s += tally<N>([](const Trial<N>& x) { return max(x.a, x.b.lane[0]); },
			              [](const Trial<N>& x, std::size_t i) {
				              return x.a.lane[i] < x.b.lane[0] ? x.b.lane[0] : x.a.lane[i];
			              });
			return s;
		});
		expect(t.mismatched).to_equal(std::size_t{0});
		expect(t.compared).to_equal(3 * kLanesPerWidthSum);

		expect(max(q31{-3}, q31{2})).to_equal(q31{2});
		expect(min(q31{-3}, q31{2})).to_equal(q31{-3});
	});

	it("runs OnePole's step written once over V, for q31 and per lane of the pack, alike", _ {
		const Tally t = over_widths([]<std::size_t N>() {
			Tally s;
			for (const q31 m : {q31{0}, q31{1} << 24, ONE_Q31 / 3, kMax}) {
				std::array<q31, N> z{};
				LanePack<q31, N> zp{};
				for (const Trial<N>& x : trials<N>()) {
					const LanePack<q31, N> got = probe::one_pole_step(m, zp, x.a);
					for (std::size_t i = 0; i < N; ++i) {
						OnePole::State st{.z = z[i]};
						const q31 kernel = OnePole::step<OnePole::Mode::ALL_PASS>({.m = m}, {}, st, x.a.lane[i]);
						const q31 scalar = probe::one_pole_step(m, z[i], x.a.lane[i]);
						++s.compared;
						s.mismatched += (got.lane[i] == scalar && scalar == kernel && st.z == z[i]
						                 && zp.lane[i] == z[i]) ? 0 : 1;
					}
				}
			}
			return s;
		});
		expect(t.mismatched).to_equal(std::size_t{0});
		expect(t.compared).to_equal(4 * kLanesPerWidthSum);
	});

	it("runs ShiftTracker's step written once over V, for q31 and per lane of the pack, alike", _ {
		using Blocker = fixed::ShiftTracker<11>;
		const Tally t = over_widths([]<std::size_t N>() {
			Tally s;
			std::array<q31, N> y{};
			LanePack<q31, N> yp{};
			for (const Trial<N>& x : trials<N>()) {
				const LanePack<q31, N> got = probe::shift_tracker_step(yp, x.a);
				for (std::size_t i = 0; i < N; ++i) {
					Blocker::State st{.y = y[i]};
					const q31 kernel = Blocker::step<Blocker::Mode::REMOVE>({.mode = Blocker::Mode::REMOVE}, {}, st,
					                                                        x.a.lane[i]);
					const q31 scalar = probe::shift_tracker_step(y[i], x.a.lane[i]);
					++s.compared;
					s.mismatched += (got.lane[i] == scalar && scalar == kernel && st.y == y[i]
					                 && yp.lane[i] == y[i]) ? 0 : 1;
				}
			}
			return s;
		});
		expect(t.mismatched).to_equal(std::size_t{0});
		expect(t.compared).to_equal(kLanesPerWidthSum);
	});

	it("clamps as AntialiasedTanh does, written once over V, at every drive's bounds", _ {
		const Tally t = over_widths([]<std::size_t N>() {
			Tally s;
			for (uint32_t drive = 0; drive <= 30; ++drive) {
				const AntialiasedTanh::Coefficients c = AntialiasedTanh::coefficients({.drive = drive});
				const auto ternaries = [&c](const Trial<N>& x, std::size_t i) {
					const q31 low_clamped = x.a.lane[i] < c.floor ? c.floor : x.a.lane[i];
					return low_clamped > c.ceiling ? c.ceiling : low_clamped;
				};
				s += tally<N>([&c](const Trial<N>& x) { return probe::clamp(x.a, c.floor, c.ceiling); }, ternaries);
				s += tally<N>([&c](const Trial<N>& x) { return probe::clamp_by_select(x.a, c.floor, c.ceiling); },
				              ternaries);
				s += tally<N>(
				    [&c](const Trial<N>& x) {
					    LanePack<q31, N> scalar{};
					    for (std::size_t i = 0; i < N; ++i) {
						    scalar.lane[i] = probe::clamp(x.a.lane[i], c.floor, c.ceiling);
					    }
					    return scalar;
				    },
				    ternaries);
				s += tally<N>(
				    [&c](const Trial<N>& x) {
					    LanePack<q31, N> scalar{};
					    for (std::size_t i = 0; i < N; ++i) {
						    scalar.lane[i] = probe::clamp_by_select(x.a.lane[i], c.floor, c.ceiling);
					    }
					    return scalar;
				    },
				    ternaries);
			}
			return s;
		});
		expect(t.mismatched).to_equal(std::size_t{0});
		expect(t.compared).to_equal(4 * 31 * kLanesPerWidthSum);
	});
});
// clang-format on

CPPSPEC_MAIN(lane_pack_spec);
