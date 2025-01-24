#pragma once

#include "gui/menu_item/submenu.h"

namespace deluge::gui::menu_item::midi::sound_drums {

class OutputMidiSubmenu : public Submenu {
public:
	using Submenu::Submenu;

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return soundEditor.editingKit() && !soundEditor.editingNonAudioDrumRow();
	}
};
} // namespace deluge::gui::menu_item::midi::sound_drums
