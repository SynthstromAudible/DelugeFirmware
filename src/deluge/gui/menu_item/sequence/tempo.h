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
#include "gui/menu_item/number.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/oled.h"
#include "io/debug/log.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"

namespace deluge::gui::menu_item::sequence {
class Tempo final : public Number {
public:
	using Number::Number;

	bool shouldEnterSubmenu() override {
		if (getCurrentUI() == &soundEditor && soundEditor.inNoteRowEditor() && !isUIModeActive(UI_MODE_AUDITIONING)) {
			display->displayPopup("Select Row");
			return false;
		}
		return true;
	}

	void readCurrentValue() override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = static_cast<Clip*>(modelStack->getTimelineCounter());

		if (!clip) {
			// No clip context - use global tempo
			int32_t globalTempo = (int32_t)currentSong->calculateBPM();
			this->setValue(globalTempo);
			return;
		}

		if (clip->hasIndependentTempo) {
			// Set to actual BPM value if clip has override
			float effectiveTempo = clip->getEffectiveTempo();
			this->setValue((int32_t)effectiveTempo);
		}
		else {
			// Initialize to current global tempo when no override is set
			int32_t globalTempo = (int32_t)currentSong->calculateBPM();
			this->setValue(globalTempo);
		}
	}

	void writeCurrentValue() override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = static_cast<Clip*>(modelStack->getTimelineCounter());

		if (!clip) {
			// No clip context available
			D_PRINTLN("TEMPO_MENU_DEBUG: No clip context available");
			display->displayPopup("No clip");
			return;
		}

		int32_t newValue = this->getValue();
		int32_t globalTempo = (int32_t)currentSong->calculateBPM();

		D_PRINTLN("TEMPO_MENU_DEBUG: writeCurrentValue - newValue=%d, globalTempo=%d, clip->hasIndependentTempo=%d, "
		          "clip->repeatCount=%d",
		          newValue, globalTempo, clip->hasIndependentTempo, clip->repeatCount);

		// Debug display
		char debugBuffer[20];
		snprintf(debugBuffer, sizeof(debugBuffer), "Set:%d G:%d", newValue, globalTempo);
		display->displayPopup(debugBuffer);

