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
// Each fixed-point operation, and its portable twin, against the Cortex-A9 instruction it stands for. The
// instruction is the ground truth (inline asm); the public operation is the path the firmware takes (ACLE or plain
// C); the portable twin is the path a host takes. All three must agree bit for bit.

#include "dsp-next/core/fixed_point.h"
#include <arm_neon.h>
#include <array>
#include <bit>
#include <cppspec.hpp>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace deluge::dsp;

namespace {

// --- Ground truth: the instructions themselves ---------------------------------------------------------------

std::int32_t isa_smmul(std::int32_t a, std::int32_t b) {
	std::int32_t out;
	asm("smmul %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}
std::int32_t isa_smmulr(std::int32_t a, std::int32_t b) {
	std::int32_t out;
	asm("smmulr %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}
std::int32_t isa_smmla(std::int32_t c, std::int32_t a, std::int32_t b) {
	std::int32_t out;
	asm("smmla %0, %1, %2, %3" : "=r"(out) : "r"(a), "r"(b), "r"(c));
	return out;
}
std::int32_t isa_smmlar(std::int32_t c, std::int32_t a, std::int32_t b) {
	std::int32_t out;
	asm("smmlar %0, %1, %2, %3" : "=r"(out) : "r"(a), "r"(b), "r"(c));
	return out;
}
std::int32_t isa_smmls(std::int32_t c, std::int32_t a, std::int32_t b) {
	std::int32_t out;
	asm("smmls %0, %1, %2, %3" : "=r"(out) : "r"(a), "r"(b), "r"(c));
	return out;
}
std::int32_t isa_smmlsr(std::int32_t c, std::int32_t a, std::int32_t b) {
	std::int32_t out;
	asm("smmlsr %0, %1, %2, %3" : "=r"(out) : "r"(a), "r"(b), "r"(c));
	return out;
}
std::int64_t isa_smull(std::int32_t a, std::int32_t b) {
	std::uint32_t lo;
	std::int32_t hi;
	asm("smull %0, %1, %2, %3" : "=&r"(lo), "=&r"(hi) : "r"(a), "r"(b));
	return static_cast<std::int64_t>((static_cast<std::uint64_t>(static_cast<std::uint32_t>(hi)) << 32) | lo);
}
std::int64_t isa_smlal(std::int64_t acc, std::int32_t a, std::int32_t b) {
	asm("smlal %Q0, %R0, %1, %2" : "+r"(acc) : "r"(a), "r"(b));
	return acc;
}
std::uint32_t isa_umull_hi(std::uint32_t a, std::uint32_t b) {
	std::uint32_t lo;
	std::uint32_t hi;
	asm("umull %0, %1, %2, %3" : "=&r"(lo), "=&r"(hi) : "r"(a), "r"(b));
	return hi;
}
std::int32_t isa_qadd(std::int32_t a, std::int32_t b) {
	std::int32_t out;
	asm("qadd %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}
std::int32_t isa_qsub(std::int32_t a, std::int32_t b) {
	std::int32_t out;
	asm("qsub %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}
std::int32_t isa_qdadd(std::int32_t a, std::int32_t b) {
	std::int32_t out;
	asm("qdadd %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}
std::int32_t isa_qdsub(std::int32_t a, std::int32_t b) {
	std::int32_t out;
	asm("qdsub %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}
template <int N>
std::int32_t isa_ssat(std::int32_t a) {
	std::int32_t out;
	asm("ssat %0, %1, %2" : "=r"(out) : "I"(N), "r"(a));
	return out;
}
template <int N>
std::int32_t isa_usat(std::int32_t a) {
	std::int32_t out;
	asm("usat %0, %1, %2" : "=r"(out) : "I"(N), "r"(a));
	return out;
}
std::int32_t isa_ssat16_asr16(std::int32_t a) {
	std::int32_t out;
	asm("ssat %0, #16, %1, asr #16" : "=r"(out) : "r"(a));
	return out;
}
std::int32_t isa_smulwb(std::int32_t a, std::int32_t b) {
	std::int32_t out;
	asm("smulwb %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}
std::int32_t isa_smulwt(std::int32_t a, std::int32_t b) {
	std::int32_t out;
	asm("smulwt %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}
std::int32_t isa_smlawb(std::int32_t c, std::int32_t a, std::int32_t b) {
	std::int32_t out;
	asm("smlawb %0, %1, %2, %3" : "=r"(out) : "r"(a), "r"(b), "r"(c));
	return out;
}
std::int32_t isa_smuad(std::int32_t a, std::int32_t b) {
	std::int32_t out;
	asm("smuad %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}
std::int32_t isa_smusd(std::int32_t a, std::int32_t b) {
	std::int32_t out;
	asm("smusd %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}
std::int32_t isa_smlad(std::int32_t c, std::int32_t a, std::int32_t b) {
	std::int32_t out;
	asm("smlad %0, %1, %2, %3" : "=r"(out) : "r"(a), "r"(b), "r"(c));
	return out;
}
std::int64_t isa_smlald(std::int64_t acc, std::int32_t a, std::int32_t b) {
	asm("smlald %Q0, %R0, %1, %2" : "+r"(acc) : "r"(a), "r"(b));
	return acc;
}
std::uint32_t isa_qadd16(std::uint32_t a, std::uint32_t b) {
	std::uint32_t out;
	asm("qadd16 %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}
std::uint32_t isa_qsub16(std::uint32_t a, std::uint32_t b) {
	std::uint32_t out;
	asm("qsub16 %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}
std::uint32_t isa_shadd16(std::uint32_t a, std::uint32_t b) {
	std::uint32_t out;
	asm("shadd16 %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}
std::int32_t isa_vqdmulh(std::int32_t a, std::int32_t b) {
	return vget_lane_s32(vqdmulh_s32(vdup_n_s32(a), vdup_n_s32(b)), 0);
}
std::int32_t isa_vqrdmulh(std::int32_t a, std::int32_t b) {
	return vget_lane_s32(vqrdmulh_s32(vdup_n_s32(a), vdup_n_s32(b)), 0);
}
template <int F>
std::int32_t isa_vcvt_to_fixed(float v) {
	asm("vcvt.s32.f32 %0, %1, %2" : "=t"(v) : "t"(v), "I"(F));
	return std::bit_cast<std::int32_t>(v);
}
template <int F>
float isa_vcvt_to_float(std::int32_t r) {
	asm("vcvt.f32.s32 %0, %1, %2" : "=t"(r) : "t"(r), "I"(F));
	return std::bit_cast<float>(r);
}

// --- Inputs ----------------------------------------------------------------------------------------------------

constexpr std::array<std::int32_t, 18> kEdges{0,
                                              1,
                                              -1,
                                              2,
                                              -2,
                                              INT32_MAX,
                                              INT32_MIN,
                                              INT32_MAX - 1,
                                              INT32_MIN + 1,
                                              1 << 30,
                                              -(1 << 30),
                                              0x7FFF,
                                              -0x8000,
                                              0x8000,
                                              0x7FFF'7FFF,
                                              -0x7FFF'8000,
                                              0x8000'8000 - 0x1'0000'0000LL,
                                              0x10000};

struct Triple {
	std::int32_t a;
	std::int32_t b;
	std::int32_t c;
};

const std::vector<Triple>& triples() {
	static const auto out = [] {
		std::vector<Triple> v;
		for (auto a : kEdges) {
			for (auto b : kEdges) {
				for (auto c : {0, 1, -1, INT32_MAX, INT32_MIN}) {
					v.push_back({a, b, c});
				}
			}
		}
		std::uint32_t s = 0x9E3779B9u;
		auto next = [&] {
			s ^= s << 13;
			s ^= s >> 17;
			s ^= s << 5;
			return static_cast<std::int32_t>(s);
		};
		for (int i = 0; i < 2'000'000; ++i) {
			const auto a = next();
			const auto b = next();
			v.push_back({a, b, next()});
		}
		return v;
	}();
	return out;
}

const std::vector<std::uint32_t>& float_inputs() {
	static const std::vector<std::uint32_t> v = [] {
		std::vector<std::uint32_t> out;
		for (float f : {0.0f,
		                -0.0f,
		                1.0f,
		                -1.0f,
		                0.5f,
		                -0.5f,
		                0.25f,
		                3.14f,
		                -3.14f,
		                42.25f,
		                0.9999999f,
		                -0.9999999f,
		                1.5f,
		                -1.5f,
		                2.9999998f,
		                -2.9999998f,
		                static_cast<float>(1u << 30),
		                static_cast<float>(1u << 31),
		                -static_cast<float>(1u << 31),
		                2147483647.0f,
		                -2147483648.0f,
		                2147483648.0f,
		                4294967296.0f,
		                1e30f,
		                -1e30f,
		                1e-30f}) {
			out.push_back(std::bit_cast<std::uint32_t>(f));
		}
		for (std::uint32_t bits : {0x7f800000u, 0xff800000u, 0x7fc00000u, 0x7f800001u, 0x00000001u, 0x80000001u}) {
			out.push_back(bits); // ±inf, qNaN, sNaN, ±smallest denormal
		}
		std::uint32_t x = 0x12345678u;
		for (int i = 0; i < 60000; ++i) {
			x = x * 1664525u + 1013904223u; // full-range LCG walk over the bit space
			out.push_back(x);
		}
		return out;
	}();
	return v;
}

// --- Checking --------------------------------------------------------------------------------------------------

std::size_t g_reported = 0;

/// Counts a mismatch and prints the first few, naming the operation and its inputs.
std::size_t differs(const char* what, std::int64_t got, std::int64_t want, const Triple& t) {
	if (got == want) {
		return 0;
	}
	if (g_reported < 30) {
		++g_reported;
		std::printf("  mismatch %s a=0x%08x b=0x%08x c=0x%08x got=0x%llx want=0x%llx\n", what,
		            static_cast<std::uint32_t>(t.a), static_cast<std::uint32_t>(t.b), static_cast<std::uint32_t>(t.c),
		            static_cast<unsigned long long>(got), static_cast<unsigned long long>(want));
	}
	return 1;
}

Q<31> q31(std::int32_t v) {
	return Q<31>::from_raw(v);
}
Q<30> q30(std::int32_t v) {
	return Q<30>::from_raw(v);
}
Q16x2<15> pair(std::int32_t v) {
	return Q16x2<15>::from_raw(static_cast<std::uint32_t>(v));
}

std::size_t integer_mismatches() {
	namespace p = detail::portable;
	std::size_t bad = 0;
	for (const auto& t : triples()) {
		const auto [a, b, c] = t;
		const auto ua = static_cast<std::uint32_t>(a);
		const auto ub = static_cast<std::uint32_t>(b);
		const auto b16 = Q16<15>::from_raw(static_cast<std::int16_t>(ub & 0xFFFFu));
		bad += differs("mul", mul(q31(a), q31(b)).raw(), isa_smmul(a, b), t);
		bad += differs("mul_r", mul_r(q31(a), q31(b)).raw(), isa_smmulr(a, b), t);
		bad += differs("mla", mla(q30(c), q31(a), q31(b)).raw(), isa_smmla(c, a, b), t);
		bad += differs("mla_r", mla_r(q30(c), q31(a), q31(b)).raw(), isa_smmlar(c, a, b), t);
		bad += differs("mls", mls(q30(c), q31(a), q31(b)).raw(), isa_smmls(c, a, b), t);
		bad += differs("mls_r", mls_r(q30(c), q31(a), q31(b)).raw(), isa_smmlsr(c, a, b), t);
		bad += differs("mul_wide", mul_wide(q31(a), q31(b)).raw(), isa_smull(a, b), t);
		const std::int64_t acc64 = (std::int64_t{c} << 32) | ua;
		bad += differs("mla_wide", mla_wide(Q64<62>::from_raw(acc64), q31(a), q31(b)).raw(), isa_smlal(acc64, a, b), t);
		bad += differs("mul_uq", mul(UQ<32>::from_raw(ua), UQ<32>::from_raw(ub)).raw(), isa_umull_hi(ua, ub), t);
		bad += differs("add_sat", add_sat(q31(a), q31(b)).raw(), isa_qadd(a, b), t);
		bad += differs("add_sat.portable", p::qadd(a, b), isa_qadd(a, b), t);
		bad += differs("sub_sat", sub_sat(q31(a), q31(b)).raw(), isa_qsub(a, b), t);
		bad += differs("sub_sat.portable", p::qsub(a, b), isa_qsub(a, b), t);
		bad += differs("neg_sat", neg_sat(q31(a)).raw(), isa_qsub(0, a), t);
		bad += differs("add_double_sat", add_double_sat(q31(a), q31(b)).raw(), isa_qdadd(a, b), t);
		bad += differs("add_double_sat.portable", p::qdadd(a, b), isa_qdadd(a, b), t);
		bad += differs("sub_double_sat", sub_double_sat(q31(a), q31(b)).raw(), isa_qdsub(a, b), t);
		bad += differs("sub_double_sat.portable", p::qdsub(a, b), isa_qdsub(a, b), t);
		bad += differs("as_sat<1>", as_sat<Q<31>>(q30(a)).raw(), isa_qadd(a, a), t);
		bad += differs("clamp_bits<1>", clamp_bits<1>(q31(a)).raw(), isa_ssat<1>(a), t);
		bad += differs("clamp_bits<16>", clamp_bits<16>(q31(a)).raw(), isa_ssat<16>(a), t);
		bad += differs("clamp_bits<24>", clamp_bits<24>(q31(a)).raw(), isa_ssat<24>(a), t);
		bad += differs("clamp_bits<31>", clamp_bits<31>(q31(a)).raw(), isa_ssat<31>(a), t);
		bad += differs("clamp_bits<32>", clamp_bits<32>(q31(a)).raw(), isa_ssat<32>(a), t);
		bad += differs("ssat<24>.portable", p::ssat<24>(a), isa_ssat<24>(a), t);
		bad += differs("clamp_unsigned_bits<0>", clamp_unsigned_bits<0>(q31(a)).raw(), isa_usat<0>(a), t);
		bad += differs("clamp_unsigned_bits<16>", clamp_unsigned_bits<16>(q31(a)).raw(), isa_usat<16>(a), t);
		bad += differs("clamp_unsigned_bits<31>", clamp_unsigned_bits<31>(q31(a)).raw(), isa_usat<31>(a), t);
		bad += differs("usat<16>.portable", p::usat<16>(a), isa_usat<16>(a), t);
		bad += differs("to_q16_sat", to_q16_sat(q31(a)).raw(), isa_ssat16_asr16(a), t);
		bad += differs("mul_q16", mul(q31(a), b16).raw(), isa_smulwb(a, b), t);
		bad += differs("mul_bottom", mul_bottom(q31(a), pair(b)).raw(), isa_smulwb(a, b), t);
		bad += differs("mul_top", mul_top(q31(a), pair(b)).raw(), isa_smulwt(a, b), t);
		bad += differs("mla_q16", mla(q30(c), q31(a), b16).raw(), isa_smlawb(c, a, b), t);
		bad += differs("dot", dot(pair(a), pair(b)).raw(), isa_smuad(a, b), t);
		bad += differs("dot_diff", dot_diff(pair(a), pair(b)).raw(), isa_smusd(a, b), t);
		bad += differs("dot_acc", dot_acc(q30(c), pair(a), pair(b)).raw(), isa_smlad(c, a, b), t);
		bad += differs("dot_wide_acc", dot_wide_acc(Q64<30>::from_raw(acc64), pair(a), pair(b)).raw(),
		               isa_smlald(acc64, a, b), t);
		bad += differs("q16x2.add_sat", add_sat(pair(a), pair(b)).raw(), isa_qadd16(ua, ub), t);
		bad += differs("q16x2.sub_sat", sub_sat(pair(a), pair(b)).raw(), isa_qsub16(ua, ub), t);
		bad += differs("q16x2.halving_add", halving_add(pair(a), pair(b)).raw(), isa_shadd16(ua, ub), t);
		bad += differs("mul_doubling_sat", mul_doubling_sat(q31(a), q31(b)).raw(), isa_vqdmulh(a, b), t);
		bad += differs("mul_doubling_r_sat", mul_doubling_r_sat(q31(a), q31(b)).raw(), isa_vqrdmulh(a, b), t);
	}
	return bad;
}

template <int F>
std::size_t float_mismatches_for() {
	std::size_t bad = 0;
	for (const std::uint32_t bits : float_inputs()) {
		const auto f = std::bit_cast<float>(bits);
		const auto r = std::bit_cast<std::int32_t>(bits);
		const Triple t{r, F, 0};
		bad += differs("from_float", from_float<F>(f).raw(), isa_vcvt_to_fixed<F>(f), t);
		bad += differs("float_to_raw.portable", detail::portable::float_to_raw<F>(f), isa_vcvt_to_fixed<F>(f), t);
		bad += differs("to_float", std::bit_cast<std::int32_t>(to_float(Q<F>::from_raw(r))),
		               std::bit_cast<std::int32_t>(isa_vcvt_to_float<F>(r)), t);
		bad += differs("raw_to_float.portable", std::bit_cast<std::int32_t>(detail::portable::raw_to_float<F>(r)),
		               std::bit_cast<std::int32_t>(isa_vcvt_to_float<F>(r)), t);
	}
	return bad;
}

template <int F = 1>
std::size_t float_mismatches() {
	std::size_t bad = float_mismatches_for<F>();
	if constexpr (F < 31) {
		bad += float_mismatches<F + 1>();
	}
	return bad;
}

} // namespace

// clang-format off
describe fixed_point_isa("fixed_point operations against the A9 instructions", ${
	it("every integer operation, and its portable twin, matches its instruction bit for bit", _ {
		expect(integer_mismatches()).to_equal(std::size_t{0});
	});
	it("the float conversions match VFP vcvt for every fractional width", _ {
		expect(float_mismatches()).to_equal(std::size_t{0});
	});
});
// clang-format on

CPPSPEC_SPEC(fixed_point_isa);
