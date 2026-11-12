#include "gui/colour/pad_colour.h"

#include "gui/colour/colour.h"

#include <utility>

namespace deluge::gui {

RGB padColourValueRGB(uint8_t value) {
	switch (static_cast<PadColour>(value)) {
	case RED:
		return colours::red;
	case GREEN:
		return colours::enabled;
	case BLUE:
		return colours::blue;
	case YELLOW:
		return colours::yellow_orange;
	case CYAN:
		return colours::cyan;
	case MAGENTA:
		return colours::magenta;
	case AMBER:
		return colours::amber;
	case WHITE:
		return colours::white;
	case PINK:
		return colours::pink;
	}
	std::unreachable(); // `readSettings` keeps every stored value below `kNumPadColours`
}

RGB padColourRGB(FlashStorage::PadColourSlot slot) {
	return padColourValueRGB(FlashStorage::padColours[slot]);
}

} // namespace deluge::gui