		if (newValue == globalTempo && !clip->hasIndependentTempo) {
			// Value equals global tempo and no override exists - no change needed
			D_PRINTLN("TEMPO_MENU_DEBUG: No change needed - already at global tempo without override");
			return;
		}
		else if (newValue == globalTempo && clip->hasIndependentTempo) {
			// User set value back to global tempo - clear override
			D_PRINTLN("TEMPO_MENU_DEBUG: Clearing tempo override - returning to global tempo");
			bool wasActive = currentSong->isClipActive(clip);
			D_PRINTLN("TEMPO_MENU_DEBUG: Clip was active: %d", wasActive);

			clip->clearTempoOverride();

			// If clip was active during tempo change, handle reconciliation
			if (wasActive && playbackHandler.isEitherClockActive()) {
				// SMART FIX: Only use reSyncClip if clip hasn't crossed bar boundaries
				// This prevents position resets while still making clips playable
				if (clip->repeatCount == 0) {
					// Clip is still in first loop - safe to resync to global timing
					D_PRINTLN("TEMPO_MENU_DEBUG: Using reSyncClip (repeatCount=0)");
					currentPlaybackMode->reSyncClip(modelStack, true, true);
				}
				else {
					// Clip has crossed bar boundaries - preserve position and just ensure playback
					D_PRINTLN("TEMPO_MENU_DEBUG: Using manual resume (repeatCount=%d)", clip->repeatCount);
					currentSong->assertActiveness(modelStack);
					clip->resumePlayback(modelStack, false); // Don't make sound on resume
				}

				// Debug: Check if clip is still active after clearing tempo
				bool stillActive = currentSong->isClipActive(clip);
				D_PRINTLN("TEMPO_MENU_DEBUG: After clearing - stillActive=%d", stillActive);
				char activeDebug[20];
				snprintf(activeDebug, sizeof(activeDebug), "ClrWas:%d Now:%d R:%d", wasActive ? 1 : 0,
				         stillActive ? 1 : 0, clip->repeatCount);
				display->displayPopup(activeDebug);
			}
		}
		else if (newValue >= 1 && newValue <= 20000) {
			// Valid BPM range - set tempo override
			D_PRINTLN("TEMPO_MENU_DEBUG: Setting tempo override to %d BPM", newValue);
			float bpm = (float)newValue;
			bool wasActive = currentSong->isClipActive(clip);
			D_PRINTLN("TEMPO_MENU_DEBUG: Clip was active: %d", wasActive);

			clip->setTempoOverride(bpm);

			// If clip was active during tempo change, ensure it continues playing with independent timing
			if (wasActive && playbackHandler.isEitherClockActive()) {
				// For clips with independent tempo, don't try to sync to other clips
				// Just ensure the clip is active and resume playback
				D_PRINTLN("TEMPO_MENU_DEBUG: Ensuring clip remains active after setting tempo");
				currentSong->assertActiveness(modelStack);
				clip->resumePlayback(modelStack, false); // Don't make sound on resume

				// Debug: Check if clip is still active after setting tempo
				bool stillActive = currentSong->isClipActive(clip);
				D_PRINTLN("TEMPO_MENU_DEBUG: After setting - stillActive=%d", stillActive);
				char activeDebug[20];
				snprintf(activeDebug, sizeof(activeDebug), "SetWas:%d Now:%d", wasActive ? 1 : 0, stillActive ? 1 : 0);
				display->displayPopup(activeDebug);
			}

			// Debug: Check what was actually stored
			float stored = clip->getEffectiveTempo();
			D_PRINTLN("TEMPO_MENU_DEBUG: Stored tempo: %.1f", stored);
			char debugBuffer2[20];
			snprintf(debugBuffer2, sizeof(debugBuffer2), "Stored:%.1f", stored);
			display->displayPopup(debugBuffer2);
		}
	}

	// BPM range: 1-20000 (matching global tempo system)
	int32_t getMinValue() const override { return 1; }
	int32_t getMaxValue() const override { return 20000; }

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) override {
		return MenuPermission::YES;
	}

	void selectEncoderAction(int32_t offset) override {
		// Update the value based on encoder input
		this->setValue(this->getValue() + offset);

		// Clamp to valid range
		int32_t maxValue = getMaxValue();
		if (this->getValue() > maxValue) {
			this->setValue(maxValue);
		}
		else {
			int32_t minValue = getMinValue();
			if (this->getValue() < minValue) {
				this->setValue(minValue);
			}
		}

		// Call the Number base class method
		Number::selectEncoderAction(offset);
	}

	void drawPixelsForOled() override {
		int32_t value = this->getValue();

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = static_cast<Clip*>(modelStack->getTimelineCounter());

		char buffer[16];
		if (clip && clip->hasIndependentTempo && value != (int32_t)currentSong->calculateBPM()) {
			snprintf(buffer, sizeof(buffer), "%d*", value);
		}
		else {
			snprintf(buffer, sizeof(buffer), "%d", value);
		}

		deluge::hid::display::OLED::main.drawStringCentred(buffer, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX,
		                                                   kTextHugeSizeY);
	}

protected:
	void drawValue() override {
		int32_t value = this->getValue();

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = static_cast<Clip*>(modelStack->getTimelineCounter());

		if (!clip) {
			// No clip context - just show the value
			display->setTextAsNumber(value);
			return;
		}

		int32_t globalTempo = (int32_t)currentSong->calculateBPM();

		// Show an asterisk (*) if the clip has an independent tempo override
		if (clip->hasIndependentTempo && value != globalTempo) {
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "%d*", value);
			display->setText(buffer);
		}
		else {
			display->setTextAsNumber(value);
		}
	}
};
} // namespace deluge::gui::menu_item::sequence
