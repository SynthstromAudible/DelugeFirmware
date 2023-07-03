#pragma once
#include "gui/menu_item/gate/mode.h"
#include "gui/menu_item/selection.h"
#include "off_time.h"
#include "gui/ui/sound_editor.h"
#include "mode.h"

extern menu_item::gate::OffTime gateOffTimeMenu;
extern menu_item::gate::Mode gateModeMenu;

namespace menu_item::gate {
class Selection final : public menu_item::Selection {
public:
	Selection(char const* newName = NULL) : menu_item::Selection(newName) {
#if HAVE_OLED
		basicTitle = "Gate outputs";
		static char const* options[] = {"Gate output 1", "Gate output 2",    "Gate output 3",
		                                "Gate output 4", "Minimum off-time", NULL};
#else
		static char const* options[] = {"Out1", "Out2", "Out3", "Out4", "OFFT", NULL};
#endif
		basicOptions = options;
	}
	void beginSession(MenuItem* navigatedBackwardFrom) {
		if (!navigatedBackwardFrom) {
			soundEditor.currentValue = 0;
		}
		else {
			soundEditor.currentValue = soundEditor.currentSourceIndex;
		}
		menu_item::Selection::beginSession(navigatedBackwardFrom);
	}

	MenuItem* selectButtonPress() {
		if (soundEditor.currentValue == NUM_GATE_CHANNELS) {
			return &gateOffTimeMenu;
		}
		else {
			soundEditor.currentSourceIndex = soundEditor.currentValue;
#if HAVE_OLED
			gate::mode_title[8] = '1' + soundEditor.currentValue;
#endif

			// TODO: this needs to be a "UpdateOptions" method on gate::Mode
			switch (soundEditor.currentValue) {
			case WHICH_GATE_OUTPUT_IS_CLOCK:
				mode_options[2] = "Clock";
				break;

			case WHICH_GATE_OUTPUT_IS_RUN:
				mode_options[2] = HAVE_OLED ? "\"Run\" signal" : "Run";
				break;

			default:
				mode_options[2] = NULL;
				break;
			}
			return &gateModeMenu;
		}
	}
};
} // namespace menu_item::gate
