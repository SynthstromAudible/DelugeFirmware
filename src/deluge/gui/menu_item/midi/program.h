#include "gui/menu_item/submenu.h"

namespace deluge::gui::menu_item::midi {

class ProgramSubMenu : public HorizontalMenu {
public:
	ProgramSubMenu(l10n::String newName, std::initializer_list<MenuItem*> newItems, Layout layout, uint32_t initSelect)
	    : HorizontalMenu(newName, newItems, layout) {
		initial_index_ = initSelect;
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return getCurrentOutputType() == OutputType::MIDI_OUT;
	}
};

} // namespace deluge::gui::menu_item::midi
