#pragma once
// Duplicated from util/lookuptables + util/waves.h so gfx stays a leaf; the audio copy is the original.
#include <cstdint>

namespace deluge::gfx::grid {

/// @brief 257-entry quarter-turn sine table (Q15), mirroring the audio DSP's `sineWaveSmall`.
///
/// @note Duplicated here, rather than shared, so that `src/gfx/**` never depends on
///       `src/deluge/util/**`. The DSP copy remains the single source of truth for DSP call sites
///       (sine_osc.h, lfo.h, voice.cpp, etc.); keep the two in step.
extern const int16_t kSineWaveSmall[257];

/// @brief Bilinear interpolation into a signed lookup table, weighted by the low bits of @p input.
///
/// @param input                Phase/position value; must not have bits set above
///                              @p num_bits_in_input.
/// @param num_bits_in_input    Number of significant bits in @p input.
/// @param table                The signed lookup table to interpolate into.
/// @param num_bits_in_table_size Number of bits of @p input that index @p table.
/// @return The interpolated table value.
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

/// @brief Sine of @p phase (Q15), interpolated against kSineWaveSmall above.
///
/// @param phase             Phase value; must not have bits set above @p num_bits_in_input.
/// @param num_bits_in_input Number of significant bits in @p phase.
/// @return The interpolated sine value, Q15.
[[gnu::always_inline]] inline int32_t sine_q15(uint32_t phase, uint8_t num_bits_in_input = 32) {
	return interpolate_table_signed(phase, num_bits_in_input, kSineWaveSmall, 8);
}

} // namespace deluge::gfx::grid
