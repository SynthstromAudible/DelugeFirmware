#pragma once
#include "io/midi/midi_device_manager.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::midi {
class InputDifferentiation final : public Selection {
public:
  using Selection::Selection;
	void readCurrentValue() { soundEditor.currentValue = MIDIDeviceManager::differentiatingInputsByDevice; }
	void writeCurrentValue() { MIDIDeviceManager::differentiatingInputsByDevice = soundEditor.currentValue; }
};
}
