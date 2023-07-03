#pragma once
#include "gui/menu_item/unpatched_param.h"
#include "processing/sound/sound.h"

namespace menu_item::mod_fx {

class Offset final : public UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;

	bool isRelevant(Sound* sound, int whichThing) {
		return (!sound
		        || sound->modFXType == MOD_FX_TYPE_CHORUS); // TODO: really want to receive a ModControllableAudio here!
	}
};

} // namespace menu_item::mod_fx
