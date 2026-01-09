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
	/// @brief  Should this menu wrap around?
	virtual bool wrapAround();
	void renderInHorizontalMenu(const SlotPosition& slot) override;

protected:
	void drawPixelsForOled() override = 0;
	void drawValue() override;
	/// Subclasses should implement this to provide a string suitable for using in a horizontal menu.
	/// Writes to a buffer instead of returning a value, since some subclasses (SyncLevel) must generate
	/// their option names. Default implementation renders the current value as number.
	virtual void getShortOption(StringBuf&);
	void getNotificationValue(StringBuf& value) override { getShortOption(value); }
};

} // namespace deluge::gui::menu_item
