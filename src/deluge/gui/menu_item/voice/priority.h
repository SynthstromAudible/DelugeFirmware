#pragma once
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::voice {
class Priority final : public Selection {
public:
	Priority(char const* newName = NULL) : Selection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = *soundEditor.currentPriority; }
	void writeCurrentValue() { *soundEditor.currentPriority = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {"LOW", "MEDIUM", "HIGH", NULL};
		return options;
	}
	int getNumOptions() { return NUM_PRIORITY_OPTIONS; }
};
} // namespace menu_item::voice
