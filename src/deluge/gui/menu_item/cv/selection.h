#pragma once
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "gui/menu_item/submenu.h"
#include "volts.h"
#include "transpose.h"

extern void setCvNumberForTitle(int m);
extern menu_item::Submenu cvSubmenu;

namespace menu_item::cv {
#if HAVE_OLED
static char const* cvOutputChannel[] = {"CV output 1", "CV output 2", NULL};
#else
static char const* cvOutputChannel[] = {"Out1", "Out2", NULL};
#endif

class Selection final : public menu_item::Selection {
public:
	Selection(char const* newName = NULL) : menu_item::Selection(newName) {
#if HAVE_OLED
		basicTitle = "CV outputs";
#endif
		basicOptions = cvOutputChannel;
	}
	void beginSession(MenuItem* navigatedBackwardFrom) {
		if (!navigatedBackwardFrom) {
			soundEditor.currentValue = 0;
		}
		else {
			soundEditor.currentValue = soundEditor.currentSourceIndex;
		}
		Selection::beginSession(navigatedBackwardFrom);
	}

	MenuItem* selectButtonPress() {
		soundEditor.currentSourceIndex = soundEditor.currentValue;
#if HAVE_OLED
		cvSubmenu.basicTitle = cvOutputChannel[soundEditor.currentValue];
		setCvNumberForTitle(soundEditor.currentValue);
#endif
		return &cvSubmenu;
	}
};
} // namespace menu_item::cv
