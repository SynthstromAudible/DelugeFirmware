#pragma once
#include "model/mod_controllable/mod_controllable_audio.h"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/sync_level.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::delay {

class Analog final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentModControllable->delay.analog; }
	void writeCurrentValue() { soundEditor.currentModControllable->delay.analog = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {
			"Digital",
#if HAVE_OLED
			"Analog",
			NULL
#else
			"ANA"
#endif
		};
		return options;
	}
};

} // namespace menu_item::delay
