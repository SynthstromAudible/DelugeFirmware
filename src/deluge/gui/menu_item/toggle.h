#pragma once

#include "value.h"

namespace deluge::gui::menu_item {

class Toggle : public Value<bool> {
public:
	using Value::Value;
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	void selectEncoderAction(int32_t offset) override;

	virtual void drawValue();
	void drawPixelsForOled();
	void displayToggleValue();

	// don't enter menu on select button press
	bool shouldEnterSubmenu() override { return false; }

	// renders toggle item type in submenus after the item name
	void renderSubmenuItemTypeForOled(int32_t yPixel) override;

	// toggles boolean ON / OFF
	void toggleValue() {
		readCurrentValue();
		setValue(!getValue());
		writeCurrentValue();
	};

	// handles changing bool setting without entering menu and updating the display
	MenuItem* selectButtonPress() override {
		toggleValue();
		displayToggleValue();
		return (MenuItem*)0xFFFFFFFF; // no navigation
	}

	// get's toggle status for rendering checkbox on OLED
	bool getToggleValue() {
		readCurrentValue();
		return this->getValue();
	}

	// get's toggle status for rendering dot on 7SEG
	uint8_t shouldDrawDotOnName() override {
		readCurrentValue();
		return this->getValue() ? 3 : 255;
	}
};

/// the toggle pointer passed to this class must be valid for as long as the menu exists
/// this means that you cannot use, for example, song specific or mod controllable stack toggles
class ToggleBool : public Toggle {
public:
	using Toggle::Toggle;

	ToggleBool(l10n::String newName, l10n::String title, bool& newToggle) : Toggle(newName, title), t(newToggle) {}

	void readCurrentValue() override { this->setValue(t); }
	void writeCurrentValue() override { t = this->getValue(); }

	bool& t;
};

class ToggleBoolDyn : public Toggle {
public:
	using Toggle::Toggle;

	ToggleBoolDyn(l10n::String newName, l10n::String title, bool* (*getTogglePtr)()) : Toggle(newName, title) {
		getTPtr = getTogglePtr;
	}

	void readCurrentValue() override { this->setValue(*(getTPtr())); }
	void writeCurrentValue() override { *(getTPtr()) = this->getValue(); }

	bool* (*getTPtr)();
};

class InvertedToggleBool : public Toggle {
public:
	using Toggle::Toggle;

	InvertedToggleBool(l10n::String newName, l10n::String title, bool& newToggle)
	    : Toggle(newName, title), t(newToggle) {}

	void readCurrentValue() override { this->setValue(!t); }
	void writeCurrentValue() override { t = !this->getValue(); }

	bool& t;
};

class InvertedToggleBoolDyn : public Toggle {
public:
	using Toggle::Toggle;

	InvertedToggleBoolDyn(l10n::String newName, l10n::String title, bool* (*getTogglePtr)()) : Toggle(newName, title) {
		getTPtr = getTogglePtr;
	}

	void readCurrentValue() override { this->setValue(!*(getTPtr())); }
	void writeCurrentValue() override { *(getTPtr()) = !this->getValue(); }

	bool* (*getTPtr)();
};

} // namespace deluge::gui::menu_item
