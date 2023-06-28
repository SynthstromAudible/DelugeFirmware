#pragma once
#include "storage/flash_storage.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::sample::browser_preview {
class Mode final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() { soundEditor.currentValue = FlashStorage::sampleBrowserPreviewMode; }
	void writeCurrentValue() { FlashStorage::sampleBrowserPreviewMode = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {"Off", "Conditional", "On", NULL};
		return options;
	}
	int getNumOptions() { return 3; }
};
}
