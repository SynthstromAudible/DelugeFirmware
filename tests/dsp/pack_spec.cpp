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

// Pack<T, N>: every operation is its scalar counterpart applied lane by lane: Pack's own on raw int32_t lanes,
// and fixed_point_lanes.h's Q31 arithmetic on Pack<Q<31>, N> against the raw Q31 functions. Each is pinned against
// that counterpart at N = 1, 2, 3, 4 and 8, over every pairing (and, for the accumulate, every triple) of Q31's
// extremes, with each lane given a different pairing, and over full-scale seeded noise. The extremes include the pairs
// whose sum or difference leaves Q31, and the products at INT32_MIN. Then one generic expression each of OnePole's
// step and ShiftTracker's step, written once over V, compiles for Q<31> and for the pack, and AntialiasedTanh's clamp
// does for a raw word and for the pack of them, each lane given the scalar result.

#include "dsp-next/core/pack.h"

#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/fixed_point_lanes.h"
#include "dsp-next/core/frame.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/filter/one_pole.h"
#include "dsp-next/fixed/filter/shift_tracker.h"
#include "dsp-next/fixed/noise/noise.h"
#include "dsp-next/fixed/shaping/antialiased_tanh.h"
#include "raw_words.h"

#include <array>
#include <concepts>
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
[[nodiscard]] V one_pole_step(Q<31> m, V& z, V x) {
	const V v = mul_r_as_wrap<Q<31>>(sub_wrap(x, z), m);
	const V y = add_wrap(v, z);
	z = add_wrap(y, v);
	return sub_wrap(add_wrap(y, y), x);
}

/// ShiftTracker<11>'s REMOVE step.
template <class V>
[[nodiscard]] V shift_tracker_step(V& y, V x) {
	y = add_wrap(y, scale_shr_floor<11>(sub_wrap(x, y)));
	return sub_wrap(x, y);
}

/// AntialiasedTanh's clamp: below the floor to the floor, then above the ceiling to the ceiling.
template <class V>
[[nodiscard]] V clamp(V x, int32_t floor, int32_t ceiling) {
	return min(max(x, floor), ceiling);
}

/// The same clamp through the comparisons and select.
template <class V>
[[nodiscard]] V clamp_by_select(V x, int32_t floor, int32_t ceiling) {
	const V low_clamped = select(x < floor, V{floor}, x);
	return select(low_clamped > ceiling, V{ceiling}, low_clamped);
}

} // namespace deluge::dsp::fixed::lane_pack_probe

