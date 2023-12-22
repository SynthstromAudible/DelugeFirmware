#pragma once

#include "util/sized.h"
#include "value.h"

namespace deluge::gui::menu_item {

class Toggle : public Value<bool> {
public:
	using Value::Value;
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	void selectEncoderAction(int32_t offset) override;

	virtual void drawValue();
	void drawPixelsForOled();
};

class ToggleBool : public Toggle {
public:
	using Toggle::Toggle;

	ToggleBool(l10n::String newName, l10n::String title, bool& newToggle) : Toggle(newName, title), t(newToggle) {}

	void readCurrentValue() override { this->setValue(t); }
	void writeCurrentValue() override { t = this->getValue(); }

	bool& t;
};

} // namespace deluge::gui::menu_item
