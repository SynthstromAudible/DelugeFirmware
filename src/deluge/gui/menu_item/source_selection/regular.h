#pragma once
#include "gui/menu_item/source_selection.h"

namespace menu_item::source_selection {
class Regular final : public SourceSelection {
public:
	Regular();
	void beginSession(MenuItem* navigatedBackwardFrom = NULL);
	ParamDescriptor getDestinationDescriptor();
	MenuItem* selectButtonPress();
	MenuItem* patchingSourceShortcutPress(int newS, bool previousPressStillActive);
};

extern Regular regularMenu;
} // namespace menu_item::source_selection
