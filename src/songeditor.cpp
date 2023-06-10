/*
#include "songeditor.h"

#include <new>
#include "TuningSystem.h"
#include "MenuItemSubmenu.h"
#include "MenuItemTuning.h"
#include "numericdriver.h"

SongEditor songEditor;

MenuItemSubmenu     tuningMenu     ;
MenuItemSubmenu     songRootMenu   ;
MenuItemTuningNote  tuningNoteMenu ;
MenuItemTuningBank  tuningBankMenu ;

SongEditor::SongEditor() {
	new (&tuningSystem) TuningSystem();
	// Song menu -------------------------------------------------------------

	new (&tuningNoteMenu) MenuItemTuningNote("NOTE");
	new (&tuningBankMenu) MenuItemTuningBank("BANK");
	static MenuItem* tuningMenuItems[] = {
		&tuningBankMenu,
		&tuningNoteMenu,
		NULL,
	};
	new (&tuningMenu) MenuItemSubmenu("TUNING", tuningMenuItems);
	static MenuItem* songRootMenuItems[] = {&tuningMenu, NULL};
	new (&songRootMenu) MenuItemSubmenu("SONG", songRootMenuItems);
}

bool SongEditor::setup(const MenuItem* item) {
	MenuItem* newItem;

	if (item) {
		newItem = (MenuItem*)item;
	}
	else {
		newItem = &songRootMenu;
	}

	menuItemNavigationRecord[navigationDepth] = newItem;
	numericDriver.setNextTransitionDirection(1);
	return true;
}

MenuItem* SongEditor::getCurrentMenuItem() {
	return menuItemNavigationRecord[navigationDepth];
}

#if HAVE_OLED
void SongEditor::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {

	// Sorry - extremely ugly hack here.
	MenuItem* currentMenuItem = getCurrentMenuItem();

	currentMenuItem->renderOLED();
}
#endif


*/
