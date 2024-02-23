#include "enumeration.h"
#include "gui/ui/sound_editor.h"

namespace deluge::gui::menu_item {
void Enumeration::beginSession(MenuItem* navigatedBackwardFrom) {
	Value::beginSession(navigatedBackwardFrom);
	if (display->haveOLED()) {
		soundEditor.menuCurrentScroll = 0;
	}
	else {
		drawValue();
	}
}

void Enumeration::selectEncoderAction(int32_t offset) {
	int32_t numOptions = size();
	int32_t startValue = getValue();

	// valid values are [0, numOptions), so wrap as needed
	int32_t nextValue = (startValue + offset) % numOptions;
	if (nextValue < 0) {
		nextValue += numOptions;
	}

	setValue(nextValue);

	// reset offset to account for wrapping
	offset = nextValue - startValue;
	Value::selectEncoderAction(offset);
}

void Enumeration::drawValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	if (display->have7SEG()) {
		display->setTextAsNumber(this->getValue());
	}
}
} // namespace deluge::gui::menu_item
