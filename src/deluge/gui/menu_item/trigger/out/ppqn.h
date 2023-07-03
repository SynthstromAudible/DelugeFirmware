#pragma once
#include "gui/menu_item/ppqn.h"
#include "playback/playback_handler.h"
#include "model/song/song.h"
#include "gui/ui/sound_editor.h"

// Trigger clock out menu
namespace menu_item::trigger::out {
class PPQN : public menu_item::PPQN {
public:
	using menu_item::PPQN::PPQN;
	void readCurrentValue() { soundEditor.currentValue = playbackHandler.analogOutTicksPPQN; }
	void writeCurrentValue() {
		playbackHandler.analogOutTicksPPQN = soundEditor.currentValue;
		playbackHandler.resyncAnalogOutTicksToInternalTicks();
	}
};
} // namespace menu_item::trigger::out
