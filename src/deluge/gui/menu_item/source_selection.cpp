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

#include "source_selection.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "model/clip/clip.h"
#include "model/output.h"
#include "model/song/song.h"
#include "modulation/macros/macros.h"
#include "modulation/params/param_descriptor.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/sound.h"
#include "util/d_stringbuf.h"

#include <etl/vector.h>

namespace deluge::gui::menu_item {

const PatchSource sourceMenuContents[] = {
    PatchSource::ENVELOPE_0,
    PatchSource::ENVELOPE_1,
    PatchSource::ENVELOPE_2,
    PatchSource::ENVELOPE_3,
    PatchSource::LFO_GLOBAL_1,
    PatchSource::LFO_LOCAL_1,
    PatchSource::LFO_GLOBAL_2,
    PatchSource::LFO_LOCAL_2,
    PatchSource::VELOCITY,
    PatchSource::NOTE,
    PatchSource::SIDECHAIN,
    PatchSource::RANDOM,
    PatchSource::X,
    PatchSource::Y,
    PatchSource::AFTERTOUCH,
};

// We're assuming all patch sources are here -- and nothing else. Let's make sure.
static_assert(kNumPatchSources == (sizeof(sourceMenuContents) / sizeof(PatchSource)));

// Macro rows appear only where a macro target could land: the feature is on and the current clip is
// a synth. Kit rows are excluded (their cables are per-row while the kit's macros are kit-global).
bool SourceSelection::macroRowsEligible() const {
	Clip* clip = getCurrentClip();
	return Macros::isEnabled() && clip != nullptr && clip->output != nullptr && clip->output->type == OutputType::SYNTH
	       && Macros::macroHost(clip) != nullptr;
}

int32_t SourceSelection::numItems() const {
	return kNumPatchSources + (macroRowsEligible() ? Macros::kNumMacros : 0);
}

// A macro row's label: the macro's user name if set, else "Macro N".
static std::string_view macroRowName(int32_t macroIndex) {
	Output* host = Macros::macroHost(getCurrentClip());
	if (host != nullptr && !host->macros[macroIndex].name.isEmpty()) {
		return host->macros[macroIndex].name.get();
	}
	return l10n::getView(static_cast<l10n::String>(util::to_underlying(l10n::String::STRING_FOR_MACRO_1) + macroIndex));
}

void SourceSelection::toggleMacroRow(int32_t macroIndex) {
	Clip* clip = getCurrentClip();
	Macros::MenuTargetToggle result = Macros::toggleMenuTarget(clip, macroIndex, getDestinationDescriptor());
	if (result == Macros::MenuTargetToggle::INVALID) {
		return;
	}
	readValueAgain(); // refresh the assigned-dot under the popup
	DEF_STACK_STRING_BUF(popup, 48);
	switch (result) {
	case Macros::MenuTargetToggle::ADDED:
	case Macros::MenuTargetToggle::REMOVED: {
		int32_t destination = Macros::destinationForDescriptor(getDestinationDescriptor());
		Macros::appendDestinationName(popup, Macros::macroHost(clip), (uint16_t)destination);
		popup.append(result == Macros::MenuTargetToggle::ADDED ? " added" : " removed");
		break;
	}
	case Macros::MenuTargetToggle::SLOTS_FULL:
		popup.append("MACRO SLOTS FULL"); // transient: expires back into this menu to pick elsewhere
		break;
	case Macros::MenuTargetToggle::CABLE_MISSING:
		popup.append("Cable missing"); // the depth being modulated isn't a created cable yet
		break;
	case Macros::MenuTargetToggle::INVALID:
		return;
	}
	display->displayPopup(popup.c_str());
}

uint8_t SourceSelection::shouldDrawDotOnValue() {
	if (isMacroRow(this->getValue())) {
		return Macros::isMenuTargetAssigned(getCurrentClip(), macroForRow(this->getValue()), getDestinationDescriptor())
		           ? 3
		           : 255;
	}
	return soundEditor.currentParamManager->getPatchCableSet()->isSourcePatchedToDestinationDescriptorVolumeInspecific(
	           s, getDestinationDescriptor())
	           ? 3
	           : 255;
}

void SourceSelection::drawPixelsForOled() {
	// Assuming 3 makes life easier -- let's make sure that holds.
	static_assert(kOLEDMenuNumOptionsVisible == 3);
	etl::vector<int32_t, kOLEDMenuNumOptionsVisible> items{};
	int32_t total = numItems();
	// A macro row is always allowed (it toggles an assignment rather than patching a source).
	auto itemIsAllowed = [this](int32_t index) {
		return isMacroRow(index) || sourceIsAllowed(sourceMenuContents[index]);
	};
	int32_t selected = this->getValue();
	int32_t sourceIndex = selected;
	// 1. Collect the selected item, and allowed items following it.
	//    Possible outcomes: [s], [s, s+1], [s, s+1, s+2]
	while (sourceIndex < total && items.size() < items.capacity()) {
		if (itemIsAllowed(sourceIndex)) {
			items.push_back(sourceIndex);
		}
		sourceIndex++;
	}
	// 2. Collected the allowed items before the selected item:
	//    2.1. If we have the max number of items, replace the last item.
	//    2.2. Otherwise fill up.
	sourceIndex = this->getValue() - 1;
	if (items.size() == items.capacity()) {
		while (sourceIndex >= 0) {
			if (itemIsAllowed(sourceIndex)) {
				items[items.size() - 1] = sourceIndex;
				break;
			}
			sourceIndex--;
		}
	}
	else {
		while (sourceIndex >= 0 && items.size() < items.capacity()) {
			if (itemIsAllowed(sourceIndex)) {
				items.push_back(sourceIndex);
			}
			sourceIndex--;
		}
	}
	// 3. Sort items.
	std::sort(items.begin(), items.end());
	// 4. Convert to names and record position of selected item.
	etl::vector<std::string_view, kOLEDMenuNumOptionsVisible> names{};
	int32_t selectedRowOnScreen = 0;
	for (int32_t i = 0; i < items.size(); i++) {
		sourceIndex = items[i];
		names.push_back(isMacroRow(sourceIndex) ? macroRowName(macroForRow(sourceIndex))
		                                        : getSourceDisplayNameForOLED(sourceMenuContents[sourceIndex]));
		if (sourceIndex == selected) {
			selectedRowOnScreen = i;
		}
	}
	// All done.
	drawItemsForOled(names, selectedRowOnScreen);
}

// 7SEG only
void SourceSelection::drawValue() {
	if (isMacroRow(this->getValue())) {
		char macroText[5] = {'M', 'A', 'C', (char)('1' + macroForRow(this->getValue())), 0};
		display->setText(macroText, false, shouldDrawDotOnValue()); // dot = assigned to this destination
		return;
	}
	l10n::String text;
	using enum l10n::String;

	switch (sourceMenuContents[this->getValue()]) {
	case PatchSource::LFO_GLOBAL_1:
		text = STRING_FOR_PATCH_SOURCE_LFO_GLOBAL_1;
		break;

	case PatchSource::LFO_GLOBAL_2:
		text = STRING_FOR_PATCH_SOURCE_LFO_GLOBAL_2;
		break;

	case PatchSource::LFO_LOCAL_1:
		text = STRING_FOR_PATCH_SOURCE_LFO_LOCAL_1;
		break;

	case PatchSource::LFO_LOCAL_2:
		text = STRING_FOR_PATCH_SOURCE_LFO_LOCAL_2;
		break;

	case PatchSource::ENVELOPE_0:
		text = STRING_FOR_PATCH_SOURCE_ENVELOPE_0;
		break;

	case PatchSource::ENVELOPE_1:
		text = STRING_FOR_PATCH_SOURCE_ENVELOPE_1;
		break;

	case PatchSource::ENVELOPE_2:
		text = STRING_FOR_PATCH_SOURCE_ENVELOPE_2;
		break;

	case PatchSource::ENVELOPE_3:
		text = STRING_FOR_PATCH_SOURCE_ENVELOPE_3;
		break;

	case PatchSource::SIDECHAIN:
		text = STRING_FOR_PATCH_SOURCE_SIDECHAIN;
		break;

	case PatchSource::VELOCITY:
		text = STRING_FOR_PATCH_SOURCE_VELOCITY;
		break;

	case PatchSource::NOTE:
		text = STRING_FOR_PATCH_SOURCE_NOTE;
		break;

	case PatchSource::RANDOM:
		text = STRING_FOR_PATCH_SOURCE_RANDOM;
		break;

	case PatchSource::AFTERTOUCH:
		text = STRING_FOR_PATCH_SOURCE_AFTERTOUCH;
		break;

	case PatchSource::X:
		text = STRING_FOR_PATCH_SOURCE_X;
		break;

	case PatchSource::Y:
		text = STRING_FOR_PATCH_SOURCE_Y;
		break;

	// explicit fallthrough cases
	case PatchSource::NOT_AVAILABLE:
	case PatchSource::SOON:
	case PatchSource::NONE:;
	}

	uint8_t drawDot = shouldDrawDotOnValue();

	display->setText(l10n::get(text), false, drawDot);
}

void SourceSelection::beginSession(MenuItem* navigatedBackwardFrom) {
	this->setValue(0);

	// s == NONE (a macro row was selected last) never matches sourceMenuContents, so the re-find
	// scan below would run off the array - fall through to the fresh-entry scan instead.
	if (navigatedBackwardFrom && s != PatchSource::NONE) {
		while (sourceMenuContents[this->getValue()] != s) {
			this->setValue(this->getValue() + 1);
		}
	}
	else {
		int32_t firstAllowedIndex = kNumPatchSources - 1;
		// Find the first source which patching exists, or the first allowed one if nothing is patched.
		while (true) {
			s = sourceMenuContents[this->getValue()];

			// Patched already?
			if (soundEditor.currentParamManager->getPatchCableSet()
			        ->isSourcePatchedToDestinationDescriptorVolumeInspecific(s, getDestinationDescriptor())) {
				break;
			}

			// Note down the first "allowed" or "editable" source
			if (this->getValue() < firstAllowedIndex && sourceIsAllowed(s)) {
				firstAllowedIndex = this->getValue();
			}

			this->setValue(this->getValue() + 1);

			if (this->getValue() >= kNumPatchSources) {
				this->setValue(firstAllowedIndex);
				s = sourceMenuContents[this->getValue()];
				break;
			}
		}
	}

	if (display->have7SEG()) {
		drawValue();
	}
}

void SourceSelection::readValueAgain() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawValue();
	}
}

