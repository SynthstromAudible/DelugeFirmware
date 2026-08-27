#pragma once
// Duplicated from util/lookuptables + util/waves.h so gfx stays a leaf; the audio copy is the original.
#include <cstdint>

namespace deluge::gfx::grid {

/// Verbatim copy of util::sineWaveSmall (src/deluge/util/lookuptables/lookuptables.cpp), a
/// 257-entry quarter-turn sine table used by the audio DSP code. Duplicated here (rather than
/// shared) so that src/gfx/** never depends on src/deluge/util/**; the audio copy remains the
/// single source of truth for DSP call sites (sine_osc.h, lfo.h, voice.cpp, etc.).
extern const int16_t kSineWaveSmall[257];

/// Verbatim port of util/waves.h's interpolateTableSigned.
[[gnu::always_inline]] inline int32_t interpolate_table_signed(uint32_t input, int32_t num_bits_in_input,
                                                               const int16_t* table,
                                                               int32_t num_bits_in_table_size = 8) {
	int32_t which_value = input >> (num_bits_in_input - num_bits_in_table_size);
	int32_t rshift_amount = num_bits_in_input - 16 - num_bits_in_table_size;
	uint32_t rshifted;
	if (rshift_amount >= 0) {
		rshifted = input >> rshift_amount;
	}
	else {
		rshifted = input << (-rshift_amount);
	}
	int32_t strength2 = rshifted & 65535;
	int32_t strength1 = 65536 - strength2;
	return (int32_t)table[which_value] * strength1 + (int32_t)table[which_value + 1] * strength2;
}

/// Verbatim port of util/waves.h's getSine, against the local kSineWaveSmall copy above.
[[gnu::always_inline]] inline int32_t sine_q15(uint32_t phase, uint8_t num_bits_in_input = 32) {
	return interpolate_table_signed(phase, num_bits_in_input, kSineWaveSmall, 8);
}

} // namespace deluge::gfx::grid
