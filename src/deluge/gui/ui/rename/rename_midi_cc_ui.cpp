/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "gui/ui/rename/rename_midi_cc_ui.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/views/automation_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "io/midi/midi_macro.h"
#include "model/instrument/midi_instrument.h"
#include "model/output.h"
#include "model/song/song.h"
#include <string_view>

RenameMidiCCUI renameMidiCCUI{"CC Name"};

bool RenameMidiCCUI::opened() {
	// the same UI names both real CCs and macro lanes - retitle to match what's selected
	title = MIDIMacro::isMacroParamID(getCurrentClip()->lastSelectedParamID) ? "Macro Name" : "CC Name";
	return RenameUI::opened();
}

bool RenameMidiCCUI::canRename() const {
	Clip* clip = getCurrentClip();
	int32_t cc = clip->lastSelectedParamID;
	// real CCs and macro lanes can be named; the expression pseudo-CCs can't
	return (cc >= 0 && cc != CC_EXTERNAL_MOD_WHEEL && cc < kNumRealCCNumbers) || MIDIMacro::isMacroParamID(cc);
}

std::string_view RenameMidiCCUI::getCurrentName() const {
	Clip* clip = getCurrentClip();
	MIDIInstrument* midiInstrument = (MIDIInstrument*)clip->output;
	int32_t cc = clip->lastSelectedParamID;
	if (MIDIMacro::isMacroParamID(cc)) {
		return midiInstrument->macros[MIDIMacro::macroIndexFromParamID(cc)].name.get();
	}
	return midiInstrument->getNameFromCC(cc);
}

bool RenameMidiCCUI::trySetName(std::string_view name) {

	Clip* clip = getCurrentClip();
	MIDIInstrument* midiInstrument = (MIDIInstrument*)clip->output;
	int32_t cc = clip->lastSelectedParamID;

	if (MIDIMacro::isMacroParamID(cc)) {
		midiInstrument->macros[MIDIMacro::macroIndexFromParamID(cc)].name.set(name);
	}
	else {
		midiInstrument->setNameForCC(cc, name);
	}
	midiInstrument->editedByUser = true; // need to set this to true so that the name gets saved with the song / preset

	return true;
}
