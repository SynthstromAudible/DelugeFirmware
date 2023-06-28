#pragma once
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"

namespace menu_item::sidechain{
class Send final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() {
		soundEditor.currentValue = ((uint64_t)soundEditor.currentSound->sideChainSendLevel * 50 + 1073741824) >> 31;
	}
	void writeCurrentValue() {
		if (soundEditor.currentValue == 50) soundEditor.currentSound->sideChainSendLevel = 2147483647;
		else soundEditor.currentSound->sideChainSendLevel = soundEditor.currentValue * 42949673;
	}
	int getMaxValue() const { return 50; }
	bool isRelevant(Sound* sound, int whichThing) { return (soundEditor.editingKit()); }
};
} // namespace menu_item::reverb::compressor
