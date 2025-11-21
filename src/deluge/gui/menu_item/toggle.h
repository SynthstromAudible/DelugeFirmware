#pragma once

#include "gui/ui/sound_editor.h"
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
	void renderInHorizontalMenu(const SlotPosition& slot) override;

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

	// handles toggling a "toggle" menu from sub-menu level
	// or handles going back up a level after making a selection from within toggle menu
	MenuItem* selectButtonPress() override {
		// this is true if you open a toggle menu using grid shortcut
		if (soundEditor.getCurrentMenuItem() == this) {
			return nullptr; // go up a level
		}
		// you're toggling toggle menu from submenu level
		else {
			toggleValue();
			displayToggleValue();
			return NO_NAVIGATION;
		}
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

private:
	const char* getNameFor(bool on);
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
