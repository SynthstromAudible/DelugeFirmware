/*
 * Copyright © 2026 Rudement
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

#include "model/song/track_link.h"
#include "definitions_cxx.hpp"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "model/clip/clip.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_manager.h"
#include "processing/engines/audio_engine.h"

namespace params = deluge::modulation::params;

namespace deluge::model::track_link {

namespace {

bool sModeOn = false;
Output* sOutput = nullptr;
bool sDrumRow = false;
bool sPropagating = false;
bool sSavedAffectEntire = false;
// The clip the gold knobs were last aimed at, so the menu opens on the same one. Only ever compared against clips
// found on the track, never dereferenced on its own, because it may have been deleted since.
Clip* sKnobClip = nullptr;

// Track Edit menu (SELECT). While it is open the song's current clip is the clip being edited, because the sound
// editor and its menu items read everything through getCurrentClip(). What it was before goes back on the way out.
Clip* sMenuClip = nullptr;
Clip* sMenuSavedCurrentClip = nullptr;
// A kit's whole-kit / drum-row choice is read by the sound editor from the clip's own Affect-entire, which is saved
// with the song, so it is borrowed for the menu and given back.
InstrumentClip* sMenuKitClip = nullptr;
bool sMenuSavedClipAffectEntire = false;
uint32_t sLastKitReminder = 0;
// About two seconds of audio at 44.1 kHz: a pause longer than this counts as a new knob gesture.
constexpr uint32_t kKitReminderGapSamples = 88200;

/// Calls f(clip) for every clip on `output`: session clips, then arrangement-only clips.
template <typename F>
void forEachClipOnOutput(Output* output, F&& f) {
	for (int32_t i = 0; i < currentSong->sessionClips.getNumElements(); i++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(i);
		if (clip->output == output) {
			f(clip);
		}
	}
	for (int32_t i = 0; i < output->clipInstances.getNumElements(); i++) {
		ClipInstance* instance = output->clipInstances.getElement(i);
		if (instance->clip && instance->clip->isArrangementOnlyClip()) {
			f(instance->clip);
		}
	}
}

bool outputStillExists(Output const* output) {
	for (Output* o = currentSong->firstOutput; o; o = o->next) {
		if (o == output) {
			return true;
		}
	}
	return false;
}

/// A clip the knobs can be pointed at. For a kit being edited by drum row, the clip must actually have a row for
/// the selected drum, otherwise the knobs would have nothing to act on.
bool clipIsUsable(Clip* clip) {
	if (clip->output != sOutput) {
		return false;
	}
	if (sOutput->type == OutputType::KIT && sDrumRow) {
		Kit* kit = static_cast<Kit*>(sOutput);
		if (!kit->selectedDrum || clip->type != ClipType::INSTRUMENT) {
			return false;
		}
		return static_cast<InstrumentClip*>(clip)->getNoteRowForDrum(kit->selectedDrum) != nullptr;
	}
	return true;
}

Clip* pickClipForKnobs(Clip* preferred) {
	if (preferred && clipIsUsable(preferred)) {
		return preferred;
	}
	Clip* found = nullptr;
	forEachClipOnOutput(sOutput, [&](Clip* clip) {
		if (!found && clipIsUsable(clip)) {
			found = clip;
		}
	});
	return found;
}

void retarget(Clip* preferred) {
	// A null clip leaves the knobs inert, which is the safe outcome when the track has nothing to act on.
	sKnobClip = pickClipForKnobs(preferred);
	view.setActiveModControllableTimelineCounter(sKnobClip);
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
	if (!sMenuClip) {
		return;
	}
	if (sMenuKitClip) {
		sMenuKitClip->affectEntire = sMenuSavedClipAffectEntire;
		sMenuKitClip = nullptr;
	}
	currentSong->setCurrentClip(sMenuSavedCurrentClip);
	sMenuClip = nullptr;
	sMenuSavedCurrentClip = nullptr;
}

/// Writes `value` into the same parameter of `clip`, if that clip has it.
bool writeToClip(Clip* clip, Drum* drum, params::Kind kind, ModelStackWithAutoParam const* source, int32_t value,
                 bool shouldLogAction, bool mayDeleteNodesInLinearRun, bool doMPEMode) {
	NoteRow* noteRow = nullptr;
	int32_t noteRowId = 0;
	ParamManagerForTimeline* paramManager;

	if (drum) {
		// Kit row: the same drum's row in the other clip holds the parameters.
		if (clip->type != ClipType::INSTRUMENT) {
			return false;
		}
		noteRow = static_cast<InstrumentClip*>(clip)->getNoteRowForDrum(drum, &noteRowId);
		if (!noteRow) {
			return false;
		}
		paramManager = &noteRow->paramManager;
	}
	else {
		paramManager = &clip->paramManager;
	}

	if (!paramManager->containsAnyMainParamCollections()) {
		return false;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStack = setupModelStackWithThreeMainThingsIncludingNoteRow(
	    modelStackMemory, currentSong, clip, noteRowId, noteRow, source->modControllable, paramManager);

	ModelStackWithAutoParam* target = (kind == params::Kind::PATCHED)
	                                      ? modelStack->getPatchedAutoParamFromId(source->paramId)
	                                      : modelStack->getUnpatchedAutoParamFromId(source->paramId);
	if (!target || !target->autoParam) {
		return false;
	}
	if (!target->autoParam->isAutomated() && target->autoParam->getCurrentValue() == value) {
		return true;
	}

	target->autoParam->setCurrentValueInResponseToUserInput(value, target, shouldLogAction, -1,
	                                                        mayDeleteNodesInLinearRun, doMPEMode);
	return true;
}

} // namespace

bool modeIsOn() {
	return sModeOn;
}

bool isSelected(Output const* output) {
	return sModeOn && sOutput != nullptr && sOutput == output;
}

bool targetsDrumRow(Output const* output) {
	return isSelected(output) && output->type == OutputType::KIT && sDrumRow;
}

char const* statusText() {
	if (!sModeOn) {
		return nullptr;
	}
	if (!sOutput) {
		return "PICK A TRACK";
	}
	if (sOutput->type == OutputType::KIT) {
		return sDrumRow ? "DRUM ROW" : "WHOLE KIT";
	}
	return "TRACK EDIT";
}

namespace {
/// Song view's OLED shows statusText() while the mode is on, so redraw it whenever that text can have changed.
void refreshScreen() {
	if (display->haveOLED() && getRootUI() == &sessionView) {
		renderUIsForOled();
	}
}
} // namespace

void toggleMode() {
	if (sModeOn) {
		exitMode();
		return;
	}
	if (!runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::TrackEdit)) {
		return;
	}

	sModeOn = true;
	sOutput = nullptr;
	sDrumRow = false;

	// Song-wide knobs would otherwise be what the gold knobs reach. Put the song's flag back on the way out.
	sSavedAffectEntire = currentSong->affectEntire;
	currentSong->affectEntire = false;

	// Until a track is picked the knobs do nothing, so a stray turn cannot change anything.
	view.setActiveModControllableTimelineCounter(nullptr);
	view.blinkOn = true;
	uiNeedsRendering(&sessionView);
	refreshScreen();
}

void exitMode() {
	if (!sModeOn) {
		return;
	}
	restoreAfterMenu();
	sModeOn = false;
	sOutput = nullptr;
	sKnobClip = nullptr;
	sDrumRow = false;
	sPropagating = false;
	view.blinkOn = false;

	currentSong->affectEntire = sSavedAffectEntire;

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
	if (!sModeOn || !output) {
		return;
	}
	sOutput = output;

	// A kit starts out on its selected drum row if it has one, otherwise on the whole kit.
	sDrumRow = false;
	if (output->type == OutputType::KIT) {
		Kit* kit = static_cast<Kit*>(output);
		sDrumRow = kit->selectedDrum && kit->selectedDrum->type == DrumType::SOUND;
	}

	retarget(preferred);
	refreshScreen();
	refreshGoldKnobIndicators();
}

bool openMenu() {
	if (!sModeOn) {
		return false;
	}
	if (sMenuClip) {
		return true;
	}
	if (sOutput && !outputStillExists(sOutput)) {
		sOutput = nullptr;
	}
	if (!sOutput) {
		display->displayPopup("PICK A TRACK");
		return true;
	}
	// MIDI and CV tracks have no sound parameters for the mode to carry.
	if (sOutput->type != OutputType::SYNTH && sOutput->type != OutputType::KIT && sOutput->type != OutputType::AUDIO) {
		display->displayPopup("NO MENU");
		return true;
	}

	// The clip the knobs are on, if it is still on the track and still usable; otherwise the first one that is.
	Clip* clip = nullptr;
	forEachClipOnOutput(sOutput, [&](Clip* c) {
		if (!clip && c == sKnobClip && clipIsUsable(c)) {
			clip = c;
		}
	});
	if (!clip) {
		clip = pickClipForKnobs(nullptr);
	}
	if (!clip) {
		display->displayPopup(sDrumRow ? "NO DRUM" : "NO CLIP");
		return true;
	}

	sMenuSavedCurrentClip = currentSong->getCurrentClip();
	currentSong->setCurrentClip(clip);
	sMenuClip = clip;

	if (sOutput->type == OutputType::KIT) {
		sMenuKitClip = static_cast<InstrumentClip*>(clip);
		sMenuSavedClipAffectEntire = sMenuKitClip->affectEntire;
		sMenuKitClip->affectEntire = !sDrumRow;
		soundEditor.setupKitGlobalFXMenu = !sDrumRow;
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

Clip* menuClip() {
	return sMenuClip;
}

bool returnFromMenu() {
	if (!sMenuClip) {
		return false;
	}
	Clip* clip = sMenuClip;
	restoreAfterMenu();
	if (!sModeOn || !sOutput) {
		return false;
	}
	// The clip came from the track a moment ago and nothing in the menu deletes clips, so it is safe to aim at.
	retarget(clip);
	view.blinkOn = true;
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kFastFlashTime);
	refreshScreen();
	refreshGoldKnobIndicators();
	return true;
}

void toggleKitTarget() {
	if (!sModeOn || !sOutput || sOutput->type != OutputType::KIT) {
		return;
	}
	Kit* kit = static_cast<Kit*>(sOutput);

	if (sDrumRow) {
		sDrumRow = false;
	}
	else {
		if (!kit->selectedDrum || kit->selectedDrum->type != DrumType::SOUND) {
			display->displayPopup("NO DRUM");
			return;
		}
		sDrumRow = true;
	}

	retarget(nullptr);
	refreshScreen();
	refreshGoldKnobIndicators();
	display->displayPopup(sDrumRow ? "DRUM ROW" : "WHOLE KIT");
}

void propagate(int32_t value, ModelStackWithAutoParam const* source, bool shouldLogAction,
               bool mayDeleteNodesInLinearRun, bool doMPEMode) {
	if (!sModeOn || sPropagating || !sOutput) {
		return;
	}
	if (getRootUI() != &sessionView) {
		return;
	}
	if (!source->autoParam || !source->paramCollection || !source->timelineCounterIsSet()) {
		return;
	}
	if (!outputStillExists(sOutput)) {
		sOutput = nullptr;
		return;
	}

	// Patched and unpatched parameters have fixed ids that mean the same thing in every clip. Modulation depths
	// are indexed by cable, which can differ from clip to clip, so they are left alone, as are expression and MIDI.
	params::Kind kind = source->paramCollection->getParamKind();
	if (kind != params::Kind::PATCHED && kind != params::Kind::UNPATCHED_SOUND
	    && kind != params::Kind::UNPATCHED_GLOBAL) {
		return;
	}

	NoteRow* sourceRow = source->getNoteRowAllowNull();
	Drum* drum = sourceRow ? sourceRow->drum : nullptr;
	if (sourceRow && !drum) {
		return;
	}

	// The edit must have been made on one of the picked track's clips.
	TimelineCounter* origin = source->getTimelineCounter();
	bool originIsOnTrack = false;
	forEachClipOnOutput(sOutput, [&](Clip* clip) {
		if (static_cast<TimelineCounter*>(clip) == origin) {
			originIsOnTrack = true;
		}
	});
	if (!originIsOnTrack) {
		return;
	}

	sPropagating = true;
	forEachClipOnOutput(sOutput, [&](Clip* clip) {
		if (static_cast<TimelineCounter*>(clip) == origin) {
			return;
		}
		writeToClip(clip, drum, kind, source, value, shouldLogAction, mayDeleteNodesInLinearRun, doMPEMode);
	});
	sPropagating = false;

	// On a kit the knobs act on either the whole kit or one drum row, and it is easy to lose track of which. Say so
	// at the start of every knob gesture (not on every click, which would hide the value readout).
	if (sOutput->type == OutputType::KIT) {
		uint32_t now = AudioEngine::audioSampleTimer;
		if (now - sLastKitReminder > kKitReminderGapSamples) {
			display->displayPopup(sDrumRow ? "DRUM ROW" : "WHOLE KIT");
		}
		sLastKitReminder = now;
	}
}

} // namespace deluge::model::track_link
