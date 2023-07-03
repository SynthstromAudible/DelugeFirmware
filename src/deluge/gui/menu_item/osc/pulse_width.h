#pragma once
#include "modulation/params/param_set.h"
#include "gui/menu_item/source/patched_param.h"
#include "processing/sound/sound.h"

namespace menu_item::osc {
class PulseWidth final : public menu_item::source::PatchedParam {
public:
	using menu_item::source::PatchedParam::PatchedParam;

	int32_t getFinalValue() { return (uint32_t)soundEditor.currentValue * (85899345 >> 1); }

	void readCurrentValue() {
		soundEditor.currentValue =
		    ((int64_t)soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP()) * 100 + 2147483648) >> 32;
	}

	bool isRelevant(Sound* sound, int whichThing) {
		if (sound->getSynthMode() == SYNTH_MODE_FM) return false;
		int oscType = sound->sources[whichThing].oscType;
		return (oscType != OSC_TYPE_SAMPLE && oscType != OSC_TYPE_INPUT_L && oscType != OSC_TYPE_INPUT_R
		        && oscType != OSC_TYPE_INPUT_STEREO);
	}
};

} // namespace menu_item::osc
