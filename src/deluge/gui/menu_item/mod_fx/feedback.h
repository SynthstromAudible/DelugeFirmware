#pragma once
#include "gui/menu_item/unpatched_param.h"
#include "processing/sound/sound.h"
#include "util/comparison.h"

namespace menu_item::mod_fx {
class Feedback final : public UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;
	bool isRelevant(Sound* sound, int whichThing) {
		// TODO: really want to receive a ModControllableAudio here!
		return (!sound || util::one_of<uint8_t>(sound->modFXType, {MOD_FX_TYPE_FLANGER, MOD_FX_TYPE_PHASER}));
	}
};
} // namespace menu_item::mod_fx
