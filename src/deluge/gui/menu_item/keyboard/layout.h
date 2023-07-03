#pragma once
#include "storage/flash_storage.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::keyboard {
class Layout final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() { soundEditor.currentValue = FlashStorage::keyboardLayout; }
	void writeCurrentValue() { FlashStorage::keyboardLayout = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {
			"QWERTY",
			"AZERTY",
#if HAVE_OLED
			"QWERTZ",
			NULL
#else
			"QRTZ"
#endif
		};
		return options;
	}
	int getNumOptions() {
		return NUM_KEYBOARD_LAYOUTS;
	}
};
} // namespace menu_item::keyboard
