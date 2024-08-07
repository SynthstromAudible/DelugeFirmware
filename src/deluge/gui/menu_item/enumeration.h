#pragma once
#include "gui/menu_item/value.h"

namespace deluge::gui::menu_item {

/**
 * @brief An enumeration has a fixed number of items, with values from 1 to n (exclusive)
 */
class Enumeration : public Value<int32_t> {
public:
	using Value::Value;
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	void selectEncoderAction(int32_t offset) override;
	virtual size_t size() = 0;

protected:
	virtual void drawPixelsForOled() override = 0;
	void drawValue() override;
	virtual bool wrapAround() { return false; }
};

} // namespace deluge::gui::menu_item
