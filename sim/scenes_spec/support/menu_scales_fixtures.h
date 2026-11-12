#pragma once

/// @file
/// @brief The fixtures of the two preset-scale checklists: a guard over both bitsets and the asymmetric bitset every
///        example starts from. The spec and the menu oracle freezer share them.

#include "model/scale/preset_scales.h"
#include "model/song/song.h"
#include "storage/flash_storage.h"

#include <bitset>
#include <cstddef>
#include <string>

namespace deluge::scenes::spec::scales {

/// @brief Saves and restores both bitsets, so no example leaks into the next.
struct ScalesGuard {
	std::bitset<NUM_PRESET_SCALES> saved_flash = FlashStorage::defaultDisabledPresetScales;
	std::bitset<NUM_PRESET_SCALES> saved_song{};
	Song* song = currentSong;

	ScalesGuard() {
		if (song != nullptr) {
			saved_song = song->project.scale.disabled_preset_scales;
		}
	}
	ScalesGuard(const ScalesGuard&) = delete;
	ScalesGuard& operator=(const ScalesGuard&) = delete;
	~ScalesGuard() {
		FlashStorage::defaultDisabledPresetScales = saved_flash;
		if (song != nullptr) {
			song->project.scale.disabled_preset_scales = saved_song;
		}
	}
};

/// @brief A repeatable, asymmetric starting bitset -- not all-on and not all-off, so a renderer that drew one box for
///        every row would be visible.
inline std::bitset<NUM_PRESET_SCALES> sentinel_bits() {
	std::bitset<NUM_PRESET_SCALES> bits;
	for (size_t i = 0; i < NUM_PRESET_SCALES; i++) {
		bits[i] = (i % 3) == 1;
	}
	return bits;
}

/// @brief @p bits as the lowercase hex of its value, the way the reference rows hold a bitset.
inline std::string bits_hex(const std::bitset<NUM_PRESET_SCALES>& bits) {
	static constexpr char kDigits[] = "0123456789abcdef";
	std::string out;
	for (size_t nibble = (NUM_PRESET_SCALES + 3) / 4; nibble > 0; nibble--) {
		unsigned digit = 0;
		for (size_t b = 0; b < 4; b++) {
			const size_t index = (nibble - 1) * 4 + b;
			if (index < NUM_PRESET_SCALES && bits[index]) {
				digit |= 1U << b;
			}
		}
		out += kDigits[digit];
	}
	return out;
}

} // namespace deluge::scenes::spec::scales
