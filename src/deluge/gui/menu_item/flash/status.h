#pragma once
#include "storage/flash_storage.h"
#include "hid/led/pad_leds.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::flash {
class Status final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() { soundEditor.currentValue = PadLEDs::flashCursor; }
	void writeCurrentValue() {
		if (PadLEDs::flashCursor == FLASH_CURSOR_SLOW) {
			PadLEDs::clearTickSquares();
		}
		PadLEDs::flashCursor = soundEditor.currentValue;
	}
	char const** getOptions() {
		static char const* options[] = {"Fast", "Off", "Slow", NULL};
		return options;
	}
	int getNumOptions() { return 3; }
};
} // namespace menu_item::flash
