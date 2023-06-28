#pragma once
#include "gui/menu_item/patched_param/integer.h"
#include "processing/sound/sound.h"

namespace menu_item::lfo::global {
class Rate final : public patched_param::Integer {
public:
  using Integer::Integer;

	bool isRelevant(Sound* sound, int whichThing) { return (sound->lfoGlobalSyncLevel == 0); }
};
} // namespace menu_item::lfo::global
