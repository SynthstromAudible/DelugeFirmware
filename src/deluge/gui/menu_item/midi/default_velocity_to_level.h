#pragma once
#include "io/midi/midi_device.h"
#include "gui/menu_item/integer.h"
#include "model/song/song.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::midi {
class DefaultVelocityToLevel final : public IntegerWithOff {
public:
	DefaultVelocityToLevel(char const* newName = NULL) : IntegerWithOff(newName) {}
	int getMaxValue() const { return 50; }
	void readCurrentValue() {
		soundEditor.currentValue =
		    ((int64_t)soundEditor.currentMIDIDevice->defaultVelocityToLevel * 50 + 536870912) >> 30;
	}
	void writeCurrentValue() {
		soundEditor.currentMIDIDevice->defaultVelocityToLevel = soundEditor.currentValue * 21474836;
		currentSong->grabVelocityToLevelFromMIDIDeviceAndSetupPatchingForEverything(soundEditor.currentMIDIDevice);
		MIDIDeviceManager::anyChangesToSave = true;
	}
};
}
