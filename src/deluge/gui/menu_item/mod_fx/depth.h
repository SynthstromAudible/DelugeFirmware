#pragma once
#include "gui/menu_item/patched_param/integer.h"
#include "processing/sound/sound.h"

namespace menu_item::mod_fx {
class Depth final : public patched_param::Integer {
public:
	using patched_param::Integer::Integer;

	bool isRelevant(Sound* sound, int whichThing) {
		return (sound->modFXType == MOD_FX_TYPE_CHORUS || sound->modFXType == MOD_FX_TYPE_PHASER);
	}
};
} // namespace menu_item::mod_fx
