#pragma once
#include "model/mod_controllable/mod_controllable_audio.h"
#include "gui/menu_item/selection.h"
#include "hid/display/numeric_driver.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::mod_fx {

class Type : public Selection {
public:
	using Selection::Selection;

	void readCurrentValue() override { soundEditor.currentValue = soundEditor.currentModControllable->modFXType; }
	void writeCurrentValue() override {
		if (!soundEditor.currentModControllable->setModFXType(soundEditor.currentValue)) {
			numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
		}
	}

	char const** getOptions() override {
		static char const* options[] = {"OFF", "FLANGER", "CHORUS", "PHASER", "STEREO CHORUS", NULL};
		return options;
	}

	int getNumOptions() override { return NUM_MOD_FX_TYPES; }
};
} // namespace menu_item::mod_fx
