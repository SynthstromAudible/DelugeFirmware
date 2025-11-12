#pragma once
#include "gui/menu_item/value.h"

#include <util/functions.h>

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
	void renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) override;

protected:
	void drawPixelsForOled() override = 0;
	void drawValue() override;
	/// Subclasses should implement this to provide a string suitable for using in a horizontal menu.
	/// Writes to a buffer instead of returning a value, since some subclasses (SyncLevel) must generate
	/// their option names. Default implementation renders the current value as number.
	virtual void getShortOption(StringBuf&);

	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		Value::configureRenderingOptions(options);

		DEF_STACK_STRING_BUF(notification_buf, kShortStringBufferSize);
		getShortOption(notification_buf);
		options.notification_value = notification_buf.data();
	}
};

} // namespace deluge::gui::menu_item
