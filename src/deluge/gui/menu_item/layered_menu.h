/*
 * Copyright © 2025 Synthstrom Audible Limited
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
#include <initializer_list>

namespace deluge::gui::menu_item {

// This is a bit brittle to maintain: if anyone adds a virtual method to MenuItem with a default
// implementation and doesn't update this class, things will break.
//
// Should probably make MenuItem purely virtual, and put the default implementations in StandardMenuItem
// for most classes to inherit - but Nikodemus didn't want to do that up-front before this was otherwise
// considered a valid design.

/// @brief Used to stack multiple menu items on a single shortcut pad: SoundEditor calls nextLayer() when
/// a shortcut is activated twice in a row. This is a no-op for regular menu items, but allows LayeredShortcut
/// to change the current_item_, to most other MenuItem methods are delegated.
class LayeredShortcut final : public MenuItem {
public:
	LayeredShortcut(std::initializer_list<MenuItem*> newItems)
	    : MenuItem(), items{newItems}, current_item_{items.begin()} {}

	/// @brief  Activates the next layer of a LayeredShortcut; called by soundEditor when a shortcut which
	/// was already active is activated again.
	/// @return The currently active layer number. Used to build a patchIndex for indirection magic for
	/// envalopes and other menu-items which use it -- doesn't do anything if the menu item doesn't use
	/// soundEditor.patchIndex.
	int32_t nextLayer() final {
		current_item_ = std::next(current_item_);
		if (current_item_ == items.end()) {
			current_item_ = items.begin();
		}
		return std::distance(items.begin(), current_item_);
	}

	/// @brief  Called by SoundEditor when menu item loses focus. Used to reset the active layer back to
	/// first.
	void lostFocus() final {
		// Tell the current layer it lost focus.
		(*current_item_)->lostFocus();
		// Reset to first layer.
		current_item_ = items.begin();
	}

	/// @brief  Returns the actual active menu item.
	MenuItem* actual() final { return (*current_item_)->actual(); }

	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) final {
		return (*current_item_)->buttonAction(b, on, inCardRoutine);
	}
	void horizontalEncoderAction(int32_t offset) final { return (*current_item_)->horizontalEncoderAction(offset); }
	void verticalEncoderAction(int32_t offset) final { return (*current_item_)->verticalEncoderAction(offset); }
	void selectEncoderAction(int32_t offset) final { return (*current_item_)->selectEncoderAction(offset); }
	bool selectEncoderActionEditsInstrument() final { return (*current_item_)->selectEncoderActionEditsInstrument(); }
	MenuItem* selectButtonPress() final { return (*current_item_)->selectButtonPress(); }
	ActionResult timerCallback() final { return (*current_item_)->timerCallback(); }
	bool usesAffectEntire() final { return (*current_item_)->usesAffectEntire(); }
	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             MultiRange** currentRange) final {
		return (*current_item_)->checkPermissionToBeginSession(modControllable, whichThing, currentRange);
	}
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) final {
		(*current_item_)->beginSession(navigatedBackwardFrom);
	}
	void readValueAgain() final { return (*current_item_)->readValueAgain(); }
	void readCurrentValue() final { return (*current_item_)->readCurrentValue(); }
	uint8_t getIndexOfPatchedParamToBlink() final { return (*current_item_)->getIndexOfPatchedParamToBlink(); }
	deluge::modulation::params::Kind getParamKind() final { return (*current_item_)->getParamKind(); }
	uint32_t getParamIndex() final { return (*current_item_)->getParamIndex(); }
	uint8_t shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) final {
		return (*current_item_)->shouldBlinkPatchingSourceShortcut(s, colour);
	}
	MenuItem* patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive = false) final {
		return (*current_item_)->patchingSourceShortcutPress(s, previousPressStillActive);
	}
	void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		return (*current_item_)->learnKnob(cable, whichKnob, modKnobMode, midiChannel);
	}
	bool allowsLearnMode() final { return (*current_item_)->allowsLearnMode(); }
	bool learnNoteOn(MIDICable& cable, int32_t channel, int32_t noteCode) final {
		return (*current_item_)->learnNoteOn(cable, channel, noteCode);
	}
	void learnProgramChange(MIDICable& cable, int32_t channel, int32_t programNumber) final {
		return (*current_item_)->learnProgramChange(cable, channel, programNumber);
	}
	void learnCC(MIDICable& cable, int32_t channel, int32_t ccNumber, int32_t value) final {
		return (*current_item_)->learnCC(cable, channel, ccNumber, value);
	}
	bool shouldBlinkLearnLed() final { return (*current_item_)->shouldBlinkLearnLed(); }
	void unlearnAction() final { return (*current_item_)->unlearnAction(); }
	bool isRangeDependent() final { return (*current_item_)->isRangeDependent(); }
	void renderOLED() final { return (*current_item_)->renderOLED(); }
	void drawPixelsForOled() final { return (*current_item_)->drawPixelsForOled(); }
	std::string_view getTitle() const final { return (*current_item_)->getTitle(); }
	uint8_t shouldDrawDotOnName() final { return (*current_item_)->shouldDrawDotOnName(); }
	void drawName() final { return (*current_item_)->drawName(); }
	std::string_view getName() const final { return (*current_item_)->getName(); }
	std::string_view getShortName() const final { return (*current_item_)->getShortName(); }
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) final {
		return (*current_item_)->isRelevant(modControllable, whichThing);
	}
	bool shouldEnterSubmenu() final { return (*current_item_)->shouldEnterSubmenu(); }
	int32_t getSubmenuItemTypeRenderLength() final { return (*current_item_)->getSubmenuItemTypeRenderLength(); }
	int32_t getSubmenuItemTypeRenderIconStart() final { return (*current_item_)->getSubmenuItemTypeRenderIconStart(); }
	void renderSubmenuItemTypeForOled(int32_t yPixel) final {
		return (*current_item_)->renderSubmenuItemTypeForOled(yPixel);
	}
	void renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) final {
		return (*current_item_)->renderInHorizontalMenu(startX, width, startY, height);
	}
	bool isSubmenu() final { return (*current_item_)->isSubmenu(); }
	void setupNumberEditor() final { return (*current_item_)->setupNumberEditor(); }
	void updatePadLights() final { return (*current_item_)->updatePadLights(); }
	void updateAutomationViewParameter() final { return (*current_item_)->updateAutomationViewParameter(); }

	bool focusChild(MenuItem* item) final { return (*current_item_)->focusChild(item); }
	bool supportsHorizontalRendering() final { return (*current_item_)->supportsHorizontalRendering(); }

private:
	deluge::vector<MenuItem*> items;
	typename decltype(items)::iterator current_item_;
};

} // namespace deluge::gui::menu_item
