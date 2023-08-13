#pragma once

#include "gui/menu_item/submenu.h"
#include "io/midi/midi_device.h"
namespace deluge::gui::menu_item::midi {
	struct Device : Submenu<2>{
		using Submenu<2>::Submenu;
		[[nodiscard]] std::string_view getTitle() const override {
			return soundEditor.currentMIDIDevice->getDisplayName();
		}
	};
}
