#pragma once
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::shortcuts {
class Version final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() { soundEditor.currentValue = soundEditor.shortcutsVersion; }
	void writeCurrentValue() { soundEditor.setShortcutsVersion(soundEditor.currentValue); }
	char const** getOptions() {
		static char const* options[] = {
#if HAVE_OLED
			"1.0",
			"3.0",
			NULL
#else
			"  1.0",
			"  3.0"
#endif
		};
		return options;
	}
	int getNumOptions() {
		return NUM_SHORTCUTS_VERSIONS;
	}
};
} // namespace menu_item::shortcuts
