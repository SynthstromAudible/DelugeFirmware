/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "gui/ui/browser/slot_browser.h"
#include "hid/button.h"

class SaveUI : public SlotBrowser {
public:
	SaveUI();
	bool opened() override;

	virtual bool performSave(bool mayOverwrite = false) = 0; // Returns true if success, or if otherwise dealt with
	                                                         // (e.g. "overwrite" context menu brought up)
	void focusRegained() override;
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth] = nullptr,
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = nullptr) override {
		return true;
	}
	bool canSeeViewUnderneath() final { return false; }
	ActionResult timerCallback() override;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;

protected:
	// void displayText(bool blinkImmediately) final;
	void enterKeyPress() final;
	static bool currentFolderIsEmpty;

	/// The save round-trip gate drives the REAL ENTER gesture rather than performSave() underneath
	/// it, so it needs the protected entry. Mirrors BrowserListingAbProbe's friendship in browser.h.
	friend class SaveRoundtripProbe;

public:
	/// @brief How many times a performSave() body has begun on the INTERACTION tier.
	///
	/// Writing the card from there is the violation this rung exists to remove, so the gate asserts
	/// this is zero. Counting rather than freezing lets the gate report the number before the
	/// conversion as well as after it; the hard assertion lands alongside.
	[[nodiscard]] static int32_t interactionTierSaves() { return interactionTierSaves_; }
	/// @brief Record one performSave() entry. Call as the FIRST statement of every override.
	static void noteSaveTier();

private:
	static inline int32_t interactionTierSaves_{};
};
