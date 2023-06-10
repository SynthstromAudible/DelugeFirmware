/*
#ifndef SONGEDITOR_H_
#define SONGEDITOR_H_

#include "UI.h"
#include "MenuItem.h"

#if HAVE_OLED
#include "oled.h"
#endif

class SongEditor final : public UI {
public:
	SongEditor();
	bool setup(const MenuItem *item = NULL);

	MenuItem* getCurrentMenuItem();

	MenuItem* menuItemNavigationRecord[16];
	MenuItem** currentSubmenuItem;

	uint8_t navigationDepth;

#if HAVE_OLED
	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);
#endif
};

extern SongEditor songEditor;

#endif
*/
