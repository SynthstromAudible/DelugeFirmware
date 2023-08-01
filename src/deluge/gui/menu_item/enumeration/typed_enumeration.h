#pragma once

#include "util/misc.h"
#include <type_traits>
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
template <util::enumeration Enum, size_t n>
class TypedEnumeration : public Value<Enum> {
public:
	using Value<Enum>::Value;
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	void selectEncoderAction(int32_t offset) override;

	virtual size_t size() { return n; };

protected:
#if HAVE_OLED
	virtual void drawValue();
	virtual void drawPixelsForOled() = 0;
#else
	void drawValue() override;
#endif
};

template <util::enumeration Enum, size_t n>
void TypedEnumeration<Enum, n>::beginSession(MenuItem* navigatedBackwardFrom) {
	Value<Enum>::beginSession(navigatedBackwardFrom);
#if HAVE_OLED
	soundEditor.menuCurrentScroll = 0;
#else
	drawValue();
#endif
}

template <util::enumeration Enum, size_t n>
void TypedEnumeration<Enum, n>::selectEncoderAction(int32_t offset) {
	auto current = util::to_underlying(this->value_) + offset;
	auto numOptions = size();

#if HAVE_OLED
	if (current >= numOptions) {
		current = numOptions - 1;
	}
	else if (current < 0) {
		current = 0;
	}
#else
	if (current >= numOptions) {
		current -= numOptions;
	}
	else if (current < 0) {
		current += numOptions;
	}
#endif

	this->value_ = static_cast<Enum>(current);
	Value<Enum>::selectEncoderAction(offset);
}

template <util::enumeration Enum, size_t n>
void TypedEnumeration<Enum, n>::drawValue() {
#if HAVE_OLED
	renderUIsForOled();
#else
	numericDriver.setTextAsNumber(util::to_underlying(this->value_));
#endif
}

} // namespace deluge::gui::menu_item
