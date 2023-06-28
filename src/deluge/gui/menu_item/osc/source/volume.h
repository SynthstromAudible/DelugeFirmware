#pragma once
#include "gui/menu_item/source/patched_param.h"
#include "processing/sound/sound.h"

namespace menu_item::osc::source {

class Volume final : public menu_item::source::PatchedParam {
public:
	using PatchedParam::PatchedParam;
	bool isRelevant(Sound* sound, int whichThing) { return (sound->getSynthMode() != SYNTH_MODE_RINGMOD); }
};
} // namespace menu_item::osc
