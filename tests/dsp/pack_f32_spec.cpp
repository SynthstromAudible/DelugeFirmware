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

// Pack<f32, N>: every operation equals its scalar counterpart lane by lane, bit for bit, at N = 2 and 4, under
// flush-to-zero; mul_add fuses exactly when kFusedMulAdd says so; the transpose is its own inverse.

#include "dsp-next/core/flush_to_zero.h"
#include "dsp-next/core/pack_f32.h"

#include <array>
#include <bit>
#include <cmath>
#include <cppspec.hpp>
#include <cstdint>
#include <random>

using namespace deluge::dsp;

// The spec's own float arithmetic rounds as written, as the pack's does, so its twin built with the device's float
// flags (pack_f32_unsafe_math_spec) compares each lane against IEEE order. The headers above pin themselves.
#pragma clang fp contract(off) reassociate(off) reciprocal(off)

namespace {

[[nodiscard]] uint32_t bits(f32 x) {
	return std::bit_cast<uint32_t>(x);
}

/// The corner values, which the operands and the corner-pair trials draw from.
constexpr std::array<f32, 8> kCorners{0.f, -0.f, 1e-40f, -1e-40f, 1.f, -1.f, 3.0e38f, -3.0e38f};

/// Operands: signed zeros, denormals (which flush), ±1, near-overflow and seeded noise over ±4.
template <std::size_t N>
[[nodiscard]] std::array<Pack<f32, N>, 3> operands(uint32_t seed) {
	std::mt19937 rng{seed};
	std::uniform_real_distribution<f32> d{-4.f, 4.f};
	std::array<Pack<f32, N>, 3> r{};
	for (std::size_t k = 0; k < 3; ++k) {
		for (std::size_t i = 0; i < N; ++i) {
			r[k].set(i, (seed % 2 == 0) ? kCorners[(seed + k * 3 + i) % kCorners.size()] : d(rng));
		}
	}
	return r;
}

template <std::size_t N, class Op, class Scalar>
void expect_lanewise(auto& self, Op op, Scalar scalar) {
	for (uint32_t seed = 0; seed < 64; ++seed) {
		const auto [a, b, c] = operands<N>(seed);
		const Pack<f32, N> r = op(a, b, c);
		for (std::size_t i = 0; i < N; ++i) {
			expect(bits(r.get(i))).to_equal(bits(scalar(a.get(i), b.get(i), c.get(i))));
		}
	}
	// Every ordered triple of corners in every lane, so the ties (+0, -0), (-0, +0), a denormal against the other
	// sign's and against zero meet whatever the operands' stride.
	for (const f32 x : kCorners) {
		for (const f32 y : kCorners) {
			for (const f32 z : kCorners) {
				const Pack<f32, N> r = op(Pack<f32, N>{x}, Pack<f32, N>{y}, Pack<f32, N>{z});
				for (std::size_t i = 0; i < N; ++i) {
					expect(bits(r.get(i))).to_equal(bits(scalar(x, y, z)));
				}
			}
		}
	}
}

template <std::size_t N>
void pack_specs(auto& self) {
	// clang-format off
	it("adds, subtracts and multiplies lane by lane", _ {
		expect_lanewise<N>(self, [](auto a, auto b, auto) { return a + b; }, [](f32 a, f32 b, f32) { return a + b; });
		expect_lanewise<N>(self, [](auto a, auto b, auto) { return a - b; }, [](f32 a, f32 b, f32) { return a - b; });
		expect_lanewise<N>(self, [](auto a, auto b, auto) { return a * b; }, [](f32 a, f32 b, f32) { return a * b; });
	});
	it("multiply-adds as the scalar mul_add does", _ {
		expect_lanewise<N>(self, [](auto a, auto b, auto c) { return mul_add(a, b, c); },
		                   [](f32 a, f32 b, f32 c) { return mul_add(a, b, c); });
	});
	it("takes min, max, abs and selects lane by lane", _ {
		expect_lanewise<N>(self, [](auto a, auto b, auto) { return min(a, b); }, [](f32 a, f32 b, f32) { return min(a, b); });
		expect_lanewise<N>(self, [](auto a, auto b, auto) { return max(a, b); }, [](f32 a, f32 b, f32) { return max(a, b); });
		expect_lanewise<N>(self, [](auto a, auto, auto) { return abs(a); }, [](f32 a, f32, f32) { return deluge::dsp::abs(a); });
		expect_lanewise<N>(self, [](auto a, auto b, auto c) { return select(a < b, c, a); },
		                   [](f32 a, f32 b, f32 c) { return select(a < b, c, a); });
	});
	it("truncates non-negative values lane by lane as the scalar form does, and to the floor", _ {
		std::mt19937 rng{3};
		std::uniform_real_distribution<f32> d{0.f, 2100.f};
		for (int round = 0; round < 64; ++round) {
			Pack<f32, N> x{};
			for (std::size_t i = 0; i < N; ++i) {
				x.set(i, round < 2 ? (round == 0 ? 0.f : 8388607.f) : (round % 3 == 0 ? std::floor(d(rng)) : d(rng)));
			}
			const Pack<f32, N> got = trunc_nonnegative(x);
			for (std::size_t i = 0; i < N; ++i) {
				expect(bits(got.get(i))).to_equal(bits(trunc_nonnegative(x.get(i))));
				expect(bits(got.get(i))).to_equal(bits(std::floor(x.get(i))));
			}
		}
	});
	it("sums its lanes in lane order", _ {
		const auto [a, b, c] = operands<N>(7);
		f32 expected = a.get(0);
		for (std::size_t i = 1; i < N; ++i) {
			expected = expected + a.get(i);
		}
		expect(bits(sum(a))).to_equal(bits(expected));
	});
	it("gathers one table entry per lane", _ {
		std::array<f32, 16> table{};
		for (std::size_t i = 0; i < table.size(); ++i) {
			table[i] = 0.5f * static_cast<f32>(i) - 3.f;
		}
		Pack<f32, N> index{};
		for (std::size_t i = 0; i < N; ++i) {
			index.set(i, static_cast<f32>((i * 5) % table.size()));
		}
		const Pack<f32, N> got = gather(table.data(), index);
		for (std::size_t i = 0; i < N; ++i) {
			expect(bits(got.get(i))).to_equal(bits(gather(table.data(), index.get(i))));
		}
	});
	it("reports whether any lane of a mask is set", _ {
		Pack<f32, N> a{0.f};
		expect(any(a > Pack<f32, N>{0.f})).to_be_false();
		a.set(N - 1, 1.f);
		const Mask<N> m = a > Pack<f32, N>{0.f};
		expect(any(m)).to_be_true();
		for (std::size_t i = 0; i < N; ++i) {
			expect(m.get(i)).to_equal(i == N - 1);
		}
	});
	it("transposes and transposes back", _ {
		std::array<Pack<f32, N>, N> m{};
		for (std::size_t r = 0; r < N; ++r) {
			for (std::size_t c = 0; c < N; ++c) {
				m[r].set(c, static_cast<f32>((r * 10) + c));
			}
		}
		auto t = m;
		transpose(t);
		for (std::size_t r = 0; r < N; ++r) {
			for (std::size_t c = 0; c < N; ++c) {
				expect(t[r].get(c)).to_equal(m[c].get(r));
			}
		}
		transpose(t);
		for (std::size_t r = 0; r < N; ++r) {
			for (std::size_t c = 0; c < N; ++c) {
				expect(t[r].get(c)).to_equal(m[r].get(c));
			}
		}
	});
	// clang-format on
}

} // namespace

