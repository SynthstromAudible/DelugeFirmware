/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "gui/menu_item/menu_item.h"
#include "gui/ui/sound_editor.h"
#include "util/containers.h"
#include <initializer_list>
#include <span>

namespace deluge::gui::menu_item {

class Submenu : public MenuItem {
public:
	enum RenderingStyle { VERTICAL, HORIZONTAL };

	Submenu(l10n::String newName, std::initializer_list<MenuItem*> newItems)
	    : MenuItem(newName), items{newItems}, current_item_{items.end()} {}
	Submenu(l10n::String newName, std::span<MenuItem*> newItems)
	    : MenuItem(newName), items{newItems.begin(), newItems.end()}, current_item_{items.end()} {}
	Submenu(l10n::String newName, l10n::String title, std::initializer_list<MenuItem*> newItems)
	    : MenuItem(newName, title), items{newItems}, current_item_{items.end()} {}
	Submenu(l10n::String newName, l10n::String title, std::span<MenuItem*> newItems)
	    : MenuItem(newName, title), items{newItems.begin(), newItems.end()}, current_item_{items.end()} {}

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	void updateDisplay();
	void selectEncoderAction(int32_t offset) override;
	MenuItem* selectButtonPress() final;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	void readValueAgain() final { updateDisplay(); }
	void unlearnAction() final;
	bool usesAffectEntire() override;
	bool allowsLearnMode() final;
	void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final;
	void learnProgramChange(MIDICable& cable, int32_t channel, int32_t programNumber) override;
	bool learnNoteOn(MIDICable& cable, int32_t channel, int32_t noteCode) final;
	virtual RenderingStyle renderingStyle() const { return RenderingStyle::VERTICAL; };
	void renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) override;
	void drawPixelsForOled() override;
	void drawSubmenuItemsForOled(std::span<MenuItem*> options, const int32_t selectedOption);
	/// @brief 	Indicates if the menu-like object should wrap-around. Destined to be virtualized.
	///         At the moment implements the legacy behaviour of wrapping on 7seg but not on OLED.
	bool wrapAround();
	bool isSubmenu() override { return true; }
	virtual bool focusChild(const MenuItem* child);
	void updatePadLights() override;
	MenuItem* patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive = false) override;
	deluge::modulation::params::Kind getParamKind() override;
	uint32_t getParamIndex() override;
	void configureRenderingOptions(const HorizontalMenuRenderingOptions &options) override {
		options.occupied_slots = 2;
		options.show_notification = false;
	}

protected:
	deluge::vector<MenuItem*> items;
	typename decltype(items)::iterator current_item_;
	uint32_t initial_index_ = 0;

private:
	bool shouldForwardButtons();
};

} // namespace deluge::gui::menu_item
