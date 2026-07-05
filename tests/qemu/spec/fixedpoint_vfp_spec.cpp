// Parity check: the portable float<->fixed conversion helpers in FixedPoint must be *bit-exact*
// to the ARM VFP fixed-point VCVT instructions they stand in for off-target. If they drift, the
// x86 host-sim (which always takes the portable path) diverges from the firmware (which uses VFP),
// and the golden WAV captures become arch-dependent.
//
// This spec runs under qemu-arm, so both sides are available in one binary: the VFP instruction
// (inline asm, the ground truth) and the portable helper (FixedPoint<F>::{float_to_raw,raw_to_float,
// raw_to_double}). It sweeps every fractional-bit width and a large set of inputs — every edge case
// (zero, +-inf, NaN, denormals, the saturation boundaries) plus a deterministic pseudo-random walk
// across the whole 32-bit pattern space — and asserts zero mismatches.

#include "util/fixedpoint.h"
#include <bit>
#include <cppspec.hpp>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

// --- VFP ground truth (fractional bits is a compile-time immediate for the #fbits operand) ---
template <int F>
int32_t vfp_f32_to_raw(float v) {
	asm("vcvt.s32.f32 %0, %1, %2" : "=t"(v) : "t"(v), "I"(F));
	return std::bit_cast<int32_t>(v);
}
template <int F>
int32_t vfp_f64_to_raw(double v) {
	int64_t o = std::bit_cast<int64_t>(v);
	asm("vcvt.s32.f64 %P0, %P0, %1" : "+w"(o) : "I"(F));
	return static_cast<int32_t>(o);
}
template <int F>
float vfp_raw_to_f32(int32_t r) {
	int32_t o = r;
	asm("vcvt.f32.s32 %0, %1, %2" : "=t"(o) : "t"(o), "I"(F));
	return std::bit_cast<float>(o);
}
template <int F>
double vfp_raw_to_f64(int32_t r) {
	double o = std::bit_cast<double>(static_cast<int64_t>(r));
	asm("vcvt.f64.s32 %P0, %P0, %1" : "+w"(o) : "I"(F));
	return o;
}

const std::vector<uint32_t>& inputs() {
	static const std::vector<uint32_t> v = [] {
		std::vector<uint32_t> out;
		auto add = [&](float f) { out.push_back(std::bit_cast<uint32_t>(f)); };
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
			add(f);
		}
		for (uint32_t bits : {0x7f800000u, 0xff800000u, 0x7fc00000u, 0x7f800001u, 0x00000001u, 0x80000001u}) {
			out.push_back(bits); // +-inf, qNaN, sNaN, +-smallest denormal
		}
		uint32_t x = 0x12345678u;
		for (int i = 0; i < 60000; ++i) {
			x = x * 1664525u + 1013904223u; // full-range LCG walk over the bit space
			out.push_back(x);
		}
		return out;
	}();
	return v;
}

// Accumulate mismatches for one fractional-bit width, printing the first few for triage.
uint64_t g_reported = 0;
template <int F>
uint64_t mismatchesForFbits() {
	uint64_t fails = 0;
	auto check = [&](const char* what, uint32_t in, uint64_t got, uint64_t exp) {
		if (got != exp && ++fails && g_reported < 30) {
			++g_reported;
			printf("  mismatch %s F=%d in=0x%08x portable=0x%llx vfp=0x%llx\n", what, F, in,
			       static_cast<unsigned long long>(got), static_cast<unsigned long long>(exp));
		}
	};
	for (uint32_t bits : inputs()) {
		float f = std::bit_cast<float>(bits);
		double d = static_cast<double>(f);
		int32_t r = std::bit_cast<int32_t>(bits);
		// The portable helpers (the path the x86 host-sim always takes) vs the VFP ground truth.
		check("f32->raw", bits, static_cast<uint32_t>(FixedPoint<F>::float_to_raw(f)),
		      static_cast<uint32_t>(vfp_f32_to_raw<F>(f)));
		check("f64->raw", bits, static_cast<uint32_t>(FixedPoint<F>::float_to_raw(d)),
		      static_cast<uint32_t>(vfp_f64_to_raw<F>(d)));
		check("raw->f32", bits, std::bit_cast<uint32_t>(FixedPoint<F>::raw_to_float(r)),
		      std::bit_cast<uint32_t>(vfp_raw_to_f32<F>(r)));
		check("raw->f64", bits, std::bit_cast<uint64_t>(FixedPoint<F>::raw_to_double(r)),
		      std::bit_cast<uint64_t>(vfp_raw_to_f64<F>(r)));
		// The shipping conversions themselves (the ctor/operator asm on ARM) vs the VFP ground truth,
		// so a mis-wired constraint in the asm is caught too. bit_cast injects an arbitrary raw value.
		check("ctor.f32", bits, static_cast<uint32_t>(FixedPoint<F>{f}.raw()),
		      static_cast<uint32_t>(vfp_f32_to_raw<F>(f)));
		check("ctor.f64", bits, static_cast<uint32_t>(FixedPoint<F>{d}.raw()),
		      static_cast<uint32_t>(vfp_f64_to_raw<F>(d)));
		check("op.f32", bits, std::bit_cast<uint32_t>(static_cast<float>(std::bit_cast<FixedPoint<F>>(r))),
		      std::bit_cast<uint32_t>(vfp_raw_to_f32<F>(r)));
		check("op.f64", bits, std::bit_cast<uint64_t>(static_cast<double>(std::bit_cast<FixedPoint<F>>(r))),
		      std::bit_cast<uint64_t>(vfp_raw_to_f64<F>(r)));
	}
	return fails;
}

// Sum mismatches across every valid fractional-bit width [1, 31].
template <int F = 1>
uint64_t mismatchesAllFbits() {
	uint64_t fails = mismatchesForFbits<F>();
	if constexpr (F < 31) {
		fails += mismatchesAllFbits<F + 1>();
	}
	return fails;
}

} // namespace

// clang-format off
describe fixedpoint_vfp("FixedPoint VFP parity", ${
	it("portable float<->fixed helpers are bit-exact to VFP vcvt across all fractional bits", _ {
		expect(mismatchesAllFbits()).to_equal(static_cast<uint64_t>(0));
	});
});

CPPSPEC_SPEC(fixedpoint_vfp);
