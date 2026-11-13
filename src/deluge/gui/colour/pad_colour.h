#pragma once

#include "gui/colour/rgb.h"
#include "storage/flash_storage.h"

#include <cstdint>

namespace deluge::gui {

/// @brief A pad-colour setting's value, as `FlashStorage::padColours` stores it.
enum PadColour : uint8_t {
	RED,
	GREEN,
	BLUE,
	YELLOW,
	CYAN,
	MAGENTA,
	AMBER,
	WHITE,
	PINK,
};

/// @brief How many values a pad-colour setting has; a stored byte at or past it is out of range.
inline constexpr int32_t kNumPadColours = 9;

/// @brief The colour a pad-colour value paints.
/// @param value A `PadColour`, below `kNumPadColours`.
RGB padColourValueRGB(uint8_t value);

/// @brief The colour pad-colour setting @p slot paints now.
RGB padColourRGB(FlashStorage::PadColourSlot slot);

} // namespace deluge::gui
