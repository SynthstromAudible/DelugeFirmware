/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "gui/ui/browser/browser.h"

class Instrument;

class SlotBrowser : public Browser {
public:
	SlotBrowser() = default;

	// 7SEG ONLY
	void focusRegained() override;
	ActionResult horizontalEncoderAction(int32_t offset) override;

	Error getCurrentFilePath(String* path) override;

protected:
	Error beginSlotSession(bool shouldDrawKeys = true, bool allowIfNoFolder = false);
	void processBackspace() override;
	// bool predictExtendedText();
	virtual void predictExtendedTextFromMemory() {}
	// 7SEG: whether turning the horizontal encoder on a non-slot name switches to the text view so the cursor is
	// visible. Load browsers keep just scrolling the name, so the keyboard doesn't pop up while browsing (#101).
	virtual bool showsTextCursorOn7Seg() const { return false; }

	// Although this is only needed by the child class LoadInstrumentPresetUI, we cut a
	// corner by including it here so our functions can set it to NULL, which is needed.
	// This is the Instrument we're currently scrolled onto. Might not be actually loaded
	// (yet)? We do need this, separate from the current FileItem, because if user moves
	// onto a folder, the currentInstrument needs to remain the same.
	Instrument* currentInstrument = nullptr;
};
