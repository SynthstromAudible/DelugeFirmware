#pragma once
#include "gui/menu_item/selection.h"

namespace menu_item::lfo {

class Shape : public Selection {
public:
	using Selection::Selection;

	char const** getOptions() {
		static char const* options[] = {"Sine", "Triangle", "Square", "Saw", "S&H", "Random Walk", NULL};
		return options;
	}
	int getNumOptions() { return NUM_LFO_TYPES; }
};

} // namespace menu_item::lfo
