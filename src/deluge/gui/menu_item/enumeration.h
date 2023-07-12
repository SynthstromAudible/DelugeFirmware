#pragma once
#include "value.h"
#include "util/sized.h"

namespace deluge::gui::menu_item {

/**
 * @brief An enumeration has a fixed number of items, with values from 1 to size
 */
class Enumeration : public Value<size_t> {
public:
	using Value::Value;
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	void selectEncoderAction(int offset) override;

	virtual size_t size() = 0;

protected:
	virtual void drawValue();
#if HAVE_OLED
	virtual void drawPixelsForOled() = 0;
#endif
};
} // namespace deluge::gui::menu_item
