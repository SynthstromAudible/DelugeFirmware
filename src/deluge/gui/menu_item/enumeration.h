#pragma once
#include "gui/menu_item/value.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "util/sized.h"

namespace deluge::gui::menu_item {

/**
 * @brief An enumeration has a fixed number of items, with values from 1 to n (exclusive)
 */
template <size_t n>
class Enumeration : public Value<int32_t> {
public:
	using Value::Value;
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	void selectEncoderAction(int32_t offset) override;

	virtual size_t size() { return n; };

protected:
	virtual void drawPixelsForOled() = 0;
	void drawValue() override;
};

template <size_t n>
void Enumeration<n>::beginSession(MenuItem* navigatedBackwardFrom) {
	Value::beginSession(navigatedBackwardFrom);
	if (display->type() == DisplayType::OLED) {
		soundEditor.menuCurrentScroll = 0;
	}
	else {
		drawValue();
	}
}

template <size_t n>
void Enumeration<n>::selectEncoderAction(int32_t offset) {
	this->setValue(this->getValue() + offset);
	int32_t numOptions = size();

	if (display->type() == DisplayType::OLED) {
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

template <size_t n>
void Enumeration<n>::drawValue() {
	if (display->type() == DisplayType::OLED) {
		renderUIsForOled();
	}
	else {
		display->setTextAsNumber(this->getValue());
	}
}

} // namespace deluge::gui::menu_item