namespace {

using deluge::dsp::fixed::AntialiasedTanh;
using deluge::dsp::fixed::OnePole;
using deluge::dsp::fixed::SeededNoise;
namespace probe = deluge::dsp::fixed::lane_pack_probe;

static_assert(std::is_trivially_copyable_v<Pack<Q<31>, 1>>);
static_assert(std::is_trivially_copyable_v<Pack<Q<31>, 8>>);
static_assert(sizeof(Pack<Q<31>, 4>) == 4 * sizeof(Q<31>));
static_assert(sizeof(Pack<Q<31>, 2>) == sizeof(Frame<Q<31>, 2>));

// A lane holds what the scalar operation gives, so + and - exist only where that is a T: a narrower T promotes to int.
template <typename P>
concept adds = requires(P a, P b) {
	{ a + b } -> std::same_as<P>;
};
template <typename P>
concept subtracts = requires(P a, P b) {
	{ a - b } -> std::same_as<P>;
};
static_assert(adds<Pack<int32_t, 2>> && subtracts<Pack<int32_t, 2>>);
static_assert(adds<Pack<float, 2>> && subtracts<Pack<float, 2>>);
static_assert(!adds<Pack<int16_t, 2>> && !subtracts<Pack<int16_t, 2>>);
static_assert(!adds<Pack<uint8_t, 2>> && !subtracts<Pack<uint8_t, 2>>);
static_assert(!adds<Pack<bool, 2>> && !subtracts<Pack<bool, 2>>);
static_assert(!adds<Pack<Q<31>, 2>> && !subtracts<Pack<Q<31>, 2>>, "a pack of Fixed adds as a Fixed does, by name");

constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
constexpr int32_t kMax = std::numeric_limits<int32_t>::max();

/// Q31's extremes and the values next to them: every pairing overflows a sum or a difference somewhere.
constexpr std::array<int32_t, 9> kExtremes{kMin, kMin + 1, kMin / 2, -1, 0, 1, kMax / 2, kMax - 1, kMax};
/// The full-scale noise trials after the extremes.
constexpr std::size_t kNoiseTrials = 2'000;
/// The seed of the noise trials.
constexpr uint32_t kSeed = 0x1A4E'5EED;

/// Three packs an operation takes: a and b for a binary operation, c as the accumulate's sum or a select's sign.
template <std::size_t N>
struct Trial {
	Pack<int32_t, N> a;
	Pack<int32_t, N> b;
	Pack<int32_t, N> c;
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

/// Whether @p a + @p b, or @p a - @p b when @p subtract, stays inside Q31.
bool in_range(int32_t a, int32_t b, bool subtract) {
	const int64_t r = subtract ? int64_t{a} - int64_t{b} : int64_t{a} + int64_t{b};
	return r >= int64_t{kMin} && r <= int64_t{kMax};
}

/// Whether every lane of @p a op @p b stays inside Q31, for the unwrapped + and -, whose overflow is undefined.
template <std::size_t N>
bool lanes_in_range(const Pack<int32_t, N>& a, const Pack<int32_t, N>& b, bool subtract) {
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
describe pack_spec("Pack", ${
	it("broadcasts a scalar to every lane, and loads and stores a frame lane for lane", _ {
		const Pack<int32_t, 3> b = Pack<int32_t, 3>::broadcast(kMin);
		for (const int32_t x : b.lane) {
			expect(x).to_equal(kMin);
		}
		const Pack<int32_t, 3> implicit = int32_t{-9};
		for (const int32_t x : implicit.lane) {
			expect(x).to_equal(int32_t{-9});
		}

		const Frame<int32_t, 3> f{{kMin, 0, kMax}};
		const Pack<int32_t, 3> p = Pack<int32_t, 3>::load(f);
		expect(p.lane[0]).to_equal(kMin);
		expect(p.lane[1]).to_equal(int32_t{0});
		expect(p.lane[2]).to_equal(kMax);

		Frame<int32_t, 3> out{{1, 2, 3}};
		(p >> 1).store(out);
		expect(out[0]).to_equal(kMin >> 1);
		expect(out[1]).to_equal(int32_t{0});
		expect(out[2]).to_equal(kMax >> 1);

		const Pack<int32_t, 2> zero{};
		expect(zero.lane[0]).to_equal(int32_t{0});
		expect(zero.lane[1]).to_equal(int32_t{0});
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
		// The trials are fixed, so the lanes whose scalar sum or difference stays in range are a fixed count of the
		// 2 * kLanesPerWidthSum offered.
		expect(t.compared).to_equal(std::size_t{27'576});
	});

	it("wraps its sums and differences lane by lane, as the scalar wrapping_add and wrapping_sub do", _ {
		const Tally t = over_widths([]<std::size_t N>() {
			Tally s = tally<N>([](const Trial<N>& x) { return add_wrap(from_raw<Q<31>>(x.a), from_raw<Q<31>>(x.b)); },
			                   [](const Trial<N>& x, std::size_t i) {
				                   return Q<31>::from_raw(deluge::dsp::wrapping_add(x.a.lane[i], x.b.lane[i]));
			                   });
			s += tally<N>([](const Trial<N>& x) { return sub_wrap(from_raw<Q<31>>(x.a), from_raw<Q<31>>(x.b)); },
			              [](const Trial<N>& x, std::size_t i) {
				              return Q<31>::from_raw(deluge::dsp::wrapping_sub(x.a.lane[i], x.b.lane[i]));
			              });
			return s;
		});
		expect(t.mismatched).to_equal(std::size_t{0});
		expect(t.compared).to_equal(2 * kLanesPerWidthSum);

		// The pairs that leave Q31 are among them, and wrap.
		const auto top = from_raw<Q<31>>(Pack<int32_t, 2>::load(Frame<int32_t, 2>{{kMax, kMin}}));
		const auto one = from_raw<Q<31>>(Pack<int32_t, 2>::load(Frame<int32_t, 2>{{1, 1}}));
		const Pack<Q<31>, 2> sum = add_wrap(top, one);
		const Pack<Q<31>, 2> difference = sub_wrap(top, one);
		expect(sum.lane[0].raw()).to_equal(kMin);
		expect(difference.lane[1].raw()).to_equal(kMax);
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
			Tally s = tally<N>([](const Trial<N>& x) { return mul(from_raw<Q<31>>(x.a), from_raw<Q<31>>(x.b)); },
			                   [](const Trial<N>& x, std::size_t i) {
				                   return Q<30>::from_raw(::multiply_32x32_rshift32(x.a.lane[i], x.b.lane[i]));
			                   });
			// A scalar second operand multiplies every lane, as a kernel's shared coefficient does.
			s += tally<N>([](const Trial<N>& x) { return mul(from_raw<Q<31>>(x.a), Q<31>::from_raw(x.b.lane[0])); },
			              [](const Trial<N>& x, std::size_t i) {
				              return Q<30>::from_raw(::multiply_32x32_rshift32(x.a.lane[i], x.b.lane[0]));
			              });
			return s;
		});
		expect(t.mismatched).to_equal(std::size_t{0});
		expect(t.compared).to_equal(2 * kLanesPerWidthSum);
	});

	it("rounds each lane's high word exactly as multiply_32x32_rshift32_rounded does", _ {
		const Tally t = over_widths([]<std::size_t N>() {
			Tally s = tally<N>([](const Trial<N>& x) { return mul_r(from_raw<Q<31>>(x.a), from_raw<Q<31>>(x.b)); },
			                   [](const Trial<N>& x, std::size_t i) {
				                   return Q<30>::from_raw(::multiply_32x32_rshift32_rounded(x.a.lane[i], x.b.lane[i]));
			                   });
			s += tally<N>([](const Trial<N>& x) { return mul_r(from_raw<Q<31>>(x.a), Q<31>::from_raw(x.b.lane[0])); },
			              [](const Trial<N>& x, std::size_t i) {
				              return Q<30>::from_raw(::multiply_32x32_rshift32_rounded(x.a.lane[i], x.b.lane[0]));
			              });
			return s;
		});
		expect(t.mismatched).to_equal(std::size_t{0});
		expect(t.compared).to_equal(2 * kLanesPerWidthSum);

		// The rounding is there: 3 × 2^31 is 1.5 × 2^32, which the truncating multiply takes to 1 and this to 2. At
		// INT32_MIN squared, 2^62, neither saturates: the high word is 2^30.
		const auto a = from_raw<Q<31>>(Pack<int32_t, 2>::load(Frame<int32_t, 2>{{3, kMin}}));
		const auto b = from_raw<Q<31>>(Pack<int32_t, 2>::load(Frame<int32_t, 2>{{int32_t{1} << 30, kMin}}));
		const Pack<Q<30>, 2> doubled = mul_r(scale_shl_wrap<1>(a), b);
		expect(doubled.lane[0].raw()).to_equal(int32_t{2});
		expect(mul(scale_shl_wrap<1>(a), b).lane[0].raw()).to_equal(int32_t{1});
		expect(mul_r(a, b).lane[1].raw()).to_equal(int32_t{1} << 30);
	});

	it("accumulates each lane's rounded high word exactly as multiply_accumulate_32x32_rshift32_rounded does", _ {
		const Tally t = over_widths([]<std::size_t N>() {
			Tally s = tally<N>(
			    [](const Trial<N>& x) {
				    return mla_r(from_raw<Q<30>>(x.c), from_raw<Q<31>>(x.a), from_raw<Q<31>>(x.b));
			    },
			    [](const Trial<N>& x, std::size_t i) {
				    return Q<30>::from_raw(
				        ::multiply_accumulate_32x32_rshift32_rounded(x.c.lane[i], x.a.lane[i], x.b.lane[i]));
			    });
			s += tally<N>(
			    [](const Trial<N>& x) {
				    return mla_r(from_raw<Q<30>>(x.c), from_raw<Q<31>>(x.a), Q<31>::from_raw(x.b.lane[0]));
			    },
			    [](const Trial<N>& x, std::size_t i) {
				    return Q<30>::from_raw(
				        ::multiply_accumulate_32x32_rshift32_rounded(x.c.lane[i], x.a.lane[i], x.b.lane[0]));
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
			s += tally<N>([](const Trial<N>& x) { return select(x.c < int32_t{0}, x.a, x.b); },
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

		expect(max(int32_t{-3}, int32_t{2})).to_equal(int32_t{2});
		expect(min(int32_t{-3}, int32_t{2})).to_equal(int32_t{-3});
	});

	it("runs OnePole's step written once over V, for Q<31> and per lane of the pack, alike", _ {
		const Tally t = over_widths([]<std::size_t N>() {
			Tally s;
			for (const int32_t m_raw : {int32_t{0}, int32_t{1} << 24, kOneQ31 / 3, kMax}) {
				const Q<31> m = Q<31>::from_raw(m_raw);
				std::array<Q<31>, N> z{};
				Pack<Q<31>, N> zp{};
				for (const Trial<N>& x : trials<N>()) {
					const Pack<Q<31>, N> got = probe::one_pole_step(m, zp, from_raw<Q<31>>(x.a));
					for (std::size_t i = 0; i < N; ++i) {
						OnePole::State st{.z = z[i]};
						const Q<31> kernel = OnePole::step<OnePole::Mode::ALL_PASS>({.m = m}, {}, st, Q<31>::from_raw(x.a.lane[i]));
						const Q<31> scalar = probe::one_pole_step(m, z[i], Q<31>::from_raw(x.a.lane[i]));
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

	it("runs ShiftTracker's step written once over V, for Q<31> and per lane of the pack, alike", _ {
		using Blocker = fixed::ShiftTracker<11>;
		const Tally t = over_widths([]<std::size_t N>() {
			Tally s;
			std::array<Q<31>, N> y{};
			Pack<Q<31>, N> yp{};
			for (const Trial<N>& x : trials<N>()) {
				const Pack<Q<31>, N> got = probe::shift_tracker_step(yp, from_raw<Q<31>>(x.a));
				for (std::size_t i = 0; i < N; ++i) {
					Blocker::State st{.y = y[i]};
					const Q<31> kernel = Blocker::step<Blocker::Mode::REMOVE>({.mode = Blocker::Mode::REMOVE}, {}, st, Q<31>::from_raw(x.a.lane[i]));
					const Q<31> scalar = probe::shift_tracker_step(y[i], Q<31>::from_raw(x.a.lane[i]));
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
				// The bounds as raw words, the lanes' format here
				const int32_t floor = c.floor.raw();
				const int32_t ceiling = c.ceiling.raw();
				const auto ternaries = [&](const Trial<N>& x, std::size_t i) {
					const int32_t low_clamped = x.a.lane[i] < floor ? floor : x.a.lane[i];
					return low_clamped > ceiling ? ceiling : low_clamped;
				};
				s += tally<N>([&](const Trial<N>& x) { return probe::clamp(x.a, floor, ceiling); }, ternaries);
				s += tally<N>([&](const Trial<N>& x) { return probe::clamp_by_select(x.a, floor, ceiling); },
				              ternaries);
				s += tally<N>(
				    [&](const Trial<N>& x) {
					    Pack<int32_t, N> scalar{};
					    for (std::size_t i = 0; i < N; ++i) {
						    scalar.lane[i] = probe::clamp(x.a.lane[i], floor, ceiling);
					    }
					    return scalar;
				    },
				    ternaries);
				s += tally<N>(
				    [&](const Trial<N>& x) {
					    Pack<int32_t, N> scalar{};
					    for (std::size_t i = 0; i < N; ++i) {
						    scalar.lane[i] = probe::clamp_by_select(x.a.lane[i], floor, ceiling);
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

CPPSPEC_MAIN(pack_spec);
