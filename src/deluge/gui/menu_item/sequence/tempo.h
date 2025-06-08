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

// Individual tempo ratio preset options
class TempoRatioGlobal final : public MenuItem {
public:
	using MenuItem::MenuItem;

	std::string_view getName() const override { return "Global"; }

	bool shouldEnterSubmenu() override { return false; }

	MenuItem* selectButtonPress() override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = static_cast<Clip*>(modelStack->getTimelineCounter());

		if (clip && clip->hasTempoRatio) {
			bool wasActive = currentSong->isClipActive(clip);
			clip->clearTempoRatio();
			display->displayPopup("Global tempo");

			// Ensure clip remains active if it was active before tempo change
			if (wasActive && playbackHandler.isEitherClockActive()) {
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

class TempoRatioHalf final : public MenuItem {
public:
	using MenuItem::MenuItem;

	std::string_view getName() const override { return "1/2 Half"; }

	bool shouldEnterSubmenu() override { return false; }

	MenuItem* selectButtonPress() override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = static_cast<Clip*>(modelStack->getTimelineCounter());

		if (clip) {
			bool wasActive = currentSong->isClipActive(clip);

			clip->setTempoRatio(1, 2);
			display->displayPopup("Half speed");

			// Ensure clip remains active if it was active before tempo change
			if (wasActive && playbackHandler.isEitherClockActive()) {
				currentSong->assertActiveness(modelStack);

				// CRITICAL FIX: Force complete playback restart on ANY ratio change
				// to ensure proper timing state initialization and prevent accumulated drift
				// Stop current playback state cleanly
				clip->expectNoFurtherTicks(currentSong, false);
				// Force a fresh restart with proper ratio-based timing
				clip->setPos(modelStack, clip->lastProcessedPos, true);

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

class TempoRatioDouble final : public MenuItem {
public:
	using MenuItem::MenuItem;

	std::string_view getName() const override { return "2/1 Double"; }

	bool shouldEnterSubmenu() override { return false; }

	MenuItem* selectButtonPress() override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = static_cast<Clip*>(modelStack->getTimelineCounter());

		if (clip) {
			bool wasActive = currentSong->isClipActive(clip);

			clip->setTempoRatio(2, 1);
			display->displayPopup("Double speed");

			// Ensure clip remains active if it was active before tempo change
			if (wasActive && playbackHandler.isEitherClockActive()) {
				currentSong->assertActiveness(modelStack);

				// CRITICAL FIX: Force complete playback restart on ANY ratio change
				// to ensure proper timing state initialization and prevent accumulated drift
				// Stop current playback state cleanly
				clip->expectNoFurtherTicks(currentSong, false);
				// Force a fresh restart with proper ratio-based timing
				clip->setPos(modelStack, clip->lastProcessedPos, true);

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

class TempoRatioThreeFour final : public MenuItem {
public:
	using MenuItem::MenuItem;

	std::string_view getName() const override { return "3/4"; }

	bool shouldEnterSubmenu() override { return false; }

	MenuItem* selectButtonPress() override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = static_cast<Clip*>(modelStack->getTimelineCounter());

		if (clip) {
			bool wasActive = currentSong->isClipActive(clip);

			clip->setTempoRatio(3, 4);
			display->displayPopup("3/4 speed");

			// Ensure clip remains active if it was active before tempo change
			if (wasActive && playbackHandler.isEitherClockActive()) {
				currentSong->assertActiveness(modelStack);

				// CRITICAL FIX: Force complete playback restart on ANY ratio change
				// to ensure proper timing state initialization and prevent accumulated drift
				// Stop current playback state cleanly
				clip->expectNoFurtherTicks(currentSong, false);
				// Force a fresh restart with proper ratio-based timing
				clip->setPos(modelStack, clip->lastProcessedPos, true);

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

class TempoRatioFourThree final : public MenuItem {
public:
	using MenuItem::MenuItem;

	std::string_view getName() const override { return "4/3"; }

	bool shouldEnterSubmenu() override { return false; }

	MenuItem* selectButtonPress() override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = static_cast<Clip*>(modelStack->getTimelineCounter());

		if (clip) {
			bool wasActive = currentSong->isClipActive(clip);

			clip->setTempoRatio(4, 3);
			display->displayPopup("4/3 speed");

			// Ensure clip remains active if it was active before tempo change
			if (wasActive && playbackHandler.isEitherClockActive()) {
				currentSong->assertActiveness(modelStack);

				// CRITICAL FIX: Force complete playback restart on ANY ratio change
				// to ensure proper timing state initialization and prevent accumulated drift
				// Stop current playback state cleanly
				clip->expectNoFurtherTicks(currentSong, false);
				// Force a fresh restart with proper ratio-based timing
				clip->setPos(modelStack, clip->lastProcessedPos, true);

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

			// Ensure clip remains active if it was active before tempo change
			if (wasActive && playbackHandler.isEitherClockActive()) {
				currentSong->assertActiveness(modelStack);

				// CRITICAL FIX: Force complete playback restart on ANY ratio change
				// to ensure proper timing state initialization and prevent accumulated drift
				// Stop current playback state cleanly
				clip->expectNoFurtherTicks(currentSong, false);
				// Force a fresh restart with proper ratio-based timing
				clip->setPos(modelStack, clip->lastProcessedPos, true);

				clip->resumePlayback(modelStack, false);
			}
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

			// Ensure clip remains active if it was active before tempo change
			if (wasActive && playbackHandler.isEitherClockActive()) {
				currentSong->assertActiveness(modelStack);

				// CRITICAL FIX: Force complete playback restart on ANY ratio change
				// to ensure proper timing state initialization and prevent accumulated drift
				// Stop current playback state cleanly
				clip->expectNoFurtherTicks(currentSong, false);
				// Force a fresh restart with proper ratio-based timing
				clip->setPos(modelStack, clip->lastProcessedPos, true);

				clip->resumePlayback(modelStack, false);
			}
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
