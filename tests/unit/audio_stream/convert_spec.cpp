// tests/unit/audio_stream/convert_spec.cpp
#include "storage/audio/stream/convert.h"

#include "cppspec.hpp"

#include <bit>
#include <cstdint>

using namespace deluge::audio::stream;

// clang-format off
describe convert("convert_word", $ {
	it("NATIVE is identity", _ { expect(convert_word(0x11223344, RawDataFormat::NATIVE)).to_equal(0x11223344); });
	it("ENDIANNESS_WRONG_24 is a no-op (3-byte swap is done cluster-wide)", _ {
		expect(convert_word(0x11223344, RawDataFormat::ENDIANNESS_WRONG_24)).to_equal(0x11223344); });
	it("ENDIANNESS_WRONG_32 reverses all 4 bytes", _ {
		expect(convert_word(0x01020304, RawDataFormat::ENDIANNESS_WRONG_32)).to_equal(0x04030201); });
	it("ENDIANNESS_WRONG_16 swaps within each half-word", _ {
		expect(convert_word(0x01020304, RawDataFormat::ENDIANNESS_WRONG_16)).to_equal(0x02010403); });
	it("UNSIGNED_8 flips the MSB of every byte", _ {
		expect(convert_word(0x00112233, RawDataFormat::UNSIGNED_8)).to_equal(0x8091A2B3); });
	it("FLOAT 0.5f maps to Q31 0x40000000", _ {
		expect(convert_word(std::bit_cast<int32_t>(0.5f), RawDataFormat::FLOAT)).to_equal(0x40000000); });
});

CPPSPEC_SPEC(convert)
