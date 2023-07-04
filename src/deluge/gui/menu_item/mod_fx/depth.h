#pragma once
#include "gui/menu_item/patched_param/integer.h"
#include "processing/sound/sound.h"
#include "util/comparison.h"

namespace menu_item::mod_fx {
class Depth final : public patched_param::Integer {
public:
	using patched_param::Integer::Integer;

	bool isRelevant(Sound* sound, int whichThing) {
		return util::one_of<uint8_t>(sound->modFXType,
		                             {MOD_FX_TYPE_CHORUS, MOD_FX_TYPE_CHORUS_STEREO, MOD_FX_TYPE_PHASER});
	}
};
} // namespace menu_item::mod_fx
