// Parity check: the argon SIMD conversion primitives used by convert.h must be bit-exact to their
// scalar references on REAL ARM NEON (as opposed to SIMDe's software emulation, which diverges for
// the FLOAT case -- see convert.h's doc comment on convert_range_simd).
//
// This spec runs under qemu-arm with -mfpu=neon (cortex-a9), so argon compiles against a real
// <arm_neon.h> backend (no SIMDe, no compat shim -- see tests/unit-arm/spec/CMakeLists.txt). Two groups:
//
//  - FLOAT (the decision gate): Argon<float>::ConvertTo<int32_t,31>() (vcvtq_n_s32_f32) vs the scalar
//    q31_from_float() from util/fixedpoint.h. Under this arm-linux cross build __arm__ is defined, so
//    q31_from_float() itself compiles to the VFP `vcvt.s32.f32 #31` instruction (see fixedpoint.h) --
//    i.e. this is a real-hardware NEON-vs-VFP comparison, executed via qemu's instruction-accurate
//    emulation of both instruction families, not a software model.
//
//  - Integer ops (the dual-arch net for the rest of convert_range_simd / convert_24bit_range_simd):
//    Reverse32bit/Reverse16bit vs swapEndianness32/swapEndianness2x16 (util/audio_format_helpers.h),
//    the UNSIGNED_8 XOR-0x80 path, and the ENDIANNESS_WRONG_24 3-byte channel swap
//    (LoadInterleaved<3>/store_interleaved).
//
// Every group sweeps hand-picked edge cases plus a deterministic pseudo-random walk and asserts zero
// mismatches, mirroring fixedpoint_vfp_spec.cpp's style.

#include <argon.hpp>
#include <argon/helpers/size.hpp>
#include <bit>
#include <cppspec.hpp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "util/audio_format_helpers.h" // swapEndianness32, swapEndianness2x16
#include "util/fixedpoint.h"           // q31_from_float, q31_t

