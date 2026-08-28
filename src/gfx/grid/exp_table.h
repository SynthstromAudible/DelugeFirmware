#pragma once
// Duplicated from util/lookuptables + util/functions so gfx stays a leaf; the audio copy is the
// original. Same arrangement (and the same drift hazard) as gfx/grid/sine_table.h -- see
// src/gfx/README.md.
#include <cstdint>

namespace deluge::gfx::grid {

/// Verbatim copy of `util::expTableSmall` (src/deluge/util/lookuptables/lookuptables.cpp:127), a
/// 257-entry table of `32768 * 2^(i/256)`. `PadLEDs::renderZoom` (pad_leds.cpp:1200) reads it to
/// turn the transition progress into "how many times bigger than native is the zoomed-in image".
extern const uint16_t kExpTableSmall[257];

/// Verbatim port of `interpolateTable` (src/deluge/util/functions.cpp:487) -- the *unsigned*
/// sibling of sine_table.h's `interpolate_table_signed`. Note the 15-bit (not 16-bit) interpolation
/// weights: the result is on a `table_value * 32768` scale, so `kExpTableSmall` yields at most
/// 65535 * 32768, which still fits an int32_t. `input` must not have bits set above
/// `num_bits_in_input`.
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

/// Verbatim port of `increaseMagnitude` (src/deluge/util/functions.h:362).
[[gnu::always_inline]] inline int32_t increase_magnitude(int32_t number, int32_t magnitude) {
	if (magnitude >= 0) {
		return number << magnitude;
	}
	return number >> (-magnitude);
}

} // namespace deluge::gfx::grid
