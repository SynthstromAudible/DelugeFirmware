#pragma once

#include "util/sized.h"
#include "value.h"

namespace deluge::gui::menu_item {

class Toggle : public Value<bool> {
public:
	using Value::Value;
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	void selectEncoderAction(int offset) override;

	virtual void drawValue();
#if HAVE_OLED
	void drawPixelsForOled();
#endif
};
} // namespace deluge::gui::menu_item
