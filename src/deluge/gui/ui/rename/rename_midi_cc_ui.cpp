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
#include "model/instrument/midi_instrument.h"
#include "model/output.h"
#include "model/song/song.h"
#include "modulation/macros/macros.h"
#include <string_view>

RenameMidiCCUI renameMidiCCUI{"CC Name"};

// The macro lane the current selection points at (works on MIDI and synth clips), or -1.
static int32_t selectedMacroLane() {
	Clip* clip = getCurrentClip();
	return Macros::macroIndexForLaneSelection(clip->output, clip->lastSelectedParamKind, clip->lastSelectedParamID);
}

bool RenameMidiCCUI::opened() {
	// the same UI names both real CCs and macro lanes - retitle to match what's selected
	title = (selectedMacroLane() >= 0) ? "Macro Name" : "CC Name";
	return RenameUI::opened();
}

bool RenameMidiCCUI::canRename() const {
	Clip* clip = getCurrentClip();
	if (selectedMacroLane() >= 0) {
		return true; // macro lanes can be named on both MIDI and synth clips
	}
	if (clip->output->type != OutputType::MIDI_OUT) {
		return false; // synth params other than macro lanes have fixed names
	}
	int32_t cc = clip->lastSelectedParamID;
	// real CCs can be named; the expression pseudo-CCs can't
	return cc >= 0 && cc != CC_EXTERNAL_MOD_WHEEL && cc < kNumRealCCNumbers;
}

std::string_view RenameMidiCCUI::getCurrentName() const {
	Clip* clip = getCurrentClip();
	int32_t macroIndex = selectedMacroLane();
	if (macroIndex >= 0) {
		return ((MelodicInstrument*)clip->output)->macros[macroIndex].name.get();
	}
	return ((MIDIInstrument*)clip->output)->getNameFromCC(clip->lastSelectedParamID);
}

bool RenameMidiCCUI::trySetName(std::string_view name) {
	Clip* clip = getCurrentClip();
	int32_t macroIndex = selectedMacroLane();
	if (macroIndex >= 0) {
		MelodicInstrument* instrument = (MelodicInstrument*)clip->output;
		instrument->macros[macroIndex].name.set(name);
		instrument->editedByUser = true;
	}
	else {
		MIDIInstrument* midiInstrument = (MIDIInstrument*)clip->output;
		midiInstrument->setNameForCC(clip->lastSelectedParamID, name);
		// need to set this to true so that the name gets saved with the song / preset
		midiInstrument->editedByUser = true;
	}
	return true;
}
