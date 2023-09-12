#pragma once
#include "gui/menu_item/value.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"
#include "util/sized.h"

extern "C" {
#if HAVE_OLED
#include "RZA1/oled/oled_low_level.h"
#endif
}

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
	void buttonAction(hid::Button b, bool on, bool inCardRoutine) override;

	virtual size_t size() { return n; };

protected:
#if HAVE_OLED
	virtual void drawValue();
	virtual void drawPixelsForOled() = 0;
#else
	void drawValue() override;
#endif
};

template <size_t n>
void Enumeration<n>::beginSession(MenuItem* navigatedBackwardFrom) {
	Value::beginSession(navigatedBackwardFrom);
#if HAVE_OLED
	soundEditor.menuCurrentScroll = 0;
#else
	drawValue();
#endif
}

template <size_t n>
void Enumeration<n>::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	if (b == hid::button::Y_ENC) {
		if (on) {
			int32_t numOptions = size();
			int32_t randomOption = (random() % numOptions - 1);
			int32_t derivedOffset = randomOption - this->getValue();
			selectEncoderAction(derivedOffset);
		}
	}
}

template <size_t n>
void Enumeration<n>::selectEncoderAction(int32_t offset) {
	this->setValue(this->getValue() + offset);
	int32_t numOptions = size();

#if HAVE_OLED
	if (this->getValue() >= numOptions) {
		this->setValue(numOptions - 1);
	}
	else if (this->getValue() < 0) {
		this->setValue(0);
	}
#else
	if (this->getValue() >= numOptions) {
		this->setValue(this->getValue() - numOptions);
	}
	else if (this->getValue() < 0) {
		this->setValue(this->getValue() + numOptions);
	}
#endif

	Value::selectEncoderAction(offset);
}

template <size_t n>
void Enumeration<n>::drawValue() {
#if HAVE_OLED
	renderUIsForOled();
#else
	numericDriver.setTextAsNumber(this->getValue());
#endif
}

} // namespace deluge::gui::menu_item
