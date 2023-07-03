#pragma once
#include "gui/menu_item/ppqn.h"
#include "playback/playback_handler.h"
#include "model/song/song.h"
#include "gui/ui/sound_editor.h"

// Trigger clock in menu
namespace menu_item::trigger::in {
class PPQN : public menu_item::PPQN {
public:
	using menu_item::PPQN::PPQN;
	void readCurrentValue() { soundEditor.currentValue = playbackHandler.analogInTicksPPQN; }
	void writeCurrentValue() {
		playbackHandler.analogInTicksPPQN = soundEditor.currentValue;
		if ((playbackHandler.playbackState & PLAYBACK_CLOCK_EXTERNAL_ACTIVE) && playbackHandler.usingAnalogClockInput)
			playbackHandler.resyncInternalTicksToInputTicks(currentSong);
	}
};
} // namespace menu_item::trigger::in
