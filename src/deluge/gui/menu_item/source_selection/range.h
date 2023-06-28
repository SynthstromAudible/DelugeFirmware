#pragma once
#include "gui/menu_item/source_selection.h"

namespace menu_item::source_selection {
class Range final : public SourceSelection {
public:
	Range();
	ParamDescriptor getDestinationDescriptor();
	MenuItem* selectButtonPress();
	MenuItem* patchingSourceShortcutPress(int newS, bool previousPressStillActive);
};
extern Range rangeMenu;
} // namespace menu_item::source_selection
