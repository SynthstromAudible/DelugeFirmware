/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "gui/l10n/l10n.h"
#include "gui/menu_item/integer.h"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/submenu.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/clip/clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include <algorithm>
#include <iterator>

namespace deluge::gui::menu_item::sequence {

// Helper function to restart playback after ratio change - prevents position contamination
inline void restartPlaybackForRatioChange(Clip* clip, ModelStackWithTimelineCounter* modelStack, bool wasActive) {
	if (wasActive && playbackHandler.isEitherClockActive()) {
		D_PRINTLN("TEMPO_RATIO_DEBUG: Starting playback restart");
		currentSong->assertActiveness(modelStack);

		// CRITICAL FIX: Preserve musical position within clip rather than trying to convert global positions
		// The issue was that setPos() expects clip-domain positions, not global-domain positions

		// Get current live position in the clip's current timing domain
		uint32_t currentLivePos = clip->getLivePos();
		D_PRINTLN("TEMPO_RATIO_DEBUG: Current live pos: %d", currentLivePos);

		// Calculate position as fraction of loop to preserve musical location
		uint32_t loopLength = clip->loopLength;
		uint32_t positionInLoop = currentLivePos % loopLength;
		D_PRINTLN("TEMPO_RATIO_DEBUG: Loop length: %d, Position in loop: %d", loopLength, positionInLoop);

		D_PRINTLN("TEMPO_RATIO_DEBUG: Calling expectNoFurtherTicks()");
		clip->expectNoFurtherTicks(currentSong, false);

		// Use the current position within the loop - this preserves musical position
		// regardless of timing domain changes
		D_PRINTLN("TEMPO_RATIO_DEBUG: Calling setPos() with position: %d", positionInLoop);
		clip->setPos(modelStack, positionInLoop, true);

		D_PRINTLN("TEMPO_RATIO_DEBUG: Calling resumePlayback()");
		clip->resumePlayback(modelStack, false);
		D_PRINTLN("TEMPO_RATIO_DEBUG: Playback restart completed");
	}
	else {
		D_PRINTLN("TEMPO_RATIO_DEBUG: Skipping restart - wasActive: %d, clockActive: %d", wasActive,
		          playbackHandler.isEitherClockActive());
	}
}

// Unified tempo ratio preset class - replaces 5 individual classes with one parameterized class
class TempoRatioPreset final : public MenuItem {
private:
	uint16_t numerator_;
	uint16_t denominator_;
	std::string_view displayName_;
	std::string_view popupMessage_;
	bool isGlobal_;

public:
	// Constructor for regular ratio presets
	TempoRatioPreset(std::string_view name, std::string_view popup, uint16_t num, uint16_t den)
	    : MenuItem(deluge::l10n::String::EMPTY_STRING), numerator_(num), denominator_(den), displayName_(name),
	      popupMessage_(popup), isGlobal_(false) {}

	// Constructor for global tempo option
	explicit TempoRatioPreset(std::string_view name)
	    : MenuItem(deluge::l10n::String::EMPTY_STRING), numerator_(1), denominator_(1), displayName_(name),
	      popupMessage_("Global tempo"), isGlobal_(true) {}

	std::string_view getName() const override { return displayName_; }
	bool shouldEnterSubmenu() override { return false; }

	MenuItem* selectButtonPress() override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = static_cast<Clip*>(modelStack->getTimelineCounter());

		if (clip) {
			bool wasActive = currentSong->isClipActive(clip);

			// Set ratio or clear to global
			if (isGlobal_) {
				clip->clearTempoRatio();
			}
			else {
				clip->setTempoRatio(numerator_, denominator_);
			}

			display->displayPopup(popupMessage_.data());

			// Restart playback for non-global ratios to prevent position contamination
			if (!isGlobal_) {
				restartPlaybackForRatioChange(clip, modelStack, wasActive);
			}
			// For global, just resume normally without restart
			else if (wasActive && playbackHandler.isEitherClockActive()) {
				currentSong->assertActiveness(modelStack);
				clip->resumePlayback(modelStack, false);
			}
		}