void SourceSelection::selectEncoderAction(int32_t offset) {
	// thisValue keeps track of our position
	// newValue keeps track of last allowed value
	int32_t thisValue = this->getValue();
	int32_t newValue = thisValue;
	// delta is the direction we step in
	// steps is the number of step we want to take
	int32_t delta = offset >= 1 ? 1 : -1;
	int32_t steps = offset >= 1 ? offset : -offset;
	// each time through the loop tries on step, which may or may not succeed
	while (steps > 0) {
		thisValue += delta;
		if (thisValue < 0 || numItems() <= thisValue) {
			// Last value in scroll direction.
			break;
		}
		if (isMacroRow(thisValue) || sourceIsAllowed(sourceMenuContents[thisValue])) {
			newValue = thisValue;
			steps--;
		}
	}

	// On a macro row there's no patch source; NONE keeps every s-keyed query (dot, shortcut blink)
	// inert without special-casing them.
	s = isMacroRow(newValue) ? PatchSource::NONE : sourceMenuContents[newValue];
	this->setValue(newValue);

	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawValue();
	}
}

bool SourceSelection::sourceIsAllowed(PatchSource source) {
	ParamDescriptor destinationDescriptor = getDestinationDescriptor();

	// If patching to another cable's range...
	if (!destinationDescriptor.isJustAParam()) {
		// Global source - can control any range
		if (source < kFirstLocalSource) {
			return true;
		}

		// Local source - range must be for cable going to local param
		else {
			return destinationDescriptor.getJustTheParam() < deluge::modulation::params::FIRST_GLOBAL;
		}
	}

	int32_t p = destinationDescriptor.getJustTheParam();

	// Check that this source is allowed to be patched to the selected param
	if (p == deluge::modulation::params::GLOBAL_VOLUME_POST_FX) {
		return (
		    soundEditor.currentSound->maySourcePatchToParam(source, deluge::modulation::params::GLOBAL_VOLUME_POST_FX,
		                                                    (ParamManagerForTimeline*)soundEditor.currentParamManager)
		        != PatchCableAcceptance::DISALLOWED
		    || soundEditor.currentSound->maySourcePatchToParam(
		           source, deluge::modulation::params::LOCAL_VOLUME,
		           (ParamManagerForTimeline*)soundEditor.currentParamManager)
		           != PatchCableAcceptance::DISALLOWED
		    || soundEditor.currentSound->maySourcePatchToParam(
		           source, deluge::modulation::params::GLOBAL_VOLUME_POST_REVERB_SEND,
		           (ParamManagerForTimeline*)soundEditor.currentParamManager)
		           != PatchCableAcceptance::DISALLOWED);
	}
	else {
		return (soundEditor.currentSound->maySourcePatchToParam(
		            source, p, (ParamManagerForTimeline*)soundEditor.currentParamManager)
		        != PatchCableAcceptance::DISALLOWED);
	}
}

uint8_t SourceSelection::getIndexOfPatchedParamToBlink() {
	return soundEditor.patchingParamSelected;
}

uint8_t SourceSelection::shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) {
	return soundEditor.currentParamManager->getPatchCableSet()->isSourcePatchedToDestinationDescriptorVolumeInspecific(
	           s, getDestinationDescriptor())
	           ? 3
	           : 255;
}

} // namespace deluge::gui::menu_item
