#include "storage/audio/stream/convert.h"
#include "util/audio_format_helpers.h"
#include "util/fixedpoint.h"
#include <bit>
#include <cstdint>

namespace deluge::audio::stream {
int32_t convert_word(int32_t word, RawDataFormat format) {
	switch (format) {
	case RawDataFormat::FLOAT:
		return q31_from_float(std::bit_cast<float>(word));
	case RawDataFormat::ENDIANNESS_WRONG_32:
		return swapEndianness32(word);
	case RawDataFormat::ENDIANNESS_WRONG_16:
		return swapEndianness2x16(word);
	case RawDataFormat::UNSIGNED_8:
		return word ^ 0x80808080;
	case RawDataFormat::ENDIANNESS_WRONG_24:
		[[fallthrough]];
	case RawDataFormat::NATIVE:
		break;
	}
	return word;
}
} // namespace deluge::audio::stream
