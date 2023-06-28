#pragma once
#include "gui/menu_item/sync_level.h"
#include "gui/ui/sound_editor.h"
#include "model/song/song.h"

namespace menu_item::sync_level {

// This one is "relative to the song". In that it'll show its text value to the user, e.g. "16ths", regardless of any song variables, and
// then when its value gets used for anything, it'll be transposed into the song's magnitude by adding / subtracting the song's insideWorldTickMagnitude
class RelativeToSong : public SyncLevel {
public:
	using SyncLevel::SyncLevel;

protected:
	void getNoteLengthName(char* buffer) final {
		getNoteLengthNameFromMagnitude(buffer, -6 + 9 - soundEditor.currentValue);
	}
};
} // namespace menu_item::sync_level
