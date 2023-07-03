#pragma once
#include "gui/menu_item/source/patched_param.h"
#include "processing/sound/sound.h"

namespace menu_item::osc::source {
class WaveIndex final : public menu_item::source::PatchedParam {
public:
	using PatchedParam::PatchedParam;
	bool isRelevant(Sound* sound, int whichThing) {
		Source* source = &sound->sources[whichThing];
		return (sound->getSynthMode() != SYNTH_MODE_FM && source->oscType == OSC_TYPE_WAVETABLE);
	}
};
} // namespace menu_item::osc::source