namespace {

// --- shared mismatch-accumulation helper (mirrors fixedpoint_vfp_spec.cpp) ---
uint64_t g_reported = 0;
uint64_t g_fails = 0;

template <class T>
void check(const char* what, uint32_t case_index, T got, T exp) {
	if (got != exp) {
		++g_fails;
		if (g_reported < 40) {
			++g_reported;
			if constexpr (std::is_same_v<T, uint32_t>) {
				printf("  mismatch %s case=%u got=0x%08x exp=0x%08x\n", what, case_index, got, exp);
			}
			else {
				printf("  mismatch %s case=%u got=0x%02x exp=0x%02x\n", what, case_index, static_cast<unsigned>(got),
				       static_cast<unsigned>(exp));
			}
		}
	}
}

// =====================================================================================
// FLOAT: Argon<float>::ConvertTo<int32_t, 31>() vs scalar q31_from_float() -- the crux.
// =====================================================================================

const std::vector<float>& float_inputs() {
	static const std::vector<float> v = [] {
		std::vector<float> out = {
		    0.0f,       -0.0f,       0.5f,       -0.5f,
		    1.0f,  // saturation boundary -> 0x7FFFFFFF
		    -1.0f, // -> 0x80000000
		    0.9999999f, -0.9999999f, 1.0000001f, -1.0000001f, 1.5f, -1.5f, 2.0f, -2.0f, 100.0f, -100.0f,
		};
		// Denormals (just above/below zero) and the NaN/inf family.
		for (uint32_t bits : {0x00000001u, // smallest positive denormal
		                      0x80000001u, // smallest negative denormal
		                      0x007fffffu, // largest denormal
		                      0x807fffffu, // largest negative denormal
		                      0x7f800000u, // +inf
		                      0xff800000u, // -inf
		                      0x7fc00000u, // quiet NaN
		                      0xff800001u, // signalling-ish NaN pattern (negative)
		                      0x7f800001u}) {
			out.push_back(std::bit_cast<float>(bits));
		}
		// Deterministic pseudo-random sweep over the full 32-bit pattern space (same LCG as
		// fixedpoint_vfp_spec.cpp), skipping non-finite results is NOT done -- NaN/inf patterns are
		// exactly what we want exercised too.
		uint32_t x = 0x9e3779b9u;
		for (int i = 0; i < 20000; ++i) {
			x = x * 1664525u + 1013904223u;
			out.push_back(std::bit_cast<float>(x));
		}
		return out;
	}();
	return v;
}

uint64_t checkFloatConvertParity() {
	g_fails = 0;
	g_reported = 0;
	const auto& inputs = float_inputs();
	constexpr size_t lanes = Argon<float>::lanes; // 4

	size_t i = 0;
	for (; i + lanes <= inputs.size(); i += lanes) {
		float buf[lanes];
		std::memcpy(buf, &inputs[i], sizeof(buf));
		auto converted = Argon<float>::Load(buf).ConvertTo<int32_t, 31>();
		auto got = converted.to_array();
		for (size_t lane = 0; lane < lanes; ++lane) {
			q31_t exp = q31_from_float(buf[lane]);
			check<uint32_t>("float->q31 (SIMD)", static_cast<uint32_t>(i + lane), std::bit_cast<uint32_t>(got[lane]),
			                std::bit_cast<uint32_t>(exp));
		}
	}
	// Scalar tail (inputs.size() not necessarily a multiple of `lanes`): pad with the last value so
	// every remaining case still gets exercised through the SIMD path too.
	if (i < inputs.size()) {
		float buf[lanes];
		size_t remaining = inputs.size() - i;
		for (size_t lane = 0; lane < lanes; ++lane) {
			buf[lane] = inputs[i + (lane < remaining ? lane : remaining - 1)];
		}
		auto converted = Argon<float>::Load(buf).ConvertTo<int32_t, 31>();
		auto got = converted.to_array();
		for (size_t lane = 0; lane < remaining; ++lane) {
			q31_t exp = q31_from_float(buf[lane]);
			check<uint32_t>("float->q31 (SIMD tail)", static_cast<uint32_t>(i + lane),
			                std::bit_cast<uint32_t>(got[lane]), std::bit_cast<uint32_t>(exp));
		}
	}
	return g_fails;
}

// =====================================================================================
// INTEGER: Reverse32bit / Reverse16bit / XOR-0x80 / 3-byte channel swap vs scalar refs.
// =====================================================================================

const std::vector<uint8_t>& byte_inputs() {
	static const std::vector<uint8_t> v = [] {
		std::vector<uint8_t> out;
		// A run covering every byte value, several times over with different alignments, plus a
		// deterministic pseudo-random sweep -- enough bytes to exercise several full 16-byte (and
		// 48-byte, for the 3-byte interleave) vector groups.
		for (int rep = 0; rep < 4; ++rep) {
			for (int b = 0; b < 256; ++b) {
				out.push_back(static_cast<uint8_t>(b));
			}
		}
		uint32_t x = 0xcafef00du;
		for (int i = 0; i < 8192; ++i) {
			x = x * 1664525u + 1013904223u;
			out.push_back(static_cast<uint8_t>(x >> 24));
		}
		// Round the length down to a multiple of 48 so both the 16-byte (XOR/rev) and 48-byte
		// (3-byte interleave) vector grids cover the whole buffer with no scalar tail to worry about.
		out.resize((out.size() / 48) * 48);
		return out;
	}();
	return v;
}

uint64_t checkReverse32Parity() {
	g_fails = 0;
	g_reported = 0;
	const auto& in = byte_inputs();
	constexpr size_t lanes = Argon<uint8_t>::lanes; // 16
	for (size_t i = 0; i + lanes <= in.size(); i += lanes) {
		uint8_t buf[lanes];
		std::memcpy(buf, &in[i], lanes);
		auto got = Argon<uint8_t>::Load(buf).Reverse32bit().to_array();

		uint8_t exp[lanes];
		for (size_t w = 0; w < lanes / 4; ++w) {
			uint32_t word;
			std::memcpy(&word, &buf[w * 4], 4);
			uint32_t swapped = swapEndianness32(word);
			std::memcpy(&exp[w * 4], &swapped, 4);
		}
		for (size_t lane = 0; lane < lanes; ++lane) {
			check<uint8_t>("Reverse32bit", static_cast<uint32_t>(i + lane), got[lane], exp[lane]);
		}
	}
	return g_fails;
}

uint64_t checkReverse16Parity() {
	g_fails = 0;
	g_reported = 0;
	const auto& in = byte_inputs();
	constexpr size_t lanes = Argon<uint8_t>::lanes; // 16
	for (size_t i = 0; i + lanes <= in.size(); i += lanes) {
		uint8_t buf[lanes];
		std::memcpy(buf, &in[i], lanes);
		auto got = Argon<uint8_t>::Load(buf).Reverse16bit().to_array();

		uint8_t exp[lanes];
		for (size_t w = 0; w < lanes / 4; ++w) {
			uint32_t word;
			std::memcpy(&word, &buf[w * 4], 4);
			uint32_t swapped = swapEndianness2x16(word);
			std::memcpy(&exp[w * 4], &swapped, 4);
		}
		for (size_t lane = 0; lane < lanes; ++lane) {
			check<uint8_t>("Reverse16bit", static_cast<uint32_t>(i + lane), got[lane], exp[lane]);
		}
	}
	return g_fails;
}

uint64_t checkXor80Parity() {
	g_fails = 0;
	g_reported = 0;
	const auto& in = byte_inputs();
	constexpr size_t lanes = Argon<uint8_t>::lanes; // 16
	Argon<uint8_t> const xor_key{uint8_t{0x80}};
	for (size_t i = 0; i + lanes <= in.size(); i += lanes) {
		uint8_t buf[lanes];
		std::memcpy(buf, &in[i], lanes);
		auto got = (Argon<uint8_t>::Load(buf) ^ xor_key).to_array();
		for (size_t lane = 0; lane < lanes; ++lane) {
			uint8_t exp = static_cast<uint8_t>(buf[lane] ^ 0x80);
			check<uint8_t>("XOR 0x80", static_cast<uint32_t>(i + lane), got[lane], exp);
		}
	}
	return g_fails;
}

// LoadInterleaved<3> + store_interleaved(p, c2, c1, c0): the vectorized byte0<->byte2 swap within
// every 3-byte group, exactly mirroring convert_24bit_range_simd in convert.h.
uint64_t checkInterleave3SwapParity() {
	g_fails = 0;
	g_reported = 0;
	const auto& in = byte_inputs();
	constexpr size_t lanes = Argon<uint8_t>::lanes; // 16
	constexpr size_t group_bytes = lanes * 3;       // 48
	for (size_t i = 0; i + group_bytes <= in.size(); i += group_bytes) {
		uint8_t buf[group_bytes];
		std::memcpy(buf, &in[i], group_bytes);

		uint8_t got[group_bytes];
		std::memcpy(got, buf, group_bytes);
		{
			auto* p = got;
			auto [c0, c1, c2] = Argon<uint8_t>::LoadInterleaved<3>(p);
			argon::store_interleaved(p, c2, c1, c0);
		}

		uint8_t exp[group_bytes];
		std::memcpy(exp, buf, group_bytes);
		for (size_t g = 0; g < lanes; ++g) {
			uint8_t tmp = exp[g * 3];
			exp[g * 3] = exp[g * 3 + 2];
			exp[g * 3 + 2] = tmp;
		}

		for (size_t b = 0; b < group_bytes; ++b) {
			check<uint8_t>("3-byte swap", static_cast<uint32_t>(i + b), got[b], exp[b]);
		}
	}
	return g_fails;
}

} // namespace

// clang-format off
describe convert_simd_neon_parity("argon SIMD conversion ops vs scalar references on real qemu-arm NEON", ${
	it("FLOAT: Argon<float>::ConvertTo<int32_t,31>() bit-matches scalar q31_from_float() [DECISION GATE]", _ {
		expect(checkFloatConvertParity()).to_equal(static_cast<uint64_t>(0));
	});

	it("Reverse32bit() bit-matches scalar swapEndianness32() per 4-byte word", _ {
		expect(checkReverse32Parity()).to_equal(static_cast<uint64_t>(0));
	});

	it("Reverse16bit() bit-matches scalar swapEndianness2x16() per 4-byte word", _ {
		expect(checkReverse16Parity()).to_equal(static_cast<uint64_t>(0));
	});

	it("XOR 0x80 bit-matches the scalar per-byte XOR 0x80", _ {
		expect(checkXor80Parity()).to_equal(static_cast<uint64_t>(0));
	});

	it("LoadInterleaved<3>+store_interleaved channel swap bit-matches the scalar byte0<->byte2 swap", _ {
		expect(checkInterleave3SwapParity()).to_equal(static_cast<uint64_t>(0));
	});
});

CPPSPEC_SPEC(convert_simd_neon_parity);
