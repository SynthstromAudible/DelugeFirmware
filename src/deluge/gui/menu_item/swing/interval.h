#pragma once
#include "gui/menu_item/sync_level.h"
#include "model/song/song.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::swing {

class Interval final : public SyncLevel {
public:
	using SyncLevel::SyncLevel;

	void readCurrentValue() { soundEditor.currentValue = currentSong->swingInterval; }
	void writeCurrentValue() { currentSong->changeSwingInterval(soundEditor.currentValue); }

	void selectEncoderAction(int offset) { // So that there's no "off" option
		soundEditor.currentValue += offset;
		int numOptions = getNumOptions();

		// Wrap value
		if (soundEditor.currentValue >= numOptions) soundEditor.currentValue -= (numOptions - 1);
		else if (soundEditor.currentValue < 1) soundEditor.currentValue += (numOptions - 1);

		Value::selectEncoderAction(offset);
	}
};

} // namespace menu_item::swing
