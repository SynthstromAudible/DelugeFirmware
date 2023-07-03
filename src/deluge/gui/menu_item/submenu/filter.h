#pragma once
#include "gui/menu_item/submenu.h"
#include "processing/sound/sound.h"

namespace menu_item::submenu {
class Filter final : public Submenu {
public:
	Filter(char const* newName = NULL, MenuItem** newFirstItem = NULL) : Submenu(newName, newFirstItem) {}
	bool isRelevant(Sound* sound, int whichThing) { return (sound->synthMode != SYNTH_MODE_FM); }
};
} // namespace menu_item::submenu
