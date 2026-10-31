/*
 * Copyright © 2026 Rudement (Initial Author), Sean Ditny (Follow-up Author)
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

#include "gui/views/session/track_link.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"

namespace deluge::gui::track_link {

namespace {

// Session selection is temporary. Persistent locks belong to Output::trackLocked
// and must not be cleared when this state or the sound menu is dismissed.
struct SessionSelection {
	bool active = false;
	Song* song = nullptr;
	Output* output = nullptr;
	// Retained pointers are compared against owned objects before dereferencing.
	Clip* knobClip = nullptr;
	bool savedSongAffectEntire = false;
};
SessionSelection selection;

// Opening the Session sound menu borrows the song's current clip and, for kits,
// the clip's Affect Entire setting. Restore those when the menu closes.
struct MenuContext {
	Clip* clip = nullptr;
	Clip* savedCurrentClip = nullptr;
	InstrumentClip* kitClip = nullptr;
	bool savedClipAffectEntire = false;
};
MenuContext menu;

// A timeline counter can also be the song. Find membership before treating it
// as a Clip, and never dereference a retained clip pointer to validate it.
Clip* findOwnedClip(Song& song, TimelineCounter const* timeline) {
	if ((timeline == nullptr) || timeline == &song) {
		return nullptr;
	}
	for (Clip* clip : AllClips::everywhere(&song)) {
		if (static_cast<TimelineCounter*>(clip) == timeline) {
			return clip;
		}
	}
	return nullptr;
}

bool songOwnsOutput(Song& song, Output const* output) {
	for (Output* candidate = song.firstOutput; candidate != nullptr; candidate = candidate->next) {
		if (candidate == output) {
			return true;
		}
	}
	return false;
}

bool outputStillExists(Output const* output) {
	return (currentSong != nullptr) && currentSong == selection.song && songOwnsOutput(*currentSong, output);
}

Clip* pickClipForKnobs(Clip* preferred) {
	Clip* fallback = nullptr;
	for (Clip* clip : AllClips::everywhere(currentSong)) {
		if (clip->output != selection.output) {
			continue;
		}
		if (clip == preferred) {
			return clip;
		}
		if (fallback == nullptr) {
			fallback = clip;
		}
	}
	return fallback;
}

void retarget(Clip* preferred) {
	// A null clip leaves the knobs inert, which is the safe outcome when the track has nothing to act on.
	selection.knobClip = pickClipForKnobs(preferred);
	view.setActiveModControllableTimelineCounter(selection.knobClip);
	uiNeedsRendering(&sessionView);
}

/// Track Edit begins with no target, which clears the gold-knob rings. Repaint them explicitly after Song view has
/// redrawn: its normal indicator update preserves any centre-detent blink that belonged to the previous target.
void refreshGoldKnobIndicators() {
	for (int32_t knob = 0; knob < NUM_LEVEL_INDICATORS; knob++) {
		indicator_leds::stopBlinkingKnobIndicator(knob);
	}
	view.setKnobIndicatorLevels();
	view.setModLedStates();
}

/// Puts back what opening the Track Edit menu borrowed. Safe to call when the menu is not open.
void restoreAfterMenu() {
	if (menu.clip == nullptr) {
		return;
	}
	if ((currentSong != nullptr) && currentSong == selection.song) {
		if (auto* clip = findOwnedClip(*currentSong, menu.kitClip);
		    (clip != nullptr) && clip->type == ClipType::INSTRUMENT) {
			static_cast<InstrumentClip*>(clip)->affectEntire = menu.savedClipAffectEntire;
		}
		if ((menu.savedCurrentClip == nullptr) || (findOwnedClip(*currentSong, menu.savedCurrentClip) != nullptr)) {
			currentSong->setCurrentClip(menu.savedCurrentClip);
		}
	}
	menu = {};
}

/// Refresh the selected track name.
void refreshScreen() {
	if (display->haveOLED() && getRootUI() == &sessionView) {
		renderUIsForOled();
	}
}

void beginSelection() {
	selection.active = true;
	selection.song = currentSong;
	selection.output = nullptr;

	// Song-wide knobs would otherwise be what the gold knobs reach. Put the song's flag back on the way out.
	selection.savedSongAffectEntire = currentSong->affectEntire;
	currentSong->affectEntire = false;

	// Until a track is picked the knobs do nothing, so a stray turn cannot change anything.
	view.setActiveModControllableTimelineCounter(nullptr);
	view.blinkOn = true;
	uiNeedsRendering(&sessionView);
	refreshScreen();
}

} // namespace

bool enabled() {
	return runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::TrackEdit);
}

bool modeIsOn() {
	return selection.active && enabled();
}

void toggleLock(Output* output) {
	if (!enabled() || output == nullptr) {
		return;
	}
	output->setTrackLocked(!output->isTrackLocked());
	view.setModLedStates();
	uiNeedsRendering(getRootUI());
	display->displayPopup(output->isTrackLocked() ? deluge::l10n::get(deluge::l10n::String::STRING_FOR_TRACK_LOCK_ON)
	                                              : deluge::l10n::get(deluge::l10n::String::STRING_FOR_TRACK_LOCK_OFF));
}

bool editingSelectedTrack() {
	if (!enabled() || currentSong == nullptr) {
		return false;
	}
	if (getRootUI() == &sessionView && (selectedOutput() != nullptr)) {
		return true;
	}
	// The actual edit target determines the LED, independently of Session selection.
	auto* timeline = view.activeModControllableModelStack.getTimelineCounterAllowNull();
	auto* clip = findOwnedClip(*currentSong, timeline);
	return (clip != nullptr) && clip->output->isTrackLocked();
}

void toggleLockForClip(Clip* clip) {
	if (clip == nullptr) {
		return;
	}
	toggleLock(clip->output);
	view.setActiveModControllableTimelineCounter(clip);
}

bool isSelected(Output const* output) {
	return output != nullptr && selectedOutput() == output;
}

void exitMode() {
	if (!selection.active) {
		return;
	}
	selectedOutput(); // Resolve any replacement before releasing the transient edit scope.
	restoreAfterMenu();
	if (outputStillExists(selection.output)) {
		selection.output->setTrackEditActive(false);
	}
	if ((currentSong != nullptr) && currentSong == selection.song) {
		currentSong->affectEntire = selection.savedSongAffectEntire;
	}
	selection = {};
	view.blinkOn = false;

	if (getRootUI() == &sessionView) {
		view.setActiveModControllableTimelineCounter(currentSong);
		// Stop the Affect-entire LED flashing and put the knob LEDs back as song view has them.
		indicator_leds::setLedState(IndicatorLED::AFFECT_ENTIRE, currentSong->affectEntire, false);
		view.setModLedStates();
		uiNeedsRendering(&sessionView, 0xFFFFFFFF, 0xFFFFFFFF);
		refreshScreen();
	}
}

void selectOutput(Output* output, Clip* preferred) {
	if ((currentSong == nullptr) || !sessionView.gridTrackRowEnabled() || !songOwnsOutput(*currentSong, output)) {
		return;
	}
	if (selection.active && selection.song != currentSong) {
		exitMode();
	}
	if (!selection.active) {
		beginSelection();
	}
	if (selection.output != output) {
		if (auto* previous = selectedOutput()) {
			previous->setTrackEditActive(false);
		}
	}
	selection.output = output;
	output->setTrackEditActive(true);

	retarget(preferred);
	refreshScreen();
	refreshGoldKnobIndicators();
}

bool openMenu() {
	if (!selection.active) {
		return false;
	}
	if (menu.clip != nullptr) {
		return true;
	}
	if ((selection.output != nullptr) && !outputStillExists(selection.output)) {
		selection.output = nullptr;
	}
	if (selection.output == nullptr) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_PICK_A_TRACK));
		return true;
	}
	// MIDI and CV tracks have no sound parameters for the mode to carry.
	if (selection.output->type != OutputType::SYNTH && selection.output->type != OutputType::KIT
	    && selection.output->type != OutputType::AUDIO) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_TRACK_NO_MENU));
		return true;
	}

	Clip* clip = pickClipForKnobs(selection.knobClip);
	if (clip == nullptr) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_TRACK_NO_CLIP));
		return true;
	}

	menu.savedCurrentClip = currentSong->getCurrentClip();
	currentSong->setCurrentClip(clip);
	menu.clip = clip;

	if (selection.output->type == OutputType::KIT) {
		menu.kitClip = static_cast<InstrumentClip*>(clip);
		menu.savedClipAffectEntire = menu.kitClip->affectEntire;
		menu.kitClip->affectEntire = true;
		soundEditor.setupKitGlobalFXMenu = true;
	}

	if (!soundEditor.setup(clip)) {
		soundEditor.setupKitGlobalFXMenu = false;
		restoreAfterMenu();
		return true;
	}
	display->setNextTransitionDirection(1);
	openUI(&soundEditor);
	return true;
}

Output* selectedOutput() {
	bool owned = outputStillExists(selection.output);
	if (!owned && currentSong != nullptr && currentSong == selection.song) {
		// Preset replacement moves this clip to a new output while the menu stays open.
		// Only search clips when rebinding; pulse/render queries keep the usual output-only lookup.
		if (auto* clip = findOwnedClip(*currentSong, selection.knobClip)) {
			selection.output = clip->output;
			owned = outputStillExists(selection.output);
		}
	}
	return modeIsOn() && owned ? selection.output : nullptr;
}

Clip* selectedClip() {
	if (selectedOutput() == nullptr) {
		return nullptr;
	}
	return pickClipForKnobs(selection.knobClip);
}

bool isMenuOpen() {
	return menu.clip != nullptr;
}

} // namespace deluge::gui::track_link
