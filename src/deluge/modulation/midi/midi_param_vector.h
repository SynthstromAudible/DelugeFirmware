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

#include "modulation/params/param_control.h"
#include "util/container/array/ordered_resizeable_array.h"

class MIDIParam;

class MIDIParamVector : public OrderedResizeableArray {
public:
	MIDIParamVector();
	Error beenCloned();
	void deleteAtKey(int32_t key);
	void bindPresetValues(const int32_t* values);
	// New entries bind to these too, so this shows what the whole vector is bound to.
	[[nodiscard]] const int32_t* boundPresetValues() const { return presetValues_; }
	MIDIParam* getElement(int32_t i);
	MIDIParam* getParamFromCC(int32_t cc);
	int32_t getValueForControl(int32_t cc, deluge::modulation::params::ControlLayer layer);
	MIDIParam* insertParam(int32_t i);
	MIDIParam* getOrCreateParamFromCC(int32_t cc, int32_t defaultValue = 0, bool allowCreation = true);

private:
	void rebindAfterMove();
	// Only the CC numbers a MIDI clip uses have a baseline; any other reads zero.
	[[nodiscard]] const int32_t* presetValueFor(uint32_t cc) const {
		return presetValues_ != nullptr && cc < kNumCCNumbersIncludingFake ? &presetValues_[cc] : nullptr;
	}
	const int32_t* presetValues_ = nullptr;
};
