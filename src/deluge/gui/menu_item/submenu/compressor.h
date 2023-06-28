#pragma once
#include "gui/menu_item/submenu.h"

namespace menu_item::submenu {

class Compressor final : public Submenu {
public:
	Compressor() {}
	Compressor(char const* newName, MenuItem** newItems, bool newForReverbCompressor)
	    : Submenu(newName, newItems) {
		forReverbCompressor = newForReverbCompressor;
	}
	void beginSession(MenuItem* navigatedBackwardFrom = NULL);

	bool forReverbCompressor;
};
}
