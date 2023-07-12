#pragma once

#include "selection.h"
#include "util/sized.h"

namespace deluge::gui::menu_item {

class Toggle : public Value<bool> {
public:
	using Value::Value;

	virtual void drawValue();
#if HAVE_OLED
	void drawPixelsForOled();
#endif
};
} // namespace deluge::gui::menu_item
