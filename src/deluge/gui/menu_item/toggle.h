#pragma once

#include "selection.h"
#include "util/sized.h"

namespace deluge::gui::menu_item {

class Toggle : public Selection {
public:
	using Selection::Selection;

	Sized<char const**> getOptions() override;
};
} // namespace deluge::gui::menu_item