// clang-format off
describe pack_f32_spec("Pack<f32, N>", ${
	fp::FlushToZero ftz;
	it("flushes a denormal to zero in the scalar reference", _ {
		volatile f32 tiny = 1e-30f;
		expect(bits(tiny * 1e-10f)).to_equal(bits(0.f));
	});
	it("fuses mul_add exactly when kFusedMulAdd", _ {
		const f32 b = 1.0f + 0x1p-23f;
		const f32 a = -(1.0f + 0x1p-22f);
		expect(bits(mul_add(a, b, b))).to_equal(bits(kFusedMulAdd ? std::fma(b, b, a) : a + (b * b)));
	});
	it("fuses a pack's mul_add exactly when kFusedMulAdd, in every lane", _ {
		const f32 b = 1.0f + 0x1p-23f;
		const f32 a = -(1.0f + 0x1p-22f);
		volatile f32 product = b * b;
		const f32 want = kFusedMulAdd ? std::fma(b, b, a) : a + product;
		const Pack<f32, 2> r2 = mul_add(Pack<f32, 2>{a}, Pack<f32, 2>{b}, Pack<f32, 2>{b});
		const Pack<f32, 4> r4 = mul_add(Pack<f32, 4>{a}, Pack<f32, 4>{b}, Pack<f32, 4>{b});
		for (std::size_t i = 0; i < 2; ++i) {
			expect(bits(r2.get(i))).to_equal(bits(want));
		}
		for (std::size_t i = 0; i < 4; ++i) {
			expect(bits(r4.get(i))).to_equal(bits(want));
		}
	});
	context("at N = 2", _ { pack_specs<2>(self); });
	context("at N = 4", _ { pack_specs<4>(self); });
});
// clang-format on

CPPSPEC_MAIN(pack_f32_spec);
