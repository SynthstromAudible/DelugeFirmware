#pragma once

#include "gui/menu_item/toggle.h"
#include "gui/ui/load/load_midi_device_definition_ui.h"
#include "gui/ui/sound_editor.h"
#include "model/instrument/midi_instrument.h"
#include "model/output.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::midi::device_definition {

class Linked : public Toggle {
public:
	using Toggle::Toggle;

	void readCurrentValue() override {
		MIDIInstrument* midiInstrument = (MIDIInstrument*)getCurrentOutput();
		this->setValue(!midiInstrument->deviceDefinitionFileName.isEmpty());
	}
	void writeCurrentValue() override {
		t = this->getValue();

		// if you want to link a definition file, open the load definition file UI
		if (t) {
			openUI(&loadMidiDeviceDefinitionUI);
		}
		// if you want to unlink a definition file, just clear the definition file name
		else {
			MIDIInstrument* midiInstrument = (MIDIInstrument*)getCurrentOutput();
			midiInstrument->deviceDefinitionFileName.clear();
		}
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) {
		Output* output = getCurrentOutput();
		return ((output != nullptr) && output->type == OutputType::MIDI_OUT);
	}

	void renderSubmenuItemTypeForOled(int32_t yPixel) final {
		deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

		int32_t startX = getSubmenuItemTypeRenderIconStart();

		if (getToggleValue()) {
			image.drawGraphicMultiLine(deluge::hid::display::OLED::checkedBoxIcon, startX, yPixel,
			                           kSubmenuIconSpacingX);

			MIDIInstrument* midiInstrument = (MIDIInstrument*)getCurrentOutput();

			char const* fullPath = midiInstrument->deviceDefinitionFileName.get();

			// locate last occurence of "/" in string
			char* fileName = strrchr((char*)fullPath, '/');

			image.drawString(++fileName, kTextSpacingX, yPixel + kTextSpacingY, kTextSpacingX, kTextSpacingY);
		}
		else {
			image.drawGraphicMultiLine(deluge::hid::display::OLED::uncheckedBoxIcon, startX, yPixel,
			                           kSubmenuIconSpacingX);
		}
	}

	bool t;
};

} // namespace deluge::gui::menu_item::midi::device_definition
