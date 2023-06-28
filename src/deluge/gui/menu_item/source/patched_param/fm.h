#pragma once
#include "gui/menu_item/source/patched_param.h"
#include "processing/sound/sound.h"

namespace menu_item::source::patched_param {
class FM final : public source::PatchedParam {
public:
	using PatchedParam::PatchedParam;
	bool isRelevant(Sound* sound, int whichThing) { return (sound->getSynthMode() == SYNTH_MODE_FM); }
};
} // namespace menu_item::source::patched_param
