#include "repeat.h"
#include "hid/display/oled.h"
#include "hid/display/oled_canvas/canvas.h"

#include "io/debug/log.h"

namespace deluge::gui::menu_item::sample {

static const Icon playModeIcons[] = {
    deluge::hid::display::OLED::playModeCutIcon,
    deluge::hid::display::OLED::playModeOnceIcon,
    deluge::hid::display::OLED::playModeLoopIcon,
    deluge::hid::display::OLED::playModeStretchIcon,
};

void Repeat::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
	auto& icon = playModeIcons[getValue()];
	startX += (width - icon.width) / 2;
	deluge::hid::display::OLED::main.drawIcon(icon, startX, startY);
}

} // namespace deluge::gui::menu_item::sample
