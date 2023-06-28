#pragma once
#include "gui/menu_item/submenu.h"
#include "model/clip/clip.h"
#include "model/song/song.h"
#include "model/output.h"

namespace menu_item::submenu {
class Bend final : public Submenu {
public:
	Bend(char const* newName = nullptr, MenuItem** newItems = nullptr) : Submenu(newName, newItems) {}
	bool isRelevant(Sound* sound, int whichThing) override {
		// Drums within a Kit don't need the two-item submenu - they have their own single item.
		const auto type = currentSong->currentClip->output->type;
		return (type == INSTRUMENT_TYPE_SYNTH || type == INSTRUMENT_TYPE_CV);
	}
};
}
