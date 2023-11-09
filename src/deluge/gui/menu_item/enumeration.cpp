#include "enumeration.h"

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
	this->setValue(this->getValue() + offset);
	int32_t numOptions = size();

	if (display->haveOLED()) {
		if (this->getValue() >= numOptions) {
			this->setValue(numOptions - 1);
		}
		else if (this->getValue() < 0) {
			this->setValue(0);
		}
	}
	else {
		if (this->getValue() >= numOptions) {
			this->setValue(this->getValue() - numOptions);
		}
		else if (this->getValue() < 0) {
			this->setValue(this->getValue() + numOptions);
		}
	}

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
