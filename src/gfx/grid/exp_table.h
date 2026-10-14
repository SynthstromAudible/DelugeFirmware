#pragma once
// Duplicated from util/lookuptables + util/functions so gfx stays a leaf; the audio copy is the
// original. Same arrangement (and the same drift hazard) as gfx/grid/sine_table.h -- see
// src/gfx/README.md.
#include <cstdint>

namespace deluge::gfx::grid {

/// @brief 257-entry table of `32768 * 2^(i/256)`, mirroring the audio DSP's `expTableSmall`.
///
/// @note Legacy `PadLEDs::renderZoom` reads this to turn the transition progress into "how many
///       times bigger than native is the zoomed-in image".
extern const uint16_t kExpTableSmall[257];

/// @brief Bilinear interpolation into an unsigned lookup table -- the *unsigned* sibling of
///        sine_table.h's interpolate_table_signed().
///
/// @note The interpolation weights are 15-bit (not 16-bit): the result is on a
///       `table_value * 32768` scale, so kExpTableSmall yields at most `65535 * 32768`, which
///       still fits an int32_t.
///
/// @param input                Phase/position value; must not have bits set above
///                              @p num_bits_in_input.
/// @param num_bits_in_input    Number of significant bits in @p input.
/// @param table                The unsigned lookup table to interpolate into.
/// @param num_bits_in_table_size Number of bits of @p input that index @p table.
/// @return The interpolated table value.
[[gnu::always_inline]] inline int32_t interpolate_table(uint32_t input, int32_t num_bits_in_input,
                                                        const uint16_t* table, int32_t num_bits_in_table_size = 8) {
	int32_t which_value = input >> (num_bits_in_input - num_bits_in_table_size);
	int32_t value1 = table[which_value];
	int32_t value2 = table[which_value + 1];

	int32_t rshift_amount = num_bits_in_input - 15 - num_bits_in_table_size;
	uint32_t rshifted;
	if (rshift_amount >= 0) {
		rshifted = input >> rshift_amount;
	}
	else {
		rshifted = input << (-rshift_amount);
	}

	int32_t strength2 = rshifted & 32767;
	int32_t strength1 = 32768 - strength2;
	return value1 * strength1 + value2 * strength2;
}

/// @brief Shift @p number left by @p magnitude, or right if @p magnitude is negative.
///
/// @note Ports legacy `increaseMagnitude`.
///
/// @param number    Value to shift.
/// @param magnitude Shift amount; positive shifts left, negative shifts right.
/// @return @p number shifted by @p magnitude.
[[gnu::always_inline]] inline int32_t increase_magnitude(int32_t number, int32_t magnitude) {
	if (magnitude >= 0) {
		return number << magnitude;
	}
	return number >> (-magnitude);
}

} // namespace deluge::gfx::grid
