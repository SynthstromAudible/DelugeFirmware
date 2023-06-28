#pragma once
#include "gui/menu_item/selection.h"
#include "processing/engines/audio_engine.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::monitor {
class Mode final : public Selection {
public:
	using Selection::Selection;

	void readCurrentValue() { soundEditor.currentValue = AudioEngine::inputMonitoringMode; }
	void writeCurrentValue() { AudioEngine::inputMonitoringMode = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {"Conditional", "On", "Off", NULL};
		return options;
	}
	int getNumOptions() { return NUM_INPUT_MONITORING_MODES; }
};
}
