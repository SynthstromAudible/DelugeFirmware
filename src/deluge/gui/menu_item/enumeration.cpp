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
	this->setValue(this->getValue() + offset);
	int32_t numOptions = size();
	int32_t sign = (offset < 0) ? -1 : ((offset > 0) ? 1 : 0);

	switch (numOptions) {
	case 0:
		[[fallthrough]];
	case 1:
		[[fallthrough]];
	case 2:
		offset = 1 * sign;
		break;
	case 3:
		offset = std::min<int32_t>(offset * sign, 2) * sign;
		break;
	case 4:
		offset = std::min<int32_t>(offset * sign, 3) * sign;
		break;
	case 5:
		offset = std::min<int32_t>(offset * sign, 4) * sign;
		break;
	default:
		break;
	}

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