		// Refresh parent menu to update title
		if (soundEditor.getCurrentMenuItem() && soundEditor.getCurrentMenuItem()->isSubmenu()) {
			soundEditor.getCurrentMenuItem()->readValueAgain();
		}

		return NO_NAVIGATION; // Stay in menu after setting ratio
	}
};

// Custom ratio numerator input
class TempoRatioNumerator final : public Integer {
public:
	using Integer::Integer;

	std::string_view getName() const override { return "Numerator"; }

	int32_t getMinValue() const override { return 1; }
	int32_t getMaxValue() const override { return 32; }

	void readCurrentValue() override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = static_cast<Clip*>(modelStack->getTimelineCounter());

		if (clip && clip->hasTempoRatio) {
			this->setValue(clip->tempoRatioNumerator);
		}
		else {
			this->setValue(1);
		}
	}

	void writeCurrentValue() override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = static_cast<Clip*>(modelStack->getTimelineCounter());

		if (clip) {
			uint16_t numerator = this->getValue();
			uint16_t denominator = clip->hasTempoRatio ? clip->tempoRatioDenominator : 1;

			bool wasActive = currentSong->isClipActive(clip);

			clip->setTempoRatio(numerator, denominator);

			// Restart playback to prevent position contamination from ratio change
			restartPlaybackForRatioChange(clip, modelStack, wasActive);
		}

		// Trigger a redraw to update any dynamic titles in parent menus
		if (display->haveOLED()) {
			renderUIsForOled();
		}
	}
};

// Custom ratio denominator input
class TempoRatioDenominator final : public Integer {
public:
	using Integer::Integer;

	std::string_view getName() const override { return "Denominator"; }

	int32_t getMinValue() const override { return 1; }
	int32_t getMaxValue() const override { return 32; }

	void readCurrentValue() override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = static_cast<Clip*>(modelStack->getTimelineCounter());

		if (clip && clip->hasTempoRatio) {
			this->setValue(clip->tempoRatioDenominator);
		}
		else {
			this->setValue(1);
		}
	}

	void writeCurrentValue() override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = static_cast<Clip*>(modelStack->getTimelineCounter());

		if (clip) {
			uint16_t numerator = clip->hasTempoRatio ? clip->tempoRatioNumerator : 1;
			uint16_t denominator = this->getValue();

			bool wasActive = currentSong->isClipActive(clip);

			clip->setTempoRatio(numerator, denominator);

			// Restart playback to prevent position contamination from ratio change
			restartPlaybackForRatioChange(clip, modelStack, wasActive);
		}

		// Trigger a redraw to update any dynamic titles in parent menus
		if (display->haveOLED()) {
			renderUIsForOled();
		}
	}
};

// Main tempo ratio submenu
class TempoRatio final : public Submenu {
public:
	using Submenu::Submenu;

	bool shouldEnterSubmenu() override {
		if (getCurrentUI() == &soundEditor && soundEditor.inNoteRowEditor() && !isUIModeActive(UI_MODE_AUDITIONING)) {
			display->displayPopup("Select Row");
			return false;
		}
		return true;
	}

	void renderOLED() override {
		// Get current clip status for dynamic title
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = static_cast<Clip*>(modelStack->getTimelineCounter());

		// Create dynamic title showing current ratio
		char titleBuffer[32];
		if (clip && clip->hasTempoRatio) {
			snprintf(titleBuffer, sizeof(titleBuffer), "TEMPO: %d:%d", clip->tempoRatioNumerator,
			         clip->tempoRatioDenominator);
		}
		else {
			snprintf(titleBuffer, sizeof(titleBuffer), "TEMPO: Global");
		}

		// Draw the custom title
		deluge::hid::display::OLED::main.drawScreenTitle(titleBuffer);

		// Use standard submenu drawing for menu items
		drawPixelsForOled();

		deluge::hid::display::OLED::markChanged();
	}
};
} // namespace deluge::gui::menu_item::sequence
