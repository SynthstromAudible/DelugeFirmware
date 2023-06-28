#pragma once
#include "integer.h"
#include "processing/sound/sound.h"

namespace menu_item::patched_param {
class IntegerNonFM : public Integer {
public:
	using Integer::Integer;
	bool isRelevant(Sound* sound, int whichThing) { return (sound->synthMode != SYNTH_MODE_FM); }
};
} // namespace menu_item::patched_param
