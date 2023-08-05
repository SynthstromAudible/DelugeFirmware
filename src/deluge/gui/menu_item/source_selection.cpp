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
#include "gui/l10n/l10n.hpp"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.hpp"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable_set.h"
#include "patch_cable_strength.h"
#include "processing/sound/sound.h"
#include <array>

namespace deluge::gui::menu_item {
const PatchSource sourceMenuContents[] = {
    PatchSource::ENVELOPE_0, PatchSource::ENVELOPE_1, PatchSource::LFO_GLOBAL, PatchSource::LFO_LOCAL,
    PatchSource::VELOCITY,   PatchSource::NOTE,       PatchSource::COMPRESSOR, PatchSource::RANDOM,
    PatchSource::X,          PatchSource::Y,          PatchSource::AFTERTOUCH,
};

uint8_t SourceSelection::shouldDrawDotOnValue() {
	return soundEditor.currentParamManager->getPatchCableSet()->isSourcePatchedToDestinationDescriptorVolumeInspecific(
	           s, getDestinationDescriptor())
	           ? 3
	           : 255;
}

int32_t SourceSelection::selectedRowOnScreen;

void SourceSelection::drawPixelsForOled() {
	static_vector<std::string, kOLEDMenuNumOptionsVisible> itemNames{};

	selectedRowOnScreen = 0;

	int32_t thisOption = scrollPos;
	size_t i = 0;

	while (i < kOLEDMenuNumOptionsVisible) {
		if (thisOption >= kNumPatchSources) {
			break;
		}

		const PatchSource sHere = sourceMenuContents[thisOption];

		if (sourceIsAllowed(sHere)) {
			itemNames.push_back(getSourceDisplayNameForOLED(sHere));
			if (thisOption == this->value_) {
				selectedRowOnScreen = static_cast<int32_t>(i);
			}
			i++;
		}
		else {
			if (thisOption == scrollPos) {
				scrollPos++;
			}
		}
		thisOption++;
	}

	drawItemsForOled(itemNames, selectedRowOnScreen);
}

// 7SEG only
void SourceSelection::drawValue() {
	l10n::Strings text;
	using enum l10n::Strings;

	switch (sourceMenuContents[this->value_]) {
	case PatchSource::LFO_GLOBAL:
		text = STRING_FOR_PATCH_SOURCE_LFO_GLOBAL;
		break;

	case PatchSource::LFO_LOCAL:
		text = STRING_FOR_PATCH_SOURCE_LFO_LOCAL;
		break;

	case PatchSource::ENVELOPE_0:
		text = STRING_FOR_PATCH_SOURCE_ENVELOPE_0;
		break;

	case PatchSource::ENVELOPE_1:
		text = STRING_FOR_PATCH_SOURCE_ENVELOPE_1;
		break;

	case PatchSource::COMPRESSOR:
		text = STRING_FOR_PATCH_SOURCE_COMPRESSOR;
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
	}

	uint8_t drawDot = shouldDrawDotOnValue();

	display.setText(l10n::get(text), false, drawDot);
}

void SourceSelection::beginSession(MenuItem* navigatedBackwardFrom) {
	this->value_ = 0;

	if (navigatedBackwardFrom) {
		while (sourceMenuContents[this->value_] != s) {
			this->value_++;
		}
		// Scroll pos will be retained from before.
	}
	else {
		int32_t firstAllowedIndex = kNumPatchSources - 1;
		while (true) {
			s = sourceMenuContents[this->value_];

			// If patching already exists on this source, we use this as the initial one to show to the user
			if (soundEditor.currentParamManager->getPatchCableSet()
			        ->isSourcePatchedToDestinationDescriptorVolumeInspecific(s, getDestinationDescriptor())) {
				break;
			}

			// Note down the first "allowed" or "editable" source
			if (this->value_ < firstAllowedIndex && sourceIsAllowed(s)) {
				firstAllowedIndex = this->value_;
			}

			this->value_++;
			if (display.type == DisplayType::OLED) {
				scrollPos = this->value_;
			}

			if (this->value_ >= kNumPatchSources) {
				this->value_ = firstAllowedIndex;
				if (display.type == DisplayType::OLED) {
					scrollPos = this->value_;
				}
				s = sourceMenuContents[this->value_];
				break;
			}
		}
	}

	if (display.type != DisplayType::OLED) {
		drawValue();
	}
}

void SourceSelection::readValueAgain() {
	if (display.type == DisplayType::OLED) {
		renderUIsForOled();
	}
	else {
		drawValue();
	}
}

void SourceSelection::selectEncoderAction(int32_t offset) {
	bool isAllowed;
	int32_t newValue = this->value_;
	do {
		newValue += offset;

		if (display.type == DisplayType::OLED) {
			if (newValue >= kNumPatchSources || newValue < 0) {
				return;
			}
		}
		else {
			if (newValue >= kNumPatchSources)
				newValue -= kNumPatchSources;
			else if (newValue < 0)
				newValue += kNumPatchSources;
		}

		s = sourceMenuContents[newValue];

	} while (!sourceIsAllowed(s));

	this->value_ = newValue;

	if (display.type == DisplayType::OLED) {
		if (this->value_ < scrollPos) {
			scrollPos = this->value_;
		}
		else if (offset >= 0 && selectedRowOnScreen == kOLEDMenuNumOptionsVisible - 1) {
			scrollPos++;
		}

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
			return destinationDescriptor.getJustTheParam() < ::Param::Global::FIRST;
		}
	}

	int32_t p = destinationDescriptor.getJustTheParam();

	// Check that this source is allowed to be patched to the selected param
	if (p == ::Param::Global::VOLUME_POST_FX) {
		return (soundEditor.currentSound->maySourcePatchToParam(
		            source, ::Param::Global::VOLUME_POST_FX, (ParamManagerForTimeline*)soundEditor.currentParamManager)
		            != PatchCableAcceptance::DISALLOWED
		        || soundEditor.currentSound->maySourcePatchToParam(
		               source, ::Param::Local::VOLUME, (ParamManagerForTimeline*)soundEditor.currentParamManager)
		               != PatchCableAcceptance::DISALLOWED
		        || soundEditor.currentSound->maySourcePatchToParam(
		               source, ::Param::Global::VOLUME_POST_REVERB_SEND,
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
