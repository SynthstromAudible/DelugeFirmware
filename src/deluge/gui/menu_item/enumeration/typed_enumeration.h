#pragma once

#include "gui/menu_item/value.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.hpp"
#include "util/misc.h"
#include "util/sized.h"
#include <type_traits>

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
	virtual void drawPixelsForOled() = 0;
	void drawValue() override;
};

template <util::enumeration Enum, size_t n>
void TypedEnumeration<Enum, n>::beginSession(MenuItem* navigatedBackwardFrom) {
	Value<Enum>::beginSession(navigatedBackwardFrom);
	if (display.type == DisplayType::OLED) {
		soundEditor.menuCurrentScroll = 0;
	}
	else {
		drawValue();
	}
}

template <util::enumeration Enum, size_t n>
void TypedEnumeration<Enum, n>::selectEncoderAction(int32_t offset) {
	auto current = util::to_underlying(this->value_) + offset;
	auto numOptions = size();

	if (display.type == DisplayType::OLED) {
		if (current >= numOptions) {
			current = numOptions - 1;
		}
		else if (current < 0) {
			current = 0;
		}
	}
	else {
		if (current >= numOptions) {
			current -= numOptions;
		}
		else if (current < 0) {
			current += numOptions;
		}
	}

	this->value_ = static_cast<Enum>(current);
	Value<Enum>::selectEncoderAction(offset);
}

template <util::enumeration Enum, size_t n>
void TypedEnumeration<Enum, n>::drawValue() {
	if (display.type == DisplayType::OLED) {
		renderUIsForOled();
	}
	else {
		display.setTextAsNumber(util::to_underlying(this->value_));
	}
}

} // namespace deluge::gui::menu_item
