#pragma once

#include "gui/menu_item/submenu.h"
#include "model/output.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::midi::device_definition {

class DeviceDefinitionSubmenu : public Submenu {
public:
	using Submenu::Submenu;

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		Output* output = getCurrentOutput();
		return (output && output->type == OutputType::MIDI_OUT);
	}
};

} // namespace deluge::gui::menu_item::midi::device_definition
