#include "menu_item.h"
#include "definitions.h"

namespace menu_item {
class PatchCables : public MenuItem {
public:
	PatchCables(char const* newName = nullptr) : MenuItem(newName) {}
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) final;
	void selectEncoderAction(int offset) final;
	void readValueAgain() final;
	MenuItem* selectButtonPress() final;

#if HAVE_OLED
	void drawPixelsForOled() final;
	static int selectedRowOnScreen;
	int scrollPos = 0; // Each instance needs to store this separately
#else
	void drawValue();
#endif

	uint8_t savedVal = 0;
};

}
