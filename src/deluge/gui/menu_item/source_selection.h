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
#include "value.h"

class ParamDescriptor;

namespace deluge::gui::menu_item {
class SourceSelection : public Value<int32_t> {
public:
	using Value::Value;
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	void selectEncoderAction(int32_t offset) final;
	virtual ParamDescriptor getDestinationDescriptor() = 0;
	uint8_t getIndexOfPatchedParamToBlink() final;
	uint8_t shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) final;
	void readValueAgain() final;
	void drawPixelsForOled() final;

	// 7seg only
	void drawValue() override;
	PatchSource s;

protected:
	bool sourceIsAllowed(PatchSource source);
	uint8_t shouldDrawDotOnValue();

	// ── Macro rows ──
	// On synth clips (with the macro feature on), the four macros appear after the real patch
	// sources. They are NOT patch sources: selecting one toggles this menu's destination as a target
	// of that macro - "Modulate with" assigns the param itself, "Modulate depth" the cable's depth -
	// so a macro can be attached right where the routing is on screen, instead of walking back to the
	// macro's own target picker. The subclasses' selectButtonPress routes macro rows here.
	bool macroRowsEligible() const;
	int32_t numItems() const; // kNumPatchSources plus the macro rows when eligible
	static bool isMacroRow(int32_t value) { return value >= kNumPatchSources; }
	static int32_t macroForRow(int32_t value) { return value - kNumPatchSources; }
	void toggleMacroRow(int32_t macroIndex); // toggle + popup feedback; stays in the menu
};
} // namespace deluge::gui::menu_item
