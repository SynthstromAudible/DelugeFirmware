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

#include "model/song/song.h"
#include "definitions_cxx.hpp"
#include "dsp/reverb/reverb.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_session_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "io/midi/device_specific/specific_midi_device.h"
#include "io/midi/midi_engine.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_clip_existence.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/sample/sample_recorder.h"
#include "model/scale/preset_scales.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/clip_iterators.h"
#include "model/voice/voice_sample.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "processing/audio_output.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"
#include "util/lookuptables/lookuptables.h"
#include <cstring>
#include <new>
#include <stdint.h>

extern "C" {}

namespace params = deluge::modulation::params;

/// Do not call in static/global constructors, song won't exist yet
Clip* getCurrentClip() {
	return currentSong->getCurrentClip();
}

/// Do not call in static/global constructors, song won't exist yet
InstrumentClip* getCurrentInstrumentClip() {
	Clip* currentClip = currentSong->getCurrentClip();
	if (currentClip->type == ClipType::INSTRUMENT) {
		return (InstrumentClip*)currentClip;
	}
	return nullptr;
}

/// Do not call in static/global constructors, song won't exist yet
AudioClip* getCurrentAudioClip() {
	Clip* currentClip = currentSong->getCurrentClip();
	if (currentClip->type == ClipType::AUDIO) {
		return (AudioClip*)currentClip;
	}
	return nullptr;
}

/// Do not call in static/global constructors, song won't exist yet
Output* getCurrentOutput() {
	return currentSong->getCurrentClip()->output;
}

/// Do not call in static/global constructors, song won't exist yet
Kit* getCurrentKit() {
	Clip* currentClip = currentSong->getCurrentClip();
	if (currentClip->output->type == OutputType::KIT) {
		return static_cast<Kit*>(currentClip->output);
	}
	return nullptr;
}

/// Do not call in static/global constructors, song won't exist yet
Instrument* getCurrentInstrument() {
	auto output = currentSong->getCurrentClip()->output;
	if (output == nullptr) {
		return nullptr;
	}
	auto outputType = output->type;
	if (outputType == OutputType::AUDIO || outputType == OutputType::NONE) {
		return nullptr;
	}

	return static_cast<Instrument*>(output);
}

/// Do not call in static/global constructors, song won't exist yet
OutputType getCurrentOutputType() {
	return currentSong->getCurrentClip()->output->type;
}

using namespace deluge;

Song::Song() : backedUpParamManagers(sizeof(BackedUpParamManager)) {
	outputClipInstanceListIsCurrentlyInvalid = false;
	insideWorldTickMagnitude = FlashStorage::defaultMagnitude;
	insideWorldTickMagnitudeOffsetFromBPM = 0;
	syncScalingClip = NULL;
	currentClip = NULL;
	slot = 32767;
	subSlot = -1;

	xScroll[NAVIGATION_CLIP] = 0;
	xScroll[NAVIGATION_ARRANGEMENT] = 0;
	xScrollForReturnToSongView = 0;

	xZoom[NAVIGATION_CLIP] = increaseMagnitude(kDefaultClipLength, insideWorldTickMagnitude - kDisplayWidthMagnitude);
	xZoom[NAVIGATION_ARRANGEMENT] = kDefaultArrangerZoom << insideWorldTickMagnitude;
	xZoomForReturnToSongView = xZoom[NAVIGATION_CLIP];

	tripletsOn = false;

	affectEntire = false;

	fillModeActive = false;

	key.modeNotes.fromScaleNotes(presetScaleNotes[MAJOR_SCALE]);

	swingAmount = 0;

	swingInterval = FlashStorage::defaultSwingInterval;

	songViewYScroll = 1 - kDisplayHeight;
	arrangementYScroll = -kDisplayHeight;

	anyClipsSoloing = false;
	anyOutputsSoloingInArrangement = false;

	firstOutput = NULL;
	firstHibernatingInstrument = NULL;
	hibernatingMIDIInstrument = NULL;

	lastClipInstanceEnteredStartPos = -1;
	arrangerAutoScrollModeActive = false;

	paramsInAutomationMode = false;

	// default to off
	midiLoopback = false;

	// Setup reverb temp variables
	reverbRoomSize = (float)30 / 50;
	reverbDamp = (float)36 / 50;
	reverbWidth = 1;
	reverbPan = 0;
	reverbSidechainVolume = getParamFromUserValue(params::STATIC_SIDECHAIN_VOLUME, -1);
	reverbSidechainShape = -601295438;
	reverbSidechainSync = SYNC_LEVEL_8TH;
	AudioEngine::reverb.setModel(deluge::dsp::Reverb::Model::MUTABLE);

	// setup base compressor gain to match 1.0
	globalEffectable.compressor.setBaseGain(0.85);

	// initialize automation arranger view variables
	lastSelectedParamID = kNoSelection;
	lastSelectedParamKind = params::Kind::NONE;
	lastSelectedParamShortcutX = kNoSelection;
	lastSelectedParamShortcutY = kNoSelection;
	lastSelectedParamArrayPosition = 0;
	// end initialize of automation arranger view variables

	masterTransposeInterval = 0;

	dirPath.set("SONGS");
}

Song::~Song() {

	// Delete existing Clips, if any
	for (int32_t c = 0; c < sessionClips.getNumElements(); c++) {
		if (!(c & 31)) {                              // Exact number here not fine-tuned
			AudioEngine::routineWithClusterLoading(); // -----------------------------------
		}

		Clip* clip = sessionClips.getClipAtIndex(c);
		deleteClipObject(clip, true, InstrumentRemoval::NONE);
	}

	for (int32_t c = 0; c < arrangementOnlyClips.getNumElements(); c++) {
		if (!(c & 31)) {                              // Exact number here not fine-tuned
			AudioEngine::routineWithClusterLoading(); // -----------------------------------
		}

		Clip* clip = arrangementOnlyClips.getClipAtIndex(c);
		deleteClipObject(clip, true, InstrumentRemoval::NONE);
	}

	AudioEngine::logAction("s4");
	AudioEngine::routineWithClusterLoading(); // -----------------------------------

	// Free all ParamManagers which are backed up. The actual vector memory containing all the BackedUpParamManager
	// objects will be freed by the Vector destructor
	deleteAllBackedUpParamManagers(false); // Don't empty vector - its destructor will do that

	deleteAllOutputs(&firstOutput);
	deleteAllOutputs((Output**)&firstHibernatingInstrument);

	deleteHibernatingMIDIInstrument();
}

#include "gui/menu_item/integer_range.h"
#include "gui/menu_item/key_range.h"
#include "timers_interrupts/timers_interrupts.h"
extern gui::menu_item::IntegerRange defaultTempoMenu;
extern gui::menu_item::IntegerRange defaultSwingAmountMenu;
extern gui::menu_item::KeyRange defaultKeyMenu;

Clip* Song::getCurrentClip() {
	return currentClip;
}

void Song::setupDefault() {
	inClipMinderViewOnLoad = true;

	seedRandom();

	setBPM(defaultTempoMenu.getRandomValueInRange(), false);
	swingAmount = defaultSwingAmountMenu.getRandomValueInRange() - 50;
	key.rootNote = defaultKeyMenu.getRandomValueInRange();

	// Do scale
	int32_t whichScale = FlashStorage::defaultScale;
	if (whichScale == PRESET_SCALE_RANDOM) {
		whichScale = random(NUM_PRESET_SCALES - 1);
	}
	else if (whichScale == PRESET_SCALE_NONE) {
		whichScale = 0; // Major. Still need the *song*, (as opposed to the Clip) to have a scale
	}
	else {
		if (whichScale >= OFFSET_5_NOTE_SCALE) {
			// remove offset for 5 note scales
			whichScale = FIRST_5_NOTE_SCALE_INDEX + whichScale - OFFSET_5_NOTE_SCALE;
		}
		else if (whichScale >= OFFSET_6_NOTE_SCALE) {
			// remove offset for 6 note scales
			whichScale = FIRST_6_NOTE_SCALE_INDEX + whichScale - OFFSET_6_NOTE_SCALE;
		}
		if (whichScale >= NUM_PRESET_SCALES) {
			// Index is out of bounds, so reset to 0
			whichScale = 0;
		}
	}

	key.modeNotes.fromScaleNotes(presetScaleNotes[whichScale]);
}

void Song::deleteAllOutputs(Output** prevPointer) {

	while (*prevPointer) {
		AudioEngine::logAction("s6");
		AudioEngine::routineWithClusterLoading(); // -----------------------------------
		Output* toDelete = *prevPointer;
		*prevPointer = toDelete->next;

		void* toDealloc = dynamic_cast<void*>(toDelete);
		toDelete->~Output();
		delugeDealloc(toDealloc);
	}
}

void Song::deleteAllBackedUpParamManagers(bool shouldAlsoEmptyVector) {
	for (int32_t i = 0; i < backedUpParamManagers.getNumElements(); i++) {
		AudioEngine::routineWithClusterLoading(); // -----------------------------------
		BackedUpParamManager* backedUp = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(i);

		backedUp->~BackedUpParamManager();
	}
	if (shouldAlsoEmptyVector) {
		backedUpParamManagers.empty();
	}
}

void Song::deleteAllBackedUpParamManagersWithClips() {

	// We'll aim to repeatedly find the longest runs possible of ones with Clips, to delete all in one go

	for (int32_t i = 0; i < backedUpParamManagers.getNumElements(); i++) {
		BackedUpParamManager* firstBackedUp = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(i);

		// If no Clip, just go onto the next
		if (!firstBackedUp->clip) {
			continue;
		}

		ModControllableAudio* modControllable = firstBackedUp->modControllable;
		int32_t searchedUpToAndIncluding = i;

keepSearchingForward:

		// If still here, this is the first one with a Clip for this ModControllable. Find the end of this
		// ModControllable's ones
		int32_t endIThisModControllable =
		    backedUpParamManagers.search((uint32_t)modControllable + 4, GREATER_OR_EQUAL,
		                                 searchedUpToAndIncluding + 1); // Search just by first word

		// But if that next one, for the next ModControllable, also has a Clip, we can just keep looking forwards
		// til we find one with no Clip
		if (endIThisModControllable < backedUpParamManagers.getNumElements()) {
			BackedUpParamManager* thisNextBackedUp =
			    (BackedUpParamManager*)backedUpParamManagers.getElementAddress(endIThisModControllable);
			if (thisNextBackedUp->clip) {
				modControllable = thisNextBackedUp->modControllable;
				searchedUpToAndIncluding = endIThisModControllable;
				goto keepSearchingForward;
			}
		}

		// Cool, we've found a big long run. Delete them
		for (int32_t j = i; j < endIThisModControllable; j++) {
			BackedUpParamManager* backedUp = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(j);

			AudioEngine::routineWithClusterLoading(); // -----------------------------------

			backedUp->~BackedUpParamManager();
		}

		int32_t numToDelete = endIThisModControllable - i;

		backedUpParamManagers.deleteAtIndex(i, numToDelete);

		// i will increment, which is fine, cos we've already determined that the next element (if there is one) has
		// no Clip, so we can skip it
	}
}

bool Song::mayDoubleTempo() {
	return ((timePerTimerTickBig >> 33) > kMinTimePerTimerTick);
}

// Returns true if a Clip created
bool Song::ensureAtLeastOneSessionClip() {
	// If no Clips added, make just one blank one - we can't have none!
	if (!sessionClips.getNumElements()) {

		void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(InstrumentClip));
		InstrumentClip* firstClip = new (memory) InstrumentClip(this);

		sessionClips.insertClipAtIndex(firstClip, 0);

		firstClip->loopLength = kDefaultClipLength << insideWorldTickMagnitude;

		ParamManager newParamManager; // Deliberate don't set up.

		ReturnOfConfirmPresetOrNextUnlaunchedOne result;
		result.error = storageManager.initSD(); // Still want this here?
		if (result.error != Error::NONE) {
			goto couldntLoad;
		}

		result.error = Browser::currentDir.set("SYNTHS");
		if (result.error != Error::NONE) {
			goto couldntLoad;
		}

		result = loadInstrumentPresetUI.findAnUnlaunchedPresetIncludingWithinSubfolders(NULL, OutputType::SYNTH,
		                                                                                Availability::ANY);

		Instrument* newInstrument;

		if (result.error == Error::NONE) {
			String newPresetName;
			result.fileItem->getDisplayNameWithoutExtension(&newPresetName);
			result.error = storageManager.loadInstrumentFromFile(this, firstClip, OutputType::SYNTH, false,
			                                                     &newInstrument, &result.fileItem->filePointer,
			                                                     &newPresetName, &Browser::currentDir);

			Browser::emptyFileItems();
			if (result.error != Error::NONE) {
				goto couldntLoad;
			}
		}
		else {
couldntLoad:
			newInstrument = storageManager.createNewInstrument(OutputType::SYNTH, &newParamManager);

			// TODO: Clean this up and get rid of infinite loop traps (proper error recovery)

			// If that failed (really unlikely) we're really screwed
			if (!newInstrument) {
				display->displayError(Error::INSUFFICIENT_RAM);
				while (1) {}
			}

			result.error = newInstrument->dirPath.set("SYNTHS");
			if (result.error != Error::NONE) {
				display->displayError(result.error);
				while (1) {}
			}

			result.error = newInstrument->name.set("0");
			if (result.error != Error::NONE) {
				display->displayError(result.error);
				while (1) {}
			}

			((SoundInstrument*)newInstrument)->setupAsDefaultSynth(&newParamManager);
			display->displayError(result.error); // E.g. show the CARD error.
		}

		newInstrument->loadAllAudioFiles(true);

		firstClip->setAudioInstrument(newInstrument, this, true, &newParamManager);
		// TODO: error checking?
		addOutput(newInstrument);

		currentClip = firstClip;

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(firstClip);

		if (playbackHandler.isEitherClockActive() && currentPlaybackMode == &session) {
			session.reSyncClip(modelStackWithTimelineCounter, true);
		}

		newInstrument->setActiveClip(modelStackWithTimelineCounter);

		return true;
	}
	return false;
}

/* Chromatic or in-key transposition.
   If chromatic == true, offset is measured in semitones.
   if not, offset is measured in steps in the current modeNotes array.
   */
void Song::transposeAllScaleModeClips(int32_t offset, bool chromatic) {
	int32_t semitones;
	int32_t yNoteOnBottomRow, ys2;

	if (!chromatic) {
		int8_t octaves;
		int8_t rootIndex;
		MusicalKey oldKey = key;
		uint8_t noteCount = key.modeNotes.count();

		if (offset < 0) {
			octaves = ((offset + 1) / noteCount) - 1;
			rootIndex = ((offset + 1) % noteCount) + noteCount - 1;
		}
		else {
			octaves = (offset / noteCount);
			rootIndex = offset % noteCount;
		}
		int8_t newModeRoot = key.modeNotes[rootIndex];

		semitones = 12 * octaves + newModeRoot;
		rotateMusicalMode(offset);

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

		MusicalKey newKey = key;
		newKey.rootNote += semitones;

		ClipArray* clipArray = &sessionClips;
		for (uint8_t iClipTypes = 0; iClipTypes < 2; iClipTypes++) {
			if (iClipTypes == 1) {
				clipArray = &arrangementOnlyClips;
			}
			for (int32_t c = 0; c < clipArray->getNumElements(); c++) {
				Clip* clip = clipArray->getClipAtIndex(c);
				if (clip->type != ClipType::INSTRUMENT) {
					continue;
				}
				if (clip->output->type == OutputType::KIT) {
					continue;
				}

				InstrumentClip* instrumentClip = (InstrumentClip*)clip;
				ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
				    modelStack->addTimelineCounter(instrumentClip);
				if (instrumentClip->isScaleModeClip()) {
					if (clip->output->type == OutputType::MIDI_OUT
					    && ((NonAudioInstrument*)clip->output)->channel == MIDI_CHANNEL_TRANSPOSE) {
						// Must not transpose MIDI clips that are routed to transpose, ie note rows
						// stay exactly the same.
						// Just have to scroll the clip so that the change in song root note
						// does not visually move the notes on the grid.
						yNoteOnBottomRow = getYNoteFromYVisual(instrumentClip->yScroll, true, oldKey);
						instrumentClip->yScroll = getYVisualFromYNote(yNoteOnBottomRow, true, newKey);
					}
					else {
						instrumentClip->transpose(semitones, modelStackWithTimelineCounter);
					}
				}
			}
		}
		key = newKey;
	}
	else {
		semitones = offset;
		transposeAllScaleModeClips(semitones);
	}
}

void Song::transposeAllScaleModeClips(int32_t interval) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	for (InstrumentClip* instrumentClip : InstrumentClips::everywhere(this)) {
		if (instrumentClip->output->type == OutputType::KIT) {
			continue;
		}

		if (instrumentClip->isScaleModeClip()) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
			    modelStack->addTimelineCounter(instrumentClip);
			instrumentClip->transpose(interval, modelStackWithTimelineCounter);
		}
	}

	key.rootNote += interval;
}

bool Song::anyScaleModeClips() {
	for (InstrumentClip* instrumentClip : InstrumentClips::everywhere(this)) {
		if (instrumentClip->isScaleModeClip()) {
			return true;
		}
	}
	return false;
}

void Song::setRootNote(int32_t newRootNote, InstrumentClip* clipToAvoidAdjustingScrollFor) {

	int32_t oldRootNote = key.rootNote;
	key.rootNote = newRootNote;

	int32_t oldNumModeNotes = key.modeNotes.count();
	NoteSet notesWithinOctavePresent;

	for (InstrumentClip* instrumentClip : InstrumentClips::everywhere(this)) {
		if (instrumentClip->isScaleModeClip()) {
			instrumentClip->seeWhatNotesWithinOctaveArePresent(notesWithinOctavePresent, key.rootNote, this);
		}
	}

	bool previousScaleFits = true;
	if (getCurrentPresetScale() >= NUM_PRESET_SCALES) {
		// We don't want to reuse "OTHER SCALE", we want the Deluge to guess a new scale
		previousScaleFits = false;
	}
	// Check if the previously set scale could fit the notes present in the clips
	// If so, we need no check at all, we can directly go back to previous scale safely
	else {
		for (int32_t i = 1; i < 12; i++) {
			if (notesWithinOctavePresent.has(i)) {
				bool checkPassed = false;
				for (int32_t n = 1; n < key.modeNotes.count(); n++) {
					if (key.modeNotes[n] == i) {
						checkPassed = true;
						break;
					}
				}
				if (!checkPassed) {
					previousScaleFits = false;
					break;
				}
			}
		}
	}

	if (!previousScaleFits) {
		// Determine the majorness or minorness of the scale
		int32_t majorness = 0;

		// The 3rd is the main indicator of majorness, to my ear
		if (notesWithinOctavePresent.has(4)) {
			majorness++;
		}
		if (notesWithinOctavePresent.has(3)) {
			majorness--;
		}

		// If it's still a tie, try the 2nd, 6th, and 7th to help us decide
		if (majorness == 0) {
			if (notesWithinOctavePresent.has(1)) {
				majorness--;
			}
			if (notesWithinOctavePresent.has(8)) {
				majorness--;
			}
			if (notesWithinOctavePresent.has(9)) {
				majorness++;
			}
		}

		bool moreMajor = (majorness >= 0);

		key.modeNotes.clear();
		key.modeNotes.add(0);

		// 2nd
		addMajorDependentModeNotes(1, true, notesWithinOctavePresent);

		// 3rd
		addMajorDependentModeNotes(3, moreMajor, notesWithinOctavePresent);

		// 4th, 5th
		if (notesWithinOctavePresent.has(5)) {
			key.modeNotes.add(5);
			if (notesWithinOctavePresent.has(6)) {
				key.modeNotes.add(6);
				if (notesWithinOctavePresent.has(7)) {
					key.modeNotes.add(7);
				}
			}
			else {
				key.modeNotes.add(7);
			}
		}
		else {
			if (notesWithinOctavePresent.has(6)) {
				if (notesWithinOctavePresent.has(7) || moreMajor) {
					key.modeNotes.add(6);
					key.modeNotes.add(7);
				}
				else {
					key.modeNotes.add(5);
					key.modeNotes.add(6);
				}
			}
			else {
				key.modeNotes.add(5);
				key.modeNotes.add(7);
			}
		}

		// 6th
		addMajorDependentModeNotes(8, moreMajor, notesWithinOctavePresent);

		// 7th
		addMajorDependentModeNotes(10, moreMajor, notesWithinOctavePresent);
	}

	// Adjust scroll for Clips with the scale. Crudely - not as high quality as happens for the Clip being processed
	// in enterScaleMode();
	int32_t numMoreNotes = (int32_t)key.modeNotes.count() - oldNumModeNotes;

	// Compensation for the change in root note itself
	int32_t rootNoteChange = key.rootNote - oldRootNote;
	int32_t rootNoteChangeEffect =
	    rootNoteChange * (12 - key.modeNotes.count())
	    / 12; // I wasn't quite sure whether this should use key.modeNotes.count() or oldNumModeNotes

	for (InstrumentClip* instrumentClip : InstrumentClips::everywhere(this)) {
		if (instrumentClip != clipToAvoidAdjustingScrollFor && instrumentClip->isScaleModeClip()) {
			// Compensation for the change in number of mode notes
			int32_t oldScrollRelativeToRootNote = instrumentClip->yScroll - oldRootNote;
			int32_t numOctaves = oldScrollRelativeToRootNote / oldNumModeNotes;

			instrumentClip->yScroll += numMoreNotes * numOctaves + rootNoteChangeEffect;
		}
	}
}

// Sets up a mode-note, optionally specifying that we prefer it a semitone higher, although this may be overridden
// by what actual note is present
void Song::addMajorDependentModeNotes(uint8_t i, bool preferHigher, NoteSet& notesWithinOctavePresent) {
	// If lower one present...
	if (notesWithinOctavePresent.has(i)) {
		// If higher one present as well...
		if (notesWithinOctavePresent.has(i + 1)) {
			key.modeNotes.add(i);
			key.modeNotes.add(i + 1);
		}
		// Or if just the lower one
		else {
			key.modeNotes.add(i);
		}
	}
	// Or, if lower one absent...
	else {
		// We probably want the higher one
		if (notesWithinOctavePresent.has(i + 1) || preferHigher) {
			key.modeNotes.add(i + 1);
			// Or if neither present and we prefer the lower one, do that
		}
		else {
			key.modeNotes.add(i);
		}
	}
}

bool Song::yNoteIsYVisualWithinOctave(int32_t yNote, int32_t yVisualWithinOctave) {
	int32_t yNoteWithinOctave = getYNoteWithinOctaveFromYNote(yNote);
	return (key.modeNotes[yVisualWithinOctave] == yNoteWithinOctave);
}

uint8_t Song::getYNoteWithinOctaveFromYNote(int32_t yNote) {
	uint16_t yNoteRelativeToRoot = yNote - key.rootNote + 132;
	int32_t yNoteWithinOctave = yNoteRelativeToRoot % 12;
	return yNoteWithinOctave;
}

bool Song::modeContainsYNote(int32_t yNote) {
	int32_t yNoteWithinOctave = (uint16_t)(yNote - key.rootNote + 132) % 12;
	return modeContainsYNoteWithinOctave(yNoteWithinOctave);
}

bool Song::modeContainsYNoteWithinOctave(uint8_t yNoteWithinOctave) {
	for (int32_t i = 0; i < key.modeNotes.count(); i++) {
		if (key.modeNotes[i] == yNoteWithinOctave) {
			return true;
		}
	}
	return false;
}

uint8_t Song::getYNoteIndexInMode(int32_t yNote) {
	uint8_t yNoteWithinOctave = (uint8_t)(yNote - key.rootNote + 132) % 12;
	for (uint8_t i = 0; i < key.modeNotes.count(); i++) {
		if (key.modeNotes[i] == yNoteWithinOctave) {
			return i;
		}
	}
	return 255;
}

/* Moves the intervals in the current modeNotes by some number of steps
    in a circular way. For example, starting in major
    and going up one step (change == 1) results in Dorian:
    major =             [0,2,4,5,7,9,11](12)
    intervals            [2 2 1 2 2 2  1]
    intervals rotated    [2 1 2 2 2 1  2]
    new mode (Dorian)=  [0,2,3,5,7,9,10](12)
    */
void Song::rotateMusicalMode(int8_t change) {
	int8_t j;
	int16_t newRoot;
	int8_t changes[12] = {0};
	uint8_t noteCount = key.modeNotes.count();

	int8_t steps = (change % noteCount + noteCount) % noteCount;
	newRoot = key.modeNotes[steps];
	for (int8_t i = 0; i < noteCount; i++) {
		changes[i] = (key.modeNotes[(i + steps) % noteCount] - newRoot) - key.modeNotes[i];
		if (i >= noteCount - steps) {
			changes[i] += 12;
		}
	}

	replaceMusicalMode(changes, false);
}

/* Changes the musical mode of all scale mode clips, by
   supplying an array of deltas in semitones */
void Song::replaceMusicalMode(int8_t changes[], bool affectMIDITranspose) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	for (InstrumentClip* instrumentClip : InstrumentClips::everywhere(this)) {
		if (!affectMIDITranspose && instrumentClip->output->type == OutputType::MIDI_OUT
		    && ((NonAudioInstrument*)instrumentClip->output)->channel == MIDI_CHANNEL_TRANSPOSE) {
			// Must not transpose MIDI clips that are routed to transpose.
			continue;
		}
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(instrumentClip);

		instrumentClip->replaceMusicalMode(key.modeNotes.count(), changes, modelStackWithTimelineCounter);
	}

	key.applyChanges(changes);
}

// Flattens or sharpens a given note-within-octave in the current scale
void Song::changeMusicalMode(uint8_t yVisualWithinOctave, int8_t change) {

	int8_t changes[12] = {0};
	changes[yVisualWithinOctave] += change;

	replaceMusicalMode(changes, true);
}

bool Song::isYNoteAllowed(int32_t yNote, bool inKeyMode) {
	if (!inKeyMode) {
		return true;
	}
	return modeContainsYNoteWithinOctave(getYNoteWithinOctaveFromYNote(yNote));
}

int32_t Song::getYVisualFromYNote(int32_t yNote, bool inKeyMode) {
	return getYVisualFromYNote(yNote, inKeyMode, key);
}

int32_t Song::getYVisualFromYNote(int32_t yNote, bool inKeyMode, const MusicalKey& key) {
	if (!inKeyMode) {
		return yNote;
	}
	int32_t yNoteRelativeToRoot = yNote - key.rootNote;
	int32_t yNoteWithinOctave = (uint16_t)(yNoteRelativeToRoot + 120) % 12;

	int32_t octave = (uint16_t)(yNoteRelativeToRoot + 120 - yNoteWithinOctave) / 12 - 10;

	int32_t yVisualWithinOctave = 0;
	for (int32_t i = 0; i < key.modeNotes.count() && key.modeNotes[i] <= yNoteWithinOctave; i++) {
		yVisualWithinOctave = i;
	}
	return yVisualWithinOctave + octave * key.modeNotes.count() + key.rootNote;
}

int32_t Song::getYNoteFromYVisual(int32_t yVisual, bool inKeyMode) {
	return getYNoteFromYVisual(yVisual, inKeyMode, key);
}

int32_t Song::getYNoteFromYVisual(int32_t yVisual, bool inKeyMode, const MusicalKey& key) {
	if (!inKeyMode) {
		return yVisual;
	}
	int32_t yVisualRelativeToRoot = yVisual - key.rootNote;
	int32_t yVisualWithinOctave = yVisualRelativeToRoot % key.modeNotes.count();
	if (yVisualWithinOctave < 0) {
		yVisualWithinOctave += key.modeNotes.count();
	}

	int32_t octave = (yVisualRelativeToRoot - yVisualWithinOctave) / key.modeNotes.count();

	int32_t yNoteWithinOctave = key.modeNotes[yVisualWithinOctave];
	return yNoteWithinOctave + octave * 12 + key.rootNote;
}

bool Song::mayMoveModeNote(int16_t yVisualWithinOctave, int8_t newOffset) {
	// If it's the root note and moving down, special criteria
	if (yVisualWithinOctave == 0 && newOffset == -1) {
		return (key.modeNotes[key.modeNotes.count() - 1]
		        < 11); // May ony move down if the top note in scale isn't directly below (at semitone 11)
	}

	else {
		return ((newOffset == 1 &&                          // We're moving up and
		         key.modeNotes[yVisualWithinOctave] < 11 && // We're not already at the top of the scale and
		         (yVisualWithinOctave == key.modeNotes.count() - 1
		          || // Either we're the top note, so don't need to check for a higher neighbour
		          key.modeNotes[yVisualWithinOctave + 1]
		              > key.modeNotes[yVisualWithinOctave] + 1) // Or the next note up has left us space to move up
		         )
		        || (newOffset == -1 && // We're moving down and
		            key.modeNotes[yVisualWithinOctave] > 1
		            && // We're not already at the bottom of the scale (apart from the root note) and
		            key.modeNotes[yVisualWithinOctave - 1]
		                < key.modeNotes[yVisualWithinOctave] - 1 // The next note down has left us space to move down
		            ));
	}
}

void Song::removeYNoteFromMode(int32_t yNoteWithinOctave) {

	// Why did I get rid of this again?

	/*
	int32_t i = 0;
	while (i < key.modeNotes.count) {
	    if (key.modeNotes[i] == yNoteWithinOctave) goto foundIt;
	    i++;
	}

	return;

	foundIt:

	key.modeNotes.count--;
	while (i < key.modeNotes.count) {
	    key.modeNotes[i] = key.modeNotes[i + 1];
	    i++;
	}

	*/

	for (InstrumentClip* instrumentClip : InstrumentClips::everywhere(this)) {
		instrumentClip->noteRemovedFromMode(yNoteWithinOctave, this);
	}
}

bool Song::areAllClipsInSectionPlaying(int32_t section) {
	if (getAnyClipsSoloing()) {
		return false;
	}

	for (int32_t l = 0; l < sessionClips.getNumElements(); l++) {
		Clip* clip = sessionClips.getClipAtIndex(l);
		if (clip->section == section && !isClipActive(clip)) {
			return false;
		}
	}

	return true;
}

uint32_t Song::getInputTickScale() {
	if (!syncScalingClip) {
		return 3;
	}
	uint32_t inputTickScale = syncScalingClip->loopLength;
	while (!(inputTickScale & 1)) {
		inputTickScale >>= 1;
	}
	return inputTickScale;
}

Clip* Song::getSyncScalingClip() {
	return syncScalingClip;
}

void Song::setInputTickScaleClip(Clip* clip) {
	uint32_t oldScale = getInputTickScale();
	syncScalingClip = clip;
	inputTickScalePotentiallyJustChanged(oldScale);
}

void Song::inputTickScalePotentiallyJustChanged(uint32_t oldScale) {
	uint32_t newScale = getInputTickScale();

	// Chances are we'll have to change the input tick magnitude to account for the magnitudinal difference between
	// say a 1-based and 9-based time
	if ((float)newScale * 1.41 < oldScale) {
		do {
			newScale *= 2;
			insideWorldTickMagnitude++;
			// insideWorldTickMagnitudeOffsetFromBPM--;
		} while ((float)newScale * 1.41 < oldScale);
	}

	else {
		while ((float)oldScale * 1.41 < newScale) {
			oldScale *= 2;
			insideWorldTickMagnitude--;
			// insideWorldTickMagnitudeOffsetFromBPM++;
		}
	}

	// We then do a very similar process again, to calculate insideWorldTickMagnitudeOffsetFromBPM in such a way
	// that, say, 8th-notes will always appear about the same length to the user

	oldScale = 3;
	newScale = getInputTickScale();
	insideWorldTickMagnitudeOffsetFromBPM = 0;

	if ((float)newScale * 1.41 < oldScale) {
		do {
			newScale *= 2;
			// insideWorldTickMagnitude++;
			insideWorldTickMagnitudeOffsetFromBPM--;
		} while ((float)newScale * 1.41 < oldScale);
	}

	else {
		while ((float)oldScale * 1.41 < newScale) {
			oldScale *= 2;
			// insideWorldTickMagnitude--;
			insideWorldTickMagnitudeOffsetFromBPM++;
		}
	}
}

// This is a little bit of an ugly hack - normally this is true, but we'll briefly set it to false while doing that
// "revert" that we do when (re)lengthening a Clip
bool allowResyncingDuringClipLengthChange = true;

void Song::changeFillMode(bool on) {
	fillModeActive = on;
	// we peek fill notes when fill is held so need to re render rows
	uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
	if ((runtimeFeatureSettings.get(RuntimeFeatureSettingType::SyncScalingAction)
	     == RuntimeFeatureStateSyncScalingAction::Fill)) {
		indicator_leds::setLedState(IndicatorLED::SYNC_SCALING, on);
	}
}

// If action is NULL, that means this is being called as part of an undo, so don't do any extra stuff.
// Currently mayReSyncClip is only set to false in a call that happens when we've just finished recording that Clip.
void Song::setClipLength(Clip* clip, uint32_t newLength, Action* action, bool mayReSyncClip) {
	uint32_t oldLength = clip->loopLength;

	if (clip == syncScalingClip) {
		uint32_t oldScale = getInputTickScale();
		clip->loopLength = newLength;
		inputTickScalePotentiallyJustChanged(oldScale);
	}
	else {
		clip->loopLength = newLength;
	}

	if (action) {
		action->recordClipLengthChange(clip,
		                               oldLength); // Records just the simple fact that clip->length has changed
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithSong(modelStackMemory, this)->addTimelineCounter(clip);

	if (newLength < oldLength) {
		clip->lengthChanged(modelStack, oldLength, action);
	}

	clip->output->clipLengthChanged(clip, oldLength);

	if (playbackHandler.isEitherClockActive() && isClipActive(clip)) {

		if (mayReSyncClip) {
			if (allowResyncingDuringClipLengthChange) {
				currentPlaybackMode->reSyncClip(modelStack, false,
				                                false); // Don't "resume" - we're going to do that below.
			}
		}
		else {
			playbackHandler.expectEvent(); // Is this maybe redundant now that Arranger has a reSyncClip()?
		}

		clip->resumePlayback(modelStack);
	}
}

void Song::doubleClipLength(InstrumentClip* clip, Action* action) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithSong(modelStackMemory, this)->addTimelineCounter(clip);

	int32_t oldLength = clip->loopLength;

	uint32_t oldScale = getInputTickScale();

	clip->increaseLengthWithRepeats(modelStack, oldLength << 1, IndependentNoteRowLengthIncrease::DOUBLE, false,
	                                action);

	if (clip == syncScalingClip) {
		inputTickScalePotentiallyJustChanged(oldScale);
	}

	clip->output->clipLengthChanged(clip, oldLength);

	if (playbackHandler.isEitherClockActive() && isClipActive(clip)) {
		currentPlaybackMode->reSyncClip(modelStack);
	}
}

Clip* Song::getClipWithOutput(Output* output, bool mustBeActive, Clip* excludeClip) {

	// For each clip in session and arranger for specific Output
	int32_t numElements = sessionClips.getNumElements();
	bool doingArrangementClips = false;
	// TODO: This should be rewritten with AllClips as well, but it's less obvious
	// so leaving or later.
traverseClips:
	for (int32_t c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = sessionClips.getClipAtIndex(c);
			if (clip->output != output) {
				continue;
			}
		}
		else {
			ClipInstance* clipInstance = output->clipInstances.getElement(c);
			if (!clipInstance->clip) {
				continue;
			}
			if (!clipInstance->clip->isArrangementOnlyClip()) {
				continue;
			}
			clip = clipInstance->clip;
		}

		if (clip == excludeClip) {
			continue;
		}

		if (mustBeActive && !isClipActive(clip)) {
			continue;
		}
		return clip;
	}
	if (!doingArrangementClips) {
		doingArrangementClips = true;
		numElements = output->clipInstances.getNumElements();
		goto traverseClips;
	}

	return NULL;
}

Clip* Song::getSessionClipWithOutput(Output* output, int32_t requireSection, Clip* excludeClip, int32_t* clipIndex,
                                     bool excludePendingOverdubs) {

	// For each clip in session
	for (int32_t c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		if (clip->output == output) {
			if (clip == excludeClip) {
				continue;
			}
			if (requireSection != -1 && clip->section != requireSection) {
				continue;
			}
			if (excludePendingOverdubs && clip->isPendingOverdub) {
				continue;
			}

			if (clipIndex) {
				*clipIndex = c;
			}
			return clip;
		}
	}

	return NULL;
}

Clip* Song::getNextSessionClipWithOutput(int32_t offset, Output* output, Clip* prevClip) {

	int32_t oldIndex = -1;
	if (prevClip) {
		oldIndex = sessionClips.getIndexForClip(prevClip);
	}

	if (oldIndex == -1) {
		if (offset < 0) {
			oldIndex = sessionClips.getNumElements();
		}
	}

	int32_t newIndex = oldIndex;
	while (true) {
		newIndex += offset;
		if (newIndex == -1 || newIndex == sessionClips.getNumElements()) {
			return NULL;
		}

		Clip* clip = sessionClips.getClipAtIndex(newIndex);
		if (clip->output == output) {
			return clip;
		}
	}
}

void Song::writeTemplateSong(const char* templatePath) {
	name.set("DEFAULT");
	Error error = storageManager.createXMLFile(templatePath, smSerializer, false, false);
	if (error != Error::NONE) {
		return;
	}
	writeToFile(storageManager);
	GetSerializer().closeFileAfterWriting(templatePath, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<song\n",
	                                      "\n</song>\n");
}

void Song::writeToFile(StorageManager& bdsm) {

	setupClipIndexesForSaving();
	Serializer& writer = GetSerializer();
	writer.writeOpeningTagBeginning("song");

	writer.writeFirmwareVersion();
	writer.writeEarliestCompatibleFirmwareVersion("4.1.0-alpha");

	writer.writeAttribute("previewNumPads", "144");

	writer.write("\n");
	writer.printIndents();
	writer.write("preview=\"");

	for (int32_t y = 0; y < kDisplayHeight; y++) {
		for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
			for (int32_t colour = 0; colour < 3; colour++) {
				char buffer[3];
				byteToHex(PadLEDs::imageStore[y][x][colour], buffer);
				writer.write(buffer);
			}
		}
	}
	writer.write("\"");

	if (getRootUI() == &arrangerView) {
		writer.writeAttribute("inArrangementView", 1);
		goto weAreInArrangementEditorOrInClipInstance;
	}

	if (lastClipInstanceEnteredStartPos != -1) {
		writer.writeAttribute("currentTrackInstanceArrangementPos", lastClipInstanceEnteredStartPos);

weAreInArrangementEditorOrInClipInstance:
		writer.writeAttribute("xScrollSongView", xScrollForReturnToSongView);
		writer.writeAttribute("xZoomSongView", xZoomForReturnToSongView);
	}

	writer.writeAttribute("arrangementAutoScrollOn", arrangerAutoScrollModeActive);

	writer.writeAttribute("xScroll", xScroll[NAVIGATION_CLIP]);
	writer.writeAttribute("xZoom", xZoom[NAVIGATION_CLIP]);
	writer.writeAttribute("yScrollSongView", songViewYScroll);
	writer.writeAttribute("yScrollArrangementView", arrangementYScroll);
	writer.writeAttribute("xScrollArrangementView", xScroll[NAVIGATION_ARRANGEMENT]);
	writer.writeAttribute("xZoomArrangementView", xZoom[NAVIGATION_ARRANGEMENT]);
	writer.writeAttribute("timePerTimerTick", timePerTimerTickBig >> 32);
	writer.writeAttribute("timerTickFraction", (uint32_t)timePerTimerTickBig);
	writer.writeAttribute("rootNote", key.rootNote);
	writer.writeAttribute("inputTickMagnitude", getInputTickMagnitude());
	writer.writeAttribute("swingAmount", swingAmount);
	writer.writeAbsoluteSyncLevelToFile(this, "swingInterval", (SyncLevel)swingInterval, true);

	if (tripletsOn) {
		writer.writeAttribute("tripletsLevel", tripletsLevel);
	}

	writer.writeAttribute("affectEntire", affectEntire);
	writer.writeAttribute("activeModFunction", globalEffectable.modKnobMode);

	if (lastSelectedParamID != kNoSelection) {
		writer.writeAttribute("lastSelectedParamID", lastSelectedParamID);
		writer.writeAttribute("lastSelectedParamKind", util::to_underlying(lastSelectedParamKind));
		writer.writeAttribute("lastSelectedParamShortcutX", lastSelectedParamShortcutX);
		writer.writeAttribute("lastSelectedParamShortcutY", lastSelectedParamShortcutY);
		writer.writeAttribute("lastSelectedParamArrayPosition", lastSelectedParamArrayPosition);
	}

	globalEffectable.writeAttributesToFile(writer, false);

	// Community Firmware parameters (always write them after the official ones, just before closing the parent tag)
	writer.writeAttribute("midiLoopback", midiLoopback);
	writer.writeAttribute("songGridScrollX", songGridScrollX);
	writer.writeAttribute("songGridScrollY", songGridScrollY);
	writer.writeAttribute("sessionLayout", sessionLayout);

	writer.writeOpeningTagEnd(); // -------------------------------------------------------------- Attributes end

	writer.writeOpeningTag("modeNotes");
	for (int32_t i = 0; i < key.modeNotes.count(); i++) {
		writer.writeTag("modeNote", key.modeNotes[i]);
	}
	writer.writeClosingTag("modeNotes");

	writer.writeOpeningTagBeginning("reverb");
	deluge::dsp::Reverb::Model model = AudioEngine::reverb.getModel();
	uint32_t roomSize = AudioEngine::reverb.getRoomSize() * (uint32_t)2147483648u;
	uint32_t damping = AudioEngine::reverb.getDamping() * (uint32_t)2147483648u;
	uint32_t width = AudioEngine::reverb.getWidth() * (uint32_t)2147483648u;

	roomSize = std::min(roomSize, (uint32_t)2147483647);
	damping = std::min(damping, (uint32_t)2147483647);
	width = std::min(width, (uint32_t)2147483647);

	writer.writeAttribute("roomSize", roomSize);
	writer.writeAttribute("dampening", damping);
	writer.writeAttribute("width", width);
	writer.writeAttribute("pan", AudioEngine::reverbPan);
	writer.writeAttribute("model", util::to_underlying(model));
	writer.writeOpeningTagEnd();

	writer.writeOpeningTagBeginning("compressor");
	writer.writeAttribute("attack", AudioEngine::reverbSidechain.attack);
	writer.writeAttribute("release", AudioEngine::reverbSidechain.release);
	writer.writeAttribute("volume", AudioEngine::reverbSidechainVolume);
	writer.writeAttribute("shape", AudioEngine::reverbSidechainShape);
	writer.writeAttribute("syncLevel", AudioEngine::reverbSidechain.syncLevel);
	writer.closeTag();

	writer.writeClosingTag("reverb");

	globalEffectable.writeTagsToFile(writer, NULL, false);

	int32_t* valuesForOverride = paramsInAutomationMode ? unautomatedParamValues : NULL;

	writer.writeOpeningTagBeginning("songParams");
	GlobalEffectableForClip::writeParamAttributesToFile(writer, &paramManager, true, valuesForOverride);
	writer.writeOpeningTagEnd();
	GlobalEffectableForClip::writeParamTagsToFile(writer, &paramManager, true, valuesForOverride);
	writer.writeClosingTag("songParams");

	writer.writeOpeningTag("instruments");
	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		thisOutput->writeToFile(bdsm, NULL, this);
	}
	writer.writeClosingTag("instruments");

	writer.writeOpeningTag("sections");
	for (int32_t s = 0; s < kMaxNumSections; s++) {
		if (true || sections[s].launchMIDICommand.containsSomething() || sections[s].numRepetitions) {
			writer.writeOpeningTagBeginning("section");
			writer.writeAttribute("id", s, false);
			if (true || sections[s].numRepetitions) {
				writer.writeAttribute("numRepeats", sections[s].numRepetitions, false);
			}
			if (sections[s].launchMIDICommand.containsSomething()) {
				// Annoyingly, I used one-off tag names here, rather than it conforming to what the LearnedMIDI
				// class now uses.
				writer.writeAttribute("midiCommandChannel", sections[s].launchMIDICommand.channelOrZone, false);
				writer.writeAttribute("midiCommandNote", sections[s].launchMIDICommand.noteOrCC, false);
				if (sections[s].launchMIDICommand.device) {
					writer.writeOpeningTagEnd();
					sections[s].launchMIDICommand.device->writeReferenceToFile(writer, "midiCommandDevice");
					writer.writeClosingTag("section");
					continue; // We now don't need to close the tag.
				}
			}
			writer.closeTag();
		}
	}
	writer.writeClosingTag("sections");

	writer.writeOpeningTag("sessionClips");
	for (int32_t c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);
		clip->writeToFile(writer, this);
	}
	writer.writeClosingTag("sessionClips");

	if (arrangementOnlyClips.getNumElements()) {

		writer.writeOpeningTag("arrangementOnlyTracks");
		for (int32_t c = 0; c < arrangementOnlyClips.getNumElements(); c++) {
			Clip* clip = arrangementOnlyClips.getClipAtIndex(c);
			if (!clip->output->clipHasInstance(clip)) {
				continue; // Get rid of any redundant Clips. There shouldn't be any, but occasionally they somehow
				          // get left over.
			}
			clip->writeToFile(writer, this);
		}
		writer.writeClosingTag("arrangementOnlyTracks");
	}

	// Chord mem

	int maxChordPosToSave = 0;
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (chordMemNoteCount[y] > 0) {
			maxChordPosToSave = y + 1;
		}
	}
	if (maxChordPosToSave > 0) {
		// some chords to save
		writer.writeOpeningTag("chordMem");
		for (int32_t y = 0; y < maxChordPosToSave; y++) {
			writer.writeOpeningTag("chord");
			for (int i = 0; i < chordMemNoteCount[y]; i++) {
				writer.writeOpeningTagBeginning("note");
				writer.writeAttribute("code", chordMem[y][i]);
				writer.closeTag();
			}
			writer.writeClosingTag("chord");
		}
		writer.writeClosingTag("chordMem");
	}

	writer.writeClosingTag("song");
}

Error Song::readFromFile(Deserializer& reader) {
	D_PRINTLN("DEBUG: readFromFile");

	outputClipInstanceListIsCurrentlyInvalid = true;

	D_PRINTLN("");
	D_PRINTLN("loading song!!!!!!!!!!!!!!");

	char const* tagName;

	for (int32_t s = 0; s < kMaxNumSections; s++) {
		sections[s].numRepetitions = -1;
	}

	uint64_t newTimePerTimerTick = (uint64_t)1 << 32; // TODO: make better!

	// reverb mode
	if (smDeserializer.firmware_version < FirmwareVersion::official({4, 1, 4})) {
		AudioEngine::reverb.setModel(deluge::dsp::Reverb::Model::FREEVERB);
	}

	while (*(tagName = reader.readNextTagOrAttributeName())) {
		// D_PRINTLN(tagName); delayMS(30);
		switch (*(uint32_t*)tagName) {

		// "reve" -> reverb
		case 0x65766572:
			if (!strcmp(tagName, "reverb")) {
				while (*(tagName = reader.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "model")) {
						deluge::dsp::Reverb::Model model =
						    static_cast<deluge::dsp::Reverb::Model>(reader.readTagOrAttributeValueInt());
						if (model == deluge::dsp::Reverb::Model::FREEVERB) {
							AudioEngine::reverb.setModel(deluge::dsp::Reverb::Model::FREEVERB);
						}
						else if (model == deluge::dsp::Reverb::Model::MUTABLE) {
							AudioEngine::reverb.setModel(deluge::dsp::Reverb::Model::MUTABLE);
						}
						reader.exitTag("model");
					}
					else if (!strcmp(tagName, "roomSize")) {
						reverbRoomSize = (float)reader.readTagOrAttributeValueInt() / 2147483648u;
						reader.exitTag("roomSize");
					}
					else if (!strcmp(tagName, "dampening")) {
						reverbDamp = (float)reader.readTagOrAttributeValueInt() / 2147483648u;
						reader.exitTag("dampening");
					}
					else if (!strcmp(tagName, "width")) {
						int32_t widthInt = reader.readTagOrAttributeValueInt();
						if (widthInt == -2147483648) {
							widthInt = 2147483647; // Was being saved incorrectly in V2.1.0-beta1 and alphas, so we fix
							                       // it on read here!
						}
						reverbWidth = (float)widthInt / 2147483648u;
						reader.exitTag("width");
					}
					else if (!strcmp(tagName, "pan")) {
						reverbPan = reader.readTagOrAttributeValueInt();
						reader.exitTag("pan");
					}
					else if (!strcmp(tagName, "compressor")) {
						while (*(tagName = reader.readNextTagOrAttributeName())) {
							if (!strcmp(tagName, "attack")) {
								reverbSidechainAttack = reader.readTagOrAttributeValueInt();
								reader.exitTag("attack");
							}
							else if (!strcmp(tagName, "release")) {
								reverbSidechainRelease = reader.readTagOrAttributeValueInt();
								reader.exitTag("release");
							}
							else if (!strcmp(tagName, "volume")) {
								reverbSidechainVolume = reader.readTagOrAttributeValueInt();
								reader.exitTag("volume");
							}
							else if (!strcmp(tagName, "shape")) {
								reverbSidechainShape = reader.readTagOrAttributeValueInt();
								reader.exitTag("shape");
							}
							else if (!strcmp(tagName, "syncLevel")) {
								reverbSidechainSync = (SyncLevel)convertSyncLevelFromFileValueToInternalValue(
								    reader.readTagOrAttributeValueInt());
								reverbSidechainSync = (SyncLevel)std::min((uint8_t)reverbSidechainSync, (uint8_t)9);
								reader.exitTag("syncLevel");
							}
							else {
								reader.exitTag(tagName);
							}
						}
						reader.exitTag("compressor");
					}
					else {
						reader.exitTag(tagName);
					}
				}
			}
			else {
				goto unknownTag;
			}
			reader.exitTag();
			break;

		case charsToIntegerConstant('x', 'S', 'c', 'r'):

			switch (*(((uint32_t*)tagName) + 1)) {

			// "oll\x00" ->xScroll
			case 0x006c6c6f:
				xScroll[NAVIGATION_CLIP] = reader.readTagOrAttributeValueInt();
				xScroll[NAVIGATION_CLIP] = std::max((int32_t)0, xScroll[NAVIGATION_CLIP]);
				break;

			// "ollS" -> xScrollSongView
			case 0x536c6c6f:
				if (!strcmp(tagName, "xScrollSongView")) {
					xScrollForReturnToSongView = reader.readTagOrAttributeValueInt();
					xScrollForReturnToSongView = std::max((int32_t)0, xScrollForReturnToSongView);
					break;
				}
				else {
					goto unknownTag;
				}

			// "ollA" -> xScrollArrangementView"
			case 0x416c6c6f:
				if (!strcmp(tagName, "xScrollArrangementView")) {
					xScroll[NAVIGATION_ARRANGEMENT] = reader.readTagOrAttributeValueInt();
					break;
				}
				else {
					goto unknownTag;
				}

			default:
				goto unknownTag;
			}

			reader.exitTag();
			break;

		// "xZoo..."
		case 0x6f6f5a78:

			// "xZoomSongView"
			if (!strcmp(tagName, "xZoomSongView")) {
				xZoomForReturnToSongView = reader.readTagOrAttributeValueInt();
				xZoomForReturnToSongView = std::max((int32_t)1, xZoomForReturnToSongView);
			}

			// "xZoom"
			else if (!strcmp(tagName, "xZoom")) {
				xZoom[NAVIGATION_CLIP] = reader.readTagOrAttributeValueInt();
				xZoom[NAVIGATION_CLIP] = std::max((uint32_t)1, xZoom[NAVIGATION_CLIP]);
			}
			else {
				goto unknownTag;
			}

			reader.exitTag();
			break;

		// "yScr..."
		case 0x72635379:

			switch (*(((uint32_t*)tagName) + 1)) {

			// "ollS" -> yScrollSongView"
			case 0x536c6c6f:
				if (!strcmp(tagName, "yScrollSongView")) {
					songViewYScroll = reader.readTagOrAttributeValueInt();
					songViewYScroll = std::max(1 - kDisplayHeight, songViewYScroll);
					break;
				}
				else {
					goto unknownTag;
				}

			// "yScrollArrangementView"
			case 0x416c6c6f:
				if (!strcmp(tagName, "yScrollArrangementView")) {
					arrangementYScroll = reader.readTagOrAttributeValueInt();
					arrangementYScroll = std::max(1 - kDisplayHeight, arrangementYScroll);
					break;
				}
				else {
					goto unknownTag;
				}

			default:
				goto unknownTag;
			}
			reader.exitTag();
			break;

		default:
unknownTag:
			if (!strcmp(tagName, "firmwareVersion") || !strcmp(tagName, "earliestCompatibleFirmware")) {
				smDeserializer.tryReadingFirmwareTagFromFile(tagName);
				reader.exitTag(tagName);
			}
			else if (!strcmp(tagName, "preview") || !strcmp(tagName, "previewNumPads")) {
				smDeserializer.tryReadingFirmwareTagFromFile(tagName);
				reader.exitTag(tagName);
			}
			else if (!strcmp(tagName, "sessionLayout")) {
				sessionLayout = (SessionLayoutType)reader.readTagOrAttributeValueInt();
				reader.exitTag("sessionLayout");
			}

			else if (!strcmp(tagName, "songGridScrollX")) {
				songGridScrollX = reader.readTagOrAttributeValueInt();
				reader.exitTag("songGridScrollX");
			}

			else if (!strcmp(tagName, "songGridScrollY")) {
				songGridScrollY = reader.readTagOrAttributeValueInt();
				reader.exitTag("songGridScrollY");
			}

			else if (!strcmp(tagName, "xZoomArrangementView")) {
				xZoom[NAVIGATION_ARRANGEMENT] = reader.readTagOrAttributeValueInt();
				reader.exitTag("xZoomArrangementView");
			}

			else if (!strcmp(tagName,
			                 "inArrangementView")) { // For V2.0 pre-beta songs. There'd be another way to detect
				                                     // this...
				lastClipInstanceEnteredStartPos = 0;
				reader.exitTag("inArrangementView");
			}

			else if (!strcmp(tagName, "currentTrackInstanceArrangementPos")) {
				lastClipInstanceEnteredStartPos = reader.readTagOrAttributeValueInt();
				reader.exitTag("currentTrackInstanceArrangementPos");
			}

			else if (!strcmp(tagName, "arrangementAutoScrollOn")) {
				arrangerAutoScrollModeActive = reader.readTagOrAttributeValueInt();
				reader.exitTag("arrangementAutoScrollOn");
			}

			else if (!strcmp(tagName, "timePerTimerTick")) {
				newTimePerTimerTick = (newTimePerTimerTick & (uint64_t)0xFFFFFFFF)
				                      | ((uint64_t)reader.readTagOrAttributeValueInt() << 32);
				reader.exitTag("timePerTimerTick");
			}

			else if (!strcmp(tagName, "timerTickFraction")) {
				newTimePerTimerTick = (newTimePerTimerTick & ((uint64_t)0xFFFFFFFF << 32))
				                      | (uint64_t)(uint32_t)reader.readTagOrAttributeValueInt();
				reader.exitTag("timerTickFraction");
			}

			else if (!strcmp(tagName, "inputTickMagnitude")) {
				insideWorldTickMagnitude = reader.readTagOrAttributeValueInt();
				reader.exitTag("inputTickMagnitude");
			}

			else if (!strcmp(tagName, "rootNote")) {
				key.rootNote = reader.readTagOrAttributeValueInt();
				reader.exitTag("rootNote");
			}

			else if (!strcmp(tagName, "swingAmount")) {
				swingAmount = reader.readTagOrAttributeValueInt();
				swingAmount = std::min(swingAmount, (int8_t)49);
				swingAmount = std::max(swingAmount, (int8_t)-49);
				reader.exitTag("swingAmount");
			}

			else if (!strcmp(tagName, "swingInterval")) {
				// swingInterval, unlike other "sync" type params, we're going to read as just its plain old int32_t
				// value, and only shift it by insideWorldTickMagnitude after reading the whole song. This is
				// because these two attributes could easily be stored in either order in the file, so we won't know
				// both until the end. Also, in firmware pre V3.1.0-alpha, all "sync" values were stored as plain
				// old ints, to be read irrespective of insideWorldTickMagnitude
				swingInterval = reader.readTagOrAttributeValueInt();
				swingInterval = std::min(swingInterval, (uint8_t)9);
				reader.exitTag("swingInterval");
			}

			else if (!strcmp(tagName, "tripletsLevel")) {
				tripletsLevel = reader.readTagOrAttributeValueInt();
				reader.exitTag("tripletsLevel");
				tripletsOn = true;
			}

			else if (!strcmp(tagName, "activeModFunction")) {
				globalEffectable.modKnobMode = reader.readTagOrAttributeValueInt();
				globalEffectable.modKnobMode = std::min(globalEffectable.modKnobMode, (uint8_t)(kNumModButtons - 1));
				reader.exitTag("activeModFunction");
			}

			else if (!strcmp(tagName, "affectEntire")) {
				affectEntire = reader.readTagOrAttributeValueInt();
				reader.exitTag("affectEntire");
			}

			else if (!strcmp(tagName, "midiLoopback")) {
				midiLoopback = reader.readTagOrAttributeValueInt();
				reader.exitTag("midiLoopback");
			}

			else if (!strcmp(tagName, "lastSelectedParamID")) {
				lastSelectedParamID = reader.readTagOrAttributeValueInt();
				reader.exitTag("lastSelectedParamID");
			}

			else if (!strcmp(tagName, "lastSelectedParamKind")) {
				lastSelectedParamKind = static_cast<params::Kind>(reader.readTagOrAttributeValueInt());
				reader.exitTag("lastSelectedParamKind");
			}

			else if (!strcmp(tagName, "lastSelectedParamShortcutX")) {
				lastSelectedParamShortcutX = reader.readTagOrAttributeValueInt();
				reader.exitTag("lastSelectedParamShortcutX");
			}

			else if (!strcmp(tagName, "lastSelectedParamShortcutY")) {
				lastSelectedParamShortcutY = reader.readTagOrAttributeValueInt();
				reader.exitTag("lastSelectedParamShortcutY");
			}

			else if (!strcmp(tagName, "lastSelectedParamArrayPosition")) {
				lastSelectedParamArrayPosition = reader.readTagOrAttributeValueInt();
				reader.exitTag("lastSelectedParamArrayPosition");
			}

			// legacy section, read as part of global effectable (songParams tag) post c1.1
			else if (!strcmp(tagName, "songCompressor")) {
				while (*(tagName = reader.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "attack")) {
						q31_t masterCompressorAttack = reader.readTagOrAttributeValueInt();
						globalEffectable.compressor.setAttack(masterCompressorAttack);
						reader.exitTag("attack");
					}
					else if (!strcmp(tagName, "release")) {
						q31_t masterCompressorRelease = reader.readTagOrAttributeValueInt();
						globalEffectable.compressor.setRelease(masterCompressorRelease);
						reader.exitTag("release");
					}
					else if (!strcmp(tagName, "thresh")) {
						q31_t masterCompressorThresh = reader.readTagOrAttributeValueInt();
						globalEffectable.compressor.setThreshold(masterCompressorThresh);
						reader.exitTag("thresh");
					}
					else if (!strcmp(tagName, "ratio")) {
						q31_t masterCompressorRatio = reader.readTagOrAttributeValueInt();
						globalEffectable.compressor.setRatio(masterCompressorRatio);
						reader.exitTag("ratio");
					}
					else if (!strcmp(tagName, "compHPF")) {
						q31_t masterCompressorSidechain = reader.readTagOrAttributeValueInt();
						globalEffectable.compressor.setSidechain(masterCompressorSidechain);
						reader.exitTag("compHPF");
					}
					else {
						reader.exitTag(tagName);
					}
				}
				reader.exitTag("songCompressor");
			}

			else if (!strcmp(tagName, "modeNotes")) {
				key.modeNotes.clear();
				// Read in all the modeNotes
				while (*(tagName = reader.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "modeNote")) {
						key.modeNotes.addUntrusted(reader.readTagOrAttributeValueInt());
						reader.exitTag("modeNote");
					}
					else {
						reader.exitTag(tagName);
					}
				}
				reader.exitTag("modeNotes");
			}

			else if (!strcmp(tagName, "chordMem")) {
				int slot_index = 0;
				while (*(tagName = reader.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "chord")) {
						int y = slot_index++;
						if (y >= kDisplayHeight) {
							reader.exitTag("chord");
							continue;
						}
						int i = 0;
						while (*(tagName = reader.readNextTagOrAttributeName())) {
							if (!strcmp(tagName, "note")) {
								while (*(tagName = reader.readNextTagOrAttributeName())) {
									if (!strcmp(tagName, "code")) {
										if (i < MAX_NOTES_CHORD_MEM) {
											chordMem[y][i] = reader.readTagOrAttributeValueInt();
										}
									}
									else {
										reader.exitTag();
									}
								}
								i++;
							}
							else {
								reader.exitTag();
							}
						}
						chordMemNoteCount[y] = std::min(MAX_NOTES_CHORD_MEM, i);
					}
					else {
						reader.exitTag();
					}
				}
				reader.exitTag("chordMem");
			}

			else if (!strcmp(tagName, "sections")) {
				// Read in all the sections
				while (*(tagName = reader.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "section")) {

						uint8_t id = 255;
						MIDIDevice* device = NULL;
						uint8_t channel = 255;
						uint8_t note = 255;
						int16_t numRepeats = 0;

						while (*(tagName = reader.readNextTagOrAttributeName())) {
							if (!strcmp(tagName, "id")) {
								id = reader.readTagOrAttributeValueInt();
							}

							else if (!strcmp(tagName, "numRepeats")) {
								numRepeats = reader.readTagOrAttributeValueInt();
								if (numRepeats < -1 || numRepeats > 9999) {
									numRepeats = 0;
								}
							}

							// Annoyingly, I used one-off tag names here, rather than it conforming to what the
							// LearnedMIDI class now uses.
							else if (!strcmp(tagName, "midiCommandDevice")) {
								device = MIDIDeviceManager::readDeviceReferenceFromFile(reader);
							}

							else if (!strcmp(tagName, "midiCommandChannel")) {
								channel = reader.readTagOrAttributeValueInt();
							}

							else if (!strcmp(tagName, "midiCommandNote")) {
								note = reader.readTagOrAttributeValueInt();
							}

							reader.exitTag(tagName);
						}

						if (id < kMaxNumSections) {
							if (channel < 16 && note < 128) {
								sections[id].launchMIDICommand.device = device;
								sections[id].launchMIDICommand.channelOrZone = channel;
								sections[id].launchMIDICommand.noteOrCC = note;
							}
							sections[id].numRepetitions = numRepeats;
						}
						reader.exitTag("section");
					}
					else {
						reader.exitTag(tagName);
					}
				}
				reader.exitTag("sections");
			}

			else if (!strcmp(tagName, "instruments")) {

				Output** lastPointer = &firstOutput;
				while (*(tagName = reader.readNextTagOrAttributeName())) {

					void* memory;
					Output* newOutput;
					char const* defaultDirPath;
					Error error;

					if (!strcmp(tagName, "audioTrack")) {
						memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(AudioOutput));
						if (!memory) {
							return Error::INSUFFICIENT_RAM;
						}
						newOutput = new (memory) AudioOutput();
						goto loadOutput;
					}

					else if (!strcmp(tagName, "sound")) {
						memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(SoundInstrument));
						if (!memory) {
							return Error::INSUFFICIENT_RAM;
						}
						newOutput = new (memory) SoundInstrument();
						defaultDirPath = "SYNTHS";

setDirPathFirst:
						error = ((Instrument*)newOutput)->dirPath.set(defaultDirPath);
						if (error != Error::NONE) {
gotError:
							newOutput->~Output();
							delugeDealloc(memory);
							return error;
						}

loadOutput:
						error = newOutput->readFromFile(
						    reader, this, NULL,
						    0); // If it finds any default params, it'll make a ParamManager and "back it up"
						if (error != Error::NONE) {
							goto gotError;
						}
						((Instrument*)newOutput)->existsOnCard = true;
						*lastPointer = newOutput;
						lastPointer = &newOutput->next;
					}

					else if (!strcmp(tagName, "kit")) {
						memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(Kit));
						if (!memory) {
							return Error::INSUFFICIENT_RAM;
						}
						newOutput = new (memory) Kit();
						defaultDirPath = "KITS";
						goto setDirPathFirst;
					}
					// old firmwares used midiChannel or mpeZone depending on the type, but this distinction is never
					// used. The distinction is later in a different tag which can be "midiChannel" or "zone"
					else if (!strcmp(tagName, "midi") || !strcmp(tagName, "midiChannel")
					         || !strcmp(tagName, "mpeZone")) {
						memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(MIDIInstrument));
						if (!memory) {
							return Error::INSUFFICIENT_RAM;
						}
						newOutput = new (memory) MIDIInstrument();
						goto loadOutput;
					}

					else if (!strcmp(tagName, "cvChannel")) {
						memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(CVInstrument));
						if (!memory) {
							return Error::INSUFFICIENT_RAM;
						}
						newOutput = new (memory) CVInstrument();
						goto loadOutput;
					}

					reader.exitTag(tagName);
				}
				reader.exitTag("instruments");
			}

			else if (!strcmp(tagName, "songParams")) {
				GlobalEffectableForClip::readParamsFromFile(reader, &paramManager, 2147483647);
				reader.exitTag("songParams");
			}

			else if (!strcmp(tagName, "tracks") || !strcmp(tagName, "sessionClips")) {
				Error error = readClipsFromFile(reader, &sessionClips);
				if (error != Error::NONE) {
					return error;
				}
				reader.exitTag();
			}

			else if (!strcmp(tagName, "arrangementOnlyTracks") || !strcmp(tagName, "arrangementOnlyClips")) {
				Error error = readClipsFromFile(reader, &arrangementOnlyClips);
				if (error != Error::NONE) {
					return error;
				}
				reader.exitTag();
			}

			else {
				Error result = globalEffectable.readTagFromFile(reader, tagName, &paramManager, 2147483647, this);
				if (result == Error::NONE) {}
				else if (result != Error::RESULT_TAG_UNUSED) {
					return result;
				}
				else {
					Error result = smDeserializer.tryReadingFirmwareTagFromFile(tagName);
					if (result != Error::NONE && result != Error::RESULT_TAG_UNUSED) {
						return result;
					}
					if (ALPHA_OR_BETA_VERSION) {
						D_PRINTLN("unknown tag:  %s", tagName);
					}
					reader.exitTag(tagName);
				}
			}
		}
	}

	if (smDeserializer.firmware_version >= FirmwareVersion::official({3, 1, 0, "alpha2"})) {
		// Basically, like all other "sync" type parameters, the file value and internal value are different for
		// swingInterval. But unlike other ones, which get converted as we go, we do this one at the end once we
		// know we have enough info to do the conversion
		swingInterval = convertSyncLevelFromFileValueToInternalValue(swingInterval);
	}

	setTimePerTimerTick(newTimePerTimerTick);

	// Ensure all arranger-only Clips have their section as 255
	for (int32_t t = 0; t < arrangementOnlyClips.getNumElements(); t++) {
		Clip* clip = arrangementOnlyClips.getClipAtIndex(t);

		clip->section = 255;
		clip->gotInstanceYet = false;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	int32_t count = 0;

	// Match all Clips up with their Output
	// For each Clip in session and arranger
	std::array<ClipArray*, 2> const arrays{
	    &sessionClips,
	    &arrangementOnlyClips,
	};
	for (ClipArray* clipArray : arrays) {
		for (int32_t c = 0; c < clipArray->getNumElements(); c++) {
			Clip* thisClip = clipArray->getClipAtIndex(c); // TODO: deal with other Clips too!

			if (!(count & 31)) {
				AudioEngine::routineWithClusterLoading(); // -----------------------------------
				AudioEngine::logAction("aaa0");
			}
			count++;

			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(thisClip);

			Error error = thisClip->claimOutput(modelStackWithTimelineCounter);
			if (error != Error::NONE) {
				return error;
			}

			// Correct different non-synced rates of old song files
			// In a perfect world, we'd do this for Kits, MIDI and CV too
			if (smDeserializer.firmware_version < FirmwareVersion::official({1, 5, 0, "pretest"})
			    && thisClip->output->type == OutputType::SYNTH) {
				if (((InstrumentClip*)thisClip)->arpSettings.mode != ArpMode::OFF
				    && !((InstrumentClip*)thisClip)->arpSettings.syncLevel) {
					ParamManagerForTimeline* thisParamManager = &thisClip->paramManager;
					thisParamManager->getPatchedParamSet()->params[params::GLOBAL_ARP_RATE].shiftValues((1 << 30)
					                                                                                    + (1 << 28));
				}
			}
		}
	}

	AudioEngine::logAction("matched up");

	AudioEngine::routineWithClusterLoading(); // -----------------------------------

	anyOutputsSoloingInArrangement = false;

	D_PRINTLN("aaa1");

	// Match all ClipInstances up with their Clip. And while we're at it, check if any Outputs are soloing in
	// arranger
	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput->soloingInArrangementMode) {
			anyOutputsSoloingInArrangement = true;
		}

		if (thisOutput->clipInstances.getNumElements() == 0
		    && getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)thisOutput, nullptr) == nullptr
		    && thisOutput->type == OutputType::AUDIO) {
			// This clip has no way to get a param manager, and no clips to help it out. Need to create a backup or
			// things will go wrong later.
			ParamManagerForTimeline paramManager{};

			paramManager.setupUnpatched();
			GlobalEffectable::initParams(&paramManager);

			this->backUpParamManager((ModControllableAudio*)thisOutput->toModControllable(), NULL, &paramManager);
		}

		for (int32_t i = 0; i < thisOutput->clipInstances.getNumElements(); i++) {
			ClipInstance* thisInstance = thisOutput->clipInstances.getElement(i);

			// Grab out the encoded Clip reference and turn it into an actual Clip*
			uint32_t clipCode = (uint32_t)thisInstance->clip;

			// Special case for NULL Clip
			if (clipCode == 0xFFFFFFFF) {
				thisInstance->clip = NULL;
			}

			else {
				uint32_t lookingForIndex = clipCode & ~((uint32_t)1 << 31);
				bool isArrangementClip = clipCode >> 31;

				ClipArray* clips = isArrangementClip ? &arrangementOnlyClips : &sessionClips;

				if (lookingForIndex >= clips->getNumElements()) {
#if ALPHA_OR_BETA_VERSION
					display->displayPopup("E248");
#endif
skipInstance:
					thisOutput->clipInstances.deleteAtIndex(i);
					i--;
					continue;
				}

				thisInstance->clip = clips->getClipAtIndex(lookingForIndex);

				// If Instrument mismatch somehow...
				if (thisInstance->clip->output != thisOutput) {
#if ALPHA_OR_BETA_VERSION
					display->displayPopup("E451"); // Changed from E041 - was a duplicate.
#endif
					goto skipInstance;
				}

				// If arrangement-only and it somehow had already got a ClipInstance...
				if (isArrangementClip && thisInstance->clip->gotInstanceYet) {

#if ALPHA_OR_BETA_VERSION
					display->displayPopup("E042");
#endif
					goto skipInstance;
				}

				// If still here, can mark the Clip as claimed
				thisInstance->clip->gotInstanceYet = true;
			}
		}

		// If saved before V2.1, set sample-based synth instruments to linear interpolation, cos that's how it was
		if (smDeserializer.firmware_version < FirmwareVersion::official({2, 1, 0, "beta"})) {
			if (thisOutput->type == OutputType::SYNTH) {
				SoundInstrument* sound = (SoundInstrument*)thisOutput;

				for (int32_t s = 0; s < kNumSources; s++) {
					Source* source = &sound->sources[s];
					if (source->oscType == OscType::SAMPLE) {
						source->sampleControls.interpolationMode = InterpolationMode::LINEAR;
					}
				}
			}
		}
	}

	outputClipInstanceListIsCurrentlyInvalid = false; // All clipInstances are valid now.

	D_PRINTLN("aaa2");

	// Ensure no arrangement-only Clips with no ClipInstance
	// For each Clip in arrangement
	for (int32_t c = 0; c < arrangementOnlyClips.getNumElements(); c++) {
		Clip* clip = arrangementOnlyClips.getClipAtIndex(c);

		if (!clip->gotInstanceYet) {
#if ALPHA_OR_BETA_VERSION
			display->displayPopup("E043");
#endif
			if (currentClip == clip) {
				currentClip = NULL;
			}
			if (syncScalingClip == clip) {
				syncScalingClip = NULL;
			}

			arrangementOnlyClips.deleteAtIndex(c);
			deleteClipObject(clip, false, InstrumentRemoval::NONE);
			c--;
		}
	}

	// Pre V1.2...
	if (smDeserializer.firmware_version < FirmwareVersion::official({1, 2, 0})) {

		deleteAllBackedUpParamManagers(true); // Before V1.2, lots of extras of these could be created during loading
		globalEffectable.compensateVolumeForResonance(&paramManager);
	}

	if (syncScalingClip) {
		Clip* newInputTickScaleClip = syncScalingClip;
		syncScalingClip = NULL; // We shouldn't have set this manually earlier, anyway - we just saved hassle
		setInputTickScaleClip(newInputTickScaleClip);
	}

	D_PRINTLN("aaa3");
	AudioEngine::logAction("aaa3.1");

	AudioEngine::routineWithClusterLoading(); // -----------------------------------

	reassessWhetherAnyClipsSoloing();

	AudioEngine::logAction("aaa4.2");

	setupPatchingForAllParamManagers();
	AudioEngine::logAction("aaa4.3");

	AudioEngine::routineWithClusterLoading(); // -----------------------------------

	int32_t playbackWillStartInArrangerAtPos = playbackHandler.playbackState ? lastClipInstanceEnteredStartPos : -1;

	AudioEngine::logAction("aaa5.1");
	sortOutWhichClipsAreActiveWithoutSendingPGMs(modelStack, playbackWillStartInArrangerAtPos);
	AudioEngine::logAction("aaa5.2");

	AudioEngine::routineWithClusterLoading(); // -----------------------------------

	return Error::NONE;
}

Error Song::readClipsFromFile(Deserializer& reader, ClipArray* clipArray) {
	char const* tagName;

	while (*(tagName = reader.readNextTagOrAttributeName())) {

		int32_t allocationSize;
		ClipType clipType;

		if (!strcmp(tagName, "track") || !strcmp(tagName, "instrumentClip")) {
			allocationSize = sizeof(InstrumentClip);
			clipType = ClipType::INSTRUMENT;

readClip:
			if (!clipArray->ensureEnoughSpaceAllocated(1)) {
				return Error::INSUFFICIENT_RAM;
			}

			void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(allocationSize);
			if (!memory) {
				return Error::INSUFFICIENT_RAM;
			}

			Clip* newClip;
			if (clipType == ClipType::INSTRUMENT) {
				newClip = new (memory) InstrumentClip();
			}
			else {
				newClip = new (memory) AudioClip();
			}

			Error error = newClip->readFromFile(reader, this);
			if (error != Error::NONE) {
				newClip->~Clip();
				delugeDealloc(memory);
				return error;
			}

			clipArray->insertClipAtIndex(newClip, clipArray->getNumElements()); // We made sure enough space, above

			reader.exitTag();
		}
		else if (!strcmp(tagName, "audioClip")) {
			allocationSize = sizeof(AudioClip);
			clipType = ClipType::AUDIO;

			goto readClip;
		}
		else {
			reader.exitTag(tagName);
		}
	}

	return Error::NONE;
}

// Needs to be in a separate function than the above because the main song XML file needs to be closed first before
// this is called, because this will open other (sample) files
void Song::loadAllSamples(bool mayActuallyReadFiles) {

	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		thisOutput->loadAllAudioFiles(mayActuallyReadFiles);
	}

	// NOTE: Using AllClips instead of AudioClips to ensure we call into the audio engine during
	// the scan, instead of only when we find audio clips!
	uint32_t c = 0;
	for (Clip* clip : AllClips::everywhere(this)) {
		// If not reading files, high chance that we'll be searching through memory a lot and not reading the card
		// (which would call the audio routine), so we'd better call the audio routine here.
		if (!mayActuallyReadFiles && !(c++ & 7)) { // 31 bad. 15 seems to pass. 7 to be safe
			AudioEngine::logAction("Song::loadAllSamples");
			AudioEngine::routineWithClusterLoading(); // -----------------------------------
		}
		if (clip->type == ClipType::AUDIO) {
			((AudioClip*)clip)->loadSample(mayActuallyReadFiles);
		}
	}
}

void Song::loadCrucialSamplesOnly() {
	// TODO: This searches just as much as loadAllSamples, why does this not need to call into the
	// audio engine? Is the searching actually ok, and only the loadSample() counts should be considered
	// for calling into the audio engine?
	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput->getActiveClip() && isClipActive(thisOutput->getActiveClip())) {
			thisOutput->loadCrucialAudioFilesOnly();
		}
	}

	for (AudioClip* clip : AudioClips::everywhere(this)) {
		if (clip->isActiveOnOutput()) {
			clip->loadSample(true);
		}
	}
}

void Song::deleteSoundsWhichWontSound() {

	// Delete Clips inactive on Output
	// For each Clip in session and arranger
	AllClips allClips = AllClips::everywhere(this);
	for (ClipIterator it = allClips.begin(); it != allClips.end();) {
		Clip* clip = *it;

		AudioEngine::routineWithClusterLoading(); // -----------------------------------
		if (!clip->isActiveOnOutput() && clip != view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
			it.deleteClip(InstrumentRemoval::NONE);
		}
		else {
			++it;
		}
	}

	// Now there's only one Clip left per Output

	// Delete Clips which won't sound
	// For each Clip in session and arranger
	for (ClipIterator it = allClips.begin(); it != allClips.end();) {
		Clip* clip = *it;

		AudioEngine::routineWithClusterLoading(); // -----------------------------------
		if (clip->deleteSoundsWhichWontSound(this)) {
			it.deleteClip(InstrumentRemoval::DELETE);
		}
		else {
			++it;
		}
	}

	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		thisOutput->clipInstances.empty();
	}

	deleteAllOutputs((Output**)&firstHibernatingInstrument);
	deleteHibernatingMIDIInstrument();

	// Can't delete the ones with no Clips, cos these might be needed by their ModControllables
	deleteAllBackedUpParamManagersWithClips();
}

void Song::renderAudio(StereoSample* outputBuffer, int32_t numSamples, int32_t* reverbBuffer,
                       int32_t sideChainHitPending) {

	// int32_t volumePostFX = getParamNeutralValue(params::GLOBAL_VOLUME_POST_FX);
	int32_t volumePostFX =
	    getFinalParameterValueVolume(
	        134217728,
	        cableToLinearParamShortcut(paramManager.getUnpatchedParamSet()->getValue(params::UNPATCHED_VOLUME)))
	    >> 1;

	// A "post-FX volume" calculation also happens in audioDriver.render(), which is a bit more relevant really
	// because that's where filters are happening

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	AudioEngine::logAction("Start output render");
	for (Output* output = firstOutput; output; output = output->next) {
		if (!output->inValidState) {
			continue;
		}

		bool isClipActiveNow =
		    (output->getActiveClip() && isClipActive(output->getActiveClip()->getClipBeingRecordedFrom()));
		DISABLE_ALL_INTERRUPTS();
		output->renderOutput(modelStack, outputBuffer, outputBuffer + numSamples, numSamples, reverbBuffer,
		                     volumePostFX >> 1, sideChainHitPending, !isClipActiveNow, isClipActiveNow);
		ENABLE_INTERRUPTS();
#if DO_AUDIO_LOG
		char buf[64];
		snprintf(buf, sizeof(buf), "complete: %s", output->name.get());
		AudioEngine::logAction(buf);
#endif
	}

	// If recording the "MIX", this is the place where we want to grab it - before any master FX or volume applied
	// Go through each SampleRecorder, feeding them audio
	for (SampleRecorder* recorder = AudioEngine::firstRecorder; recorder; recorder = recorder->next) {

		if (recorder->status >= RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
			continue;
		}

		if (recorder->mode == AudioInputChannel::MIX) {
			recorder->feedAudio((int32_t*)outputBuffer, numSamples, true);
		}
	}

	Delay::State delayWorkingState = globalEffectable.createDelayWorkingState(paramManager);

	globalEffectable.processFXForGlobalEffectable(outputBuffer, numSamples, &volumePostFX, &paramManager,
	                                              delayWorkingState);

	int32_t postReverbVolume = paramNeutralValues[params::GLOBAL_VOLUME_POST_REVERB_SEND];
	int32_t reverbSendAmount =
	    getFinalParameterValueVolume(paramNeutralValues[params::GLOBAL_REVERB_AMOUNT],
	                                 cableToLinearParamShortcut(paramManager.getUnpatchedParamSet()->getValue(
	                                     params::UNPATCHED_REVERB_SEND_AMOUNT)));

	globalEffectable.processReverbSendAndVolume(outputBuffer, numSamples, reverbBuffer, volumePostFX, postReverbVolume,
	                                            reverbSendAmount >> 1);

	if (playbackHandler.isEitherClockActive() && !playbackHandler.ticksLeftInCountIn
	    && currentPlaybackMode == &arrangement) {
		const bool result = params::kMaxNumUnpatchedParams > 32
		                        ? paramManager.getUnpatchedParamSetSummary()->whichParamsAreInterpolating[0]
		                              || paramManager.getUnpatchedParamSetSummary()->whichParamsAreInterpolating[1]
		                        : paramManager.getUnpatchedParamSetSummary()->whichParamsAreInterpolating[0];
		if (result) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings = addToModelStack(modelStack);
			paramManager.tickSamples(numSamples, modelStackWithThreeMainThings);
		}
	}
}

void Song::setTimePerTimerTick(uint64_t newTimeBig, bool shouldLogAction) {

	if (shouldLogAction) {
		actionLogger.recordTempoChange(timePerTimerTickBig, newTimeBig);
	}

	// Alter timing of next and last timer ticks
	if (currentSong == this && playbackHandler.isInternalClockActive()) {
		uint32_t timeSinceLastTimerTick =
		    AudioEngine::audioSampleTimer - (uint32_t)(playbackHandler.timeLastTimerTickBig >> 32);

		// Using intermediary float here is best: newTimeBig might be a huge number, and below about 2bpm (so not
		// that low), multiplying it by timeSinceLastTimerTick can overflow a uint64_t
		timeSinceLastTimerTick = (float)timeSinceLastTimerTick * newTimeBig / timePerTimerTickBig;

		playbackHandler.timeLastTimerTickBig =
		    (uint64_t)(uint32_t)(AudioEngine::audioSampleTimer - timeSinceLastTimerTick) << 32;

		uint32_t timeTilNextTimerTick =
		    (uint32_t)(playbackHandler.timeNextTimerTickBig >> 32) - AudioEngine::audioSampleTimer;

		// Using intermediary float here is best: newTimeBig might be a huge number, and below about 2bpm (so not
		// that low), multiplying it by timeSinceLastTimerTick can overflow a uint64_t
		timeTilNextTimerTick = (float)timeTilNextTimerTick * newTimeBig / timePerTimerTickBig;

		playbackHandler.timeNextTimerTickBig = (uint64_t)(AudioEngine::audioSampleTimer + timeTilNextTimerTick) << 32;
	}

	timePerTimerTickBig = newTimeBig;

	divideByTimePerTimerTick = ((uint64_t)1 << 63) / ((newTimeBig * 3) >> 1);

	// Reschedule upcoming swung, MIDI and trigger clock out ticks
	if (currentSong == this && playbackHandler.isInternalClockActive()) {
		playbackHandler.scheduleSwungTickFromInternalClock();
		if (cvEngine.isTriggerClockOutputEnabled()) {
			playbackHandler.scheduleTriggerClockOutTick();
		}
		if (playbackHandler.currentlySendingMIDIOutputClocks()) {
			playbackHandler.scheduleMIDIClockOutTick();
		}
	}
}

bool Song::hasAnySwing() {
	return (swingAmount != 0);
}

void Song::resyncLFOs() {

	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput->getActiveClip()) {
			thisOutput->resyncLFOs();
		}
		// Yes, do it even for Clips that aren't actually "playing" / active in the Song
	}
}

NoteRow* Song::findNoteRowForDrum(Kit* kit, Drum* drum, Clip* stopTraversalAtClip) {

	// If currently swapping an Instrument, it can't be assumed that all arranger-only Clips for this Instrument are
	// in its clipInstances, which otherwise is a nice time-saver

	// For each Clip in session and arranger for specific Output - but if currentlySwappingInstrument, use master
	// list for arranger Clips
	ClipArray* clipArray = &sessionClips;
	bool doingClipsProvidedByOutput = false;
decideNumElements:
	int32_t numElements = clipArray->getNumElements();
	// TODO: Should use AllClips(), but less obvious so later.
traverseClips:
	for (int32_t c = 0; c < numElements; c++) {
		InstrumentClip* instrumentClip;
		if (!doingClipsProvidedByOutput) {
			Clip* clip = clipArray->getClipAtIndex(c);
			if (clip == stopTraversalAtClip) {
				return NULL;
			}
			if (clip->output != kit) {
				continue;
			}
			instrumentClip = (InstrumentClip*)clip;
		}
		else {
			ClipInstance* clipInstance = kit->clipInstances.getElement(c);
			if (!clipInstance->clip) {
				continue;
			}
			if (!clipInstance->clip->isArrangementOnlyClip()) {
				continue;
			}
			instrumentClip = (InstrumentClip*)clipInstance->clip;
		}

		NoteRow* noteRow = instrumentClip->getNoteRowForDrum(drum);
		if (noteRow) {
			return noteRow;
		}
	}
	if (!doingClipsProvidedByOutput && clipArray == &sessionClips) {
		if (outputClipInstanceListIsCurrentlyInvalid) {
			clipArray = &arrangementOnlyClips;
			goto decideNumElements;
		}
		else {
			doingClipsProvidedByOutput = true;
			numElements = kit->clipInstances.getNumElements();
			goto traverseClips;
		}
	}

	return NULL;
}

ParamManagerForTimeline* Song::findParamManagerForDrum(Kit* kit, Drum* drum, Clip* stopTraversalAtClip) {
	NoteRow* noteRow = findNoteRowForDrum(kit, drum, stopTraversalAtClip);
	if (!noteRow) {
		return NULL;
	}
	return &noteRow->paramManager;
}

void Song::setupPatchingForAllParamManagersForDrum(SoundDrum* drum) {

	// If currently swapping an Instrument, it can't be assumed that all arranger-only Clips for this Instrument are
	// in its clipInstances, which otherwise is a nice time-saver

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	// We don't know the Kit / Instrument. That's unfortunate. We'll work it out if we can
	Output* output = NULL;

	// For each Clip in session and arranger for specific Output - but if currentlySwappingInstrument, use master
	// list for arranger Clips
	ClipArray* clipArray = &sessionClips;
	bool doingClipsProvidedByOutput = false;
decideNumElements:
	int32_t numElements = clipArray->getNumElements();
	// TODO: Should use AllClips, but less obvious so later.
traverseClips:
	for (int32_t c = 0; c < numElements; c++) {
		InstrumentClip* instrumentClip;
		if (!doingClipsProvidedByOutput) {
			Clip* clip = clipArray->getClipAtIndex(c);

			if (output) {
				if (clip->output != output) {
					continue;
				}
			}
			else {
				if (clip->output->type != OutputType::KIT) {
					continue;
				}
			}

			instrumentClip = (InstrumentClip*)clip;
		}
		else {
			ClipInstance* clipInstance = output->clipInstances.getElement(c);
			if (!clipInstance->clip) {
				continue;
			}
			if (!clipInstance->clip->isArrangementOnlyClip()) {
				continue;
			}
			instrumentClip = (InstrumentClip*)clipInstance->clip;
		}

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(instrumentClip);

		ModelStackWithNoteRow* modelStackWithNoteRow =
		    instrumentClip->getNoteRowForDrum(modelStackWithTimelineCounter, drum);

		NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
		if (noteRow) {
			if (!output) {
				output =
				    instrumentClip->output; // The "if" check at the start should be unnecessary... but just in case.
			}

			if (noteRow->paramManager.containsAnyMainParamCollections()) {

				ModelStackWithParamCollection* modelStackWithParamCollection = noteRow->paramManager.getPatchCableSet(
				    modelStackWithNoteRow->addOtherTwoThings(drum, &noteRow->paramManager));

				((PatchCableSet*)modelStackWithParamCollection->paramCollection)
				    ->setupPatching(modelStackWithParamCollection);
			}
		}
	}

	// If we just finished iterating the sessionClips, we next want to do the arrangementOnlyClips.
	if (!doingClipsProvidedByOutput && clipArray == &sessionClips) {
		// If Instrument is currently being "swapped", or if we still don't actually know the Output, then we have
		// to do things the slow way and search every arrangementOnlyClip
		if (outputClipInstanceListIsCurrentlyInvalid || !output) {
			clipArray = &arrangementOnlyClips;
			goto decideNumElements;
		}

		// Or more ideally, we would have found the Output along the way, so we can just grab its arrangement-only
		// Clips directly from it
		else {
			doingClipsProvidedByOutput = true;
			numElements = output->clipInstances.getNumElements();
			goto traverseClips;
		}
	}
}

void Song::setupPatchingForAllParamManagersForInstrument(SoundInstrument* sound) {

	// If currently swapping an Instrument, it can't be assumed that all arranger-only Clips for this Instrument are
	// in its clipInstances, which otherwise is a nice time-saver

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithModControllable* modelStack = setupModelStackWithSong(modelStackMemory, this)
	                                                ->addTimelineCounter(NULL)
	                                                ->addModControllableButNoNoteRow(sound);

	// For each Clip in session and arranger for specific Output
	int32_t numElements = sessionClips.getNumElements();
	bool doingArrangementClips = false;
	// TODO: Should use AllClips, but less obvious so later.
traverseClips:
	for (int32_t c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = sessionClips.getClipAtIndex(c);
			if (clip->output != sound) {
				continue;
			}
		}
		else {
			ClipInstance* clipInstance = sound->clipInstances.getElement(c);
			if (!clipInstance->clip) {
				continue;
			}
			if (!clipInstance->clip->isArrangementOnlyClip()) {
				continue;
			}
			clip = clipInstance->clip;
		}

		modelStack->setTimelineCounter(clip);
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings = modelStack->addParamManager(&clip->paramManager);
		ModelStackWithParamCollection* modelStackWithParamCollection =
		    clip->paramManager.getPatchCableSet(modelStackWithThreeMainThings);

		((PatchCableSet*)modelStackWithParamCollection->paramCollection)->setupPatching(modelStackWithParamCollection);
	}
	if (!doingArrangementClips) {
		doingArrangementClips = true;
		numElements = sound->clipInstances.getNumElements();
		goto traverseClips;
	}
}

void Song::grabVelocityToLevelFromMIDIDeviceAndSetupPatchingForAllParamManagersForInstrument(
    MIDIDevice* device, SoundInstrument* instrument) {

	if (!device->hasDefaultVelocityToLevelSet()) {
		return;
	}

	// TODO: backed up ParamManagers?

	// If currently swapping an Instrument, it can't be assumed that all arranger-only Clips for this Instrument are
	// in its clipInstances, which otherwise is a nice time-saver

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithModControllable* modelStack = setupModelStackWithSong(modelStackMemory, this)
	                                                ->addTimelineCounter(NULL)
	                                                ->addModControllableButNoNoteRow(instrument);

	// For each Clip in session and arranger for specific Output
	int32_t numElements = sessionClips.getNumElements();
	bool doingArrangementClips = false;
	// TODO: Should use AllClips, but less obvious so later.
traverseClips:
	for (int32_t c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = sessionClips.getClipAtIndex(c);
			if (clip->output != instrument) {
				continue;
			}
		}
		else {
			ClipInstance* clipInstance = instrument->clipInstances.getElement(c);
			if (!clipInstance->clip) {
				continue;
			}
			if (!clipInstance->clip->isArrangementOnlyClip()) {
				continue;
			}
			clip = clipInstance->clip;
		}

		modelStack->setTimelineCounter(clip);
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings = modelStack->addParamManager(&clip->paramManager);
		ModelStackWithParamCollection* modelStackWithParamCollection =
		    clip->paramManager.getPatchCableSet(modelStackWithThreeMainThings);

		PatchCableSet* patchCableSet = (PatchCableSet*)modelStackWithParamCollection->paramCollection;

		patchCableSet->grabVelocityToLevelFromMIDIDeviceDefinitely(device);
		patchCableSet->setupPatching(modelStackWithParamCollection);
	}
	if (!doingArrangementClips) {
		doingArrangementClips = true;
		numElements = instrument->clipInstances.getNumElements();
		goto traverseClips;
	}
}

// kit is required, fortunately. Unlike some of the other "for drum" functions here.
void Song::grabVelocityToLevelFromMIDIDeviceAndSetupPatchingForAllParamManagersForDrum(MIDIDevice* device,
                                                                                       SoundDrum* drum, Kit* kit) {

	if (!device->hasDefaultVelocityToLevelSet()) {
		return;
	}

	// TODO: backed up ParamManagers?

	// If currently swapping an Instrument, it can't be assumed that all arranger-only Clips for this Instrument are
	// in its clipInstances, which otherwise is a nice time-saver

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	// For each Clip in session and arranger for specific Output
	int32_t numElements = sessionClips.getNumElements();
	bool doingArrangementClips = false;
	// TODO: Should use AllClips but less obvious, so later.
traverseClips:
	for (int32_t c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = sessionClips.getClipAtIndex(c);
			if (clip->output != kit) {
				continue;
			}
		}
		else {
			ClipInstance* clipInstance = kit->clipInstances.getElement(c);
			if (!clipInstance->clip) {
				continue;
			}
			if (!clipInstance->clip->isArrangementOnlyClip()) {
				continue;
			}
			clip = clipInstance->clip;
		}

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

		ModelStackWithNoteRow* modelStackWithNoteRow =
		    ((InstrumentClip*)clip)->getNoteRowForDrum(modelStackWithTimelineCounter, drum);
		NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
		if (!noteRow) {
			continue;
		}

		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStackWithNoteRow->addModControllable((SoundDrum*)drum)->addParamManager(&noteRow->paramManager);

		ModelStackWithParamCollection* modelStackWithParamCollection =
		    noteRow->paramManager.getPatchCableSet(modelStackWithThreeMainThings);

		PatchCableSet* patchCableSet = (PatchCableSet*)modelStackWithParamCollection->paramCollection;

		patchCableSet->grabVelocityToLevelFromMIDIDeviceDefinitely(device);
		patchCableSet->setupPatching(modelStackWithParamCollection);
	}
	if (!doingArrangementClips) {
		doingArrangementClips = true;
		numElements = kit->clipInstances.getNumElements();
		goto traverseClips;
	}
}

void Song::grabVelocityToLevelFromMIDIDeviceAndSetupPatchingForEverything(MIDIDevice* device) {
	// if (!device->hasDefaultVelocityToLevelSet()) return; // In this case, we'll take 0 to actually mean zero.

	// TODO: backed up ParamManagers?

	// If currently swapping an Instrument, it can't be assumed that all arranger-only Clips for this Instrument are
	// in its clipInstances, which otherwise is a nice time-saver

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	for (Clip* clip : AllClips::everywhere(this)) {
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

		Output* output = clip->output;

		if (output->type == OutputType::SYNTH) {
			SoundInstrument* synth = (SoundInstrument*)output;
			if (synth->midiInput.containsSomething() && synth->midiInput.device == device) {

				ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
				    modelStackWithTimelineCounter->addModControllableButNoNoteRow(synth)->addParamManager(
				        &clip->paramManager);

				ModelStackWithParamCollection* modelStackWithParamCollection =
				    clip->paramManager.getPatchCableSet(modelStackWithThreeMainThings);

				PatchCableSet* patchCableSet = (PatchCableSet*)modelStackWithParamCollection->paramCollection;
				patchCableSet->grabVelocityToLevelFromMIDIDeviceDefinitely(device);

				patchCableSet->setupPatching(modelStackWithParamCollection);
			}
		}

		else if (output->type == OutputType::KIT) {
			Kit* kit = (Kit*)output;

			for (Drum* drum = kit->firstDrum; drum; drum = drum->next) {
				if (drum->type == DrumType::SOUND && drum->midiInput.containsSomething()
				    && drum->midiInput.device == device) {

					ModelStackWithNoteRow* modelStackWithNoteRow =
					    ((InstrumentClip*)clip)->getNoteRowForDrum(modelStackWithTimelineCounter, drum);
					NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
					if (!noteRow) {
						continue;
					}

					ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
					    modelStackWithNoteRow->addModControllable((SoundDrum*)drum)
					        ->addParamManager(&noteRow->paramManager);

					ModelStackWithParamCollection* modelStackWithParamCollection =
					    noteRow->paramManager.getPatchCableSet(modelStackWithThreeMainThings);

					PatchCableSet* patchCableSet = (PatchCableSet*)modelStackWithParamCollection->paramCollection;

					patchCableSet->grabVelocityToLevelFromMIDIDeviceDefinitely(device);
					patchCableSet->setupPatching(modelStackWithParamCollection);
				}
			}
		}
	}
}

const char* Song::getScaleName(int32_t scale) {
	if (scale >= NUM_PRESET_SCALES) {
		// Other scale
		return deluge::l10n::get(deluge::l10n::String::STRING_FOR_OTHER_SCALE);
	}
	else {
		// Preset scale
		return presetScaleNames[scale];
	}
}

int32_t Song::cycleThroughScales() {
	int32_t currentScale = getCurrentPresetScale();
	int32_t newScale = currentScale + 1; // next scale
	if (newScale >= NUM_PRESET_SCALES) {
		// If past the last scale, start from the beginning
		newScale = 0;
	}
	return setPresetScale(newScale);
}

/// Returns CUSTOM_SCALE_WITH_MORE_THAN_7_NOTES if there are more than 7 notes and no preset is possible.
int32_t Song::setPresetScale(int32_t newScale) {
	int32_t numNotesInCurrentScale = key.modeNotes.count();
	int32_t numNotesInNewScale = 7;

	// Get num of notes of new scale
	if (newScale >= FIRST_5_NOTE_SCALE_INDEX) {
		numNotesInNewScale = 5;
	}
	else if (newScale >= FIRST_6_NOTE_SCALE_INDEX) {
		numNotesInNewScale = 6;
	}

	// Always count the root note as present, to avoid changing the root note when cycling scales.
	NoteSet notesWithinOctavePresent;
	notesWithinOctavePresent.add(0);

	if (numNotesInCurrentScale > numNotesInNewScale) {
		// We are trying to pass from source scale with more notes than the target scale.
		// We need to check the real number of notes used to see if we can convert it.

		for (InstrumentClip* instrumentClip : InstrumentClips::everywhere(this)) {
			if (instrumentClip->isScaleModeClip()) {
				instrumentClip->seeWhatNotesWithinOctaveArePresent(notesWithinOctavePresent, key.rootNote, this);
			}
		}
	}

	int32_t notesWithinOctavePresentCount = notesWithinOctavePresent.count();

	// If the new scale cannot fit the notes from the old one, we can't change scale
	if ((newScale >= 0 && notesWithinOctavePresentCount > 7) // More than 7 notes (no scale is possible)
	    || (newScale >= FIRST_6_NOTE_SCALE_INDEX
	        && notesWithinOctavePresentCount > 6) // 6-note scale, but more than 6 notes
	    || (newScale >= FIRST_5_NOTE_SCALE_INDEX
	        && notesWithinOctavePresentCount > 5)) { // 5-note scale, but more than 5 notes
		if (notesWithinOctavePresentCount <= 7) {
			// If the total number of notes present is still 7 or less,
			// we can fall back to Major scale
			newScale = 0;
			numNotesInNewScale = 7;
		}
		else {
			return CUSTOM_SCALE_WITH_MORE_THAN_7_NOTES;
		}
	}

	// If going from scale with more notes to a scale with less notes,
	// find the highest unused degree indexes and save them in temporary variables
	uint8_t transitionModeNotes[12] = {0};
	if (numNotesInCurrentScale == 7 && numNotesInNewScale < 7) {
		// First find the highest unused degree to jump from 7 to 6
		indexLastUnusedScaleDegreeFrom7To6 = 0; // Reset
		for (int32_t n = 6; n >= 1; n--) {
			if (key.modeNotes[n] != 0 && !notesWithinOctavePresent.has(key.modeNotes[n])) {
				indexLastUnusedScaleDegreeFrom7To6 = n;
				break;
			}
		}
		if (numNotesInNewScale == 5) {
			// in case the target scale is 5 notes, we are gonna need the transitionModeNotes
			// because it is going to be a 2-step jump!
			int32_t offset = 0;
			for (int32_t n = 1; n < 6; n++) {
				if (n == indexLastUnusedScaleDegreeFrom7To6) {
					offset++;
				}
				transitionModeNotes[n] = key.modeNotes[n + offset];
			}
		}
	}
	if (numNotesInCurrentScale > 5 && numNotesInNewScale == 5) {
		// See if we use the real modeNotes, or the transition ones (in case we jump directly from 7 to 5)
		uint8_t modeNotesToCompare[12] = {0};
		if (numNotesInCurrentScale == 7) {
			// If the source scale was 7 notes, use the transitionModeNotes (2-steps jump)
			for (int32_t n = 1; n < 12; n++) {
				modeNotesToCompare[n] = transitionModeNotes[n];
			}
		}
		else {
			// If the source scale was 6 notes, use the real modeNotes (1-step jump)
			for (int32_t n = 1; n < 12; n++) {
				modeNotesToCompare[n] = key.modeNotes[n];
			}
		}

		// First find the highest unused degree to jump from 6 to 5
		indexLastUnusedScaleDegreeFrom6To5 = 0; // Reset
		for (int32_t n = 5; n >= 1; n--) {
			if (modeNotesToCompare[n] != 0 && !notesWithinOctavePresent.has(modeNotesToCompare[n])) {
				indexLastUnusedScaleDegreeFrom6To5 = n;
				break;
			}
		}
	}

	if (numNotesInCurrentScale <= numNotesInNewScale) {
		// The new scale can perfectly fit all notes from the old one, so mark all notes as transposable
		notesWithinOctavePresent.fill();
		notesWithinOctavePresentCount = 12;
	}

	bool modeNoteNeedsTransposition;
	int8_t changes[12] = {0};
	int32_t noteNumDiff = numNotesInCurrentScale - numNotesInNewScale;

	if (numNotesInCurrentScale >= numNotesInNewScale) {
		// If the new scale is smaller or equal to old scale
		int32_t offset = noteNumDiff;
		for (int32_t n = numNotesInCurrentScale - 1; n >= 1; n--) {
			modeNoteNeedsTransposition = notesWithinOctavePresent.has(key.modeNotes[n]) && key.modeNotes[n] != 0;

			if (!modeNoteNeedsTransposition && offset > 0) {
				offset--;
				continue;
			}

			int32_t newNote = presetScaleNotes[newScale][n - offset];
			int32_t oldNote = key.modeNotes[n];
			if (modeNoteNeedsTransposition && oldNote != newNote) {
				changes[n] = newNote - oldNote;
			}
		}
	}
	else {
		// If the new scale is bigger than old scale,
		// we need to use the indexLastUnusedScaleDegree* variables to know which notes to skip
		// We now undo the changes that were saved in those flags when going from 7 to 6 notes and from 6 to 5 notes
		if (numNotesInCurrentScale == 5 && numNotesInNewScale == 7) {
			// 2-step jump from 5 to 7
			// We need to use the transition mode notes array to invert the change
			uint8_t transitionPresetScale[7] = {0};
			int32_t offset = 0;
			for (int32_t n = 1; n < 6; n++) {
				if (n == indexLastUnusedScaleDegreeFrom7To6) {
					offset++;
				}
				transitionPresetScale[n] = presetScaleNotes[newScale][n + offset];
			}
			// Move from our 5-note scale to our transitional 6-note scale (after that the notes are already in the
			// right place!)
			offset = 0;
			for (int32_t n = 1; n < 5; n++) {
				if (n == indexLastUnusedScaleDegreeFrom6To5) {
					offset++;
				}
				int32_t newNote = transitionPresetScale[n + offset];
				int32_t oldNote = key.modeNotes[n];
				if (oldNote != newNote) {
					changes[n] = newNote - oldNote;
				}
			}
		}
		else {
			// From 5 to 6 or from 6 to 7, no need for transitional scale,
			// directly use the presetScaleNotes and the corresponding indexLastUnusedScaleDegree* variable
			int32_t offset = 0;
			for (int32_t n = 1; n < 6; n++) {
				if ((numNotesInCurrentScale == 5 && n == indexLastUnusedScaleDegreeFrom6To5)
				    || (numNotesInCurrentScale == 6 && n == indexLastUnusedScaleDegreeFrom7To6)) {
					offset++;
				}
				int32_t newNote = presetScaleNotes[newScale][n + offset];
				int32_t oldNote = key.modeNotes[n];
				if (oldNote != newNote) {
					changes[n] = newNote - oldNote;
				}
			}
		}
	}

	replaceMusicalMode(changes, true);

	key.modeNotes.fromScaleNotes(presetScaleNotes[newScale]);

	return newScale;
}

// Returns CUSTOM_SCALE_WITH_MORE_THAN_7_NOTES if no preset matches current notes
int32_t Song::getCurrentPresetScale() {
	if (key.modeNotes.count() > 7) {
		return CUSTOM_SCALE_WITH_MORE_THAN_7_NOTES;
	}

	int32_t numPresetScales = NUM_PRESET_SCALES;
	for (int32_t p = 0; p < numPresetScales; p++) {
		for (int32_t n = 1; n < 7; n++) {
			int32_t newNote = presetScaleNotes[p][n];
			if (newNote == 0) {
				continue;
			}
			if (key.modeNotes[n] != newNote) {
				goto notThisOne;
			}
		}

		// If we're here, must be this one!
		return p;

notThisOne: {}
	}

	return CUSTOM_SCALE_WITH_MORE_THAN_7_NOTES;
}

// What does this do exactly, again?
void Song::ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(Sound* sound) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	for (InstrumentClip* instrumentClip : InstrumentClips::everywhere(this)) {
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(instrumentClip);
		instrumentClip->ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(modelStackWithTimelineCounter, sound);
	}
}

void Song::setTempoFromNumSamples(double newTempoSamples, bool shouldLogAction) {
	uint64_t newTimePerTimerTickBig;

	if (newTempoSamples >= (double)4294967296) {
		newTimePerTimerTickBig = -1; // Sets it to maximum unsigned 64-bit int32_t
	}
	else {
		newTimePerTimerTickBig = newTempoSamples * 4294967296 + 0.5;
		if ((newTimePerTimerTickBig >> 32) < kMinTimePerTimerTick) {
			newTimePerTimerTickBig = (uint64_t)kMinTimePerTimerTick << 32;
		}
	}

	setTimePerTimerTick(newTimePerTimerTickBig, shouldLogAction);
}

void Song::setBPM(float tempoBPM, bool shouldLogAction) {
	if (insideWorldTickMagnitude > 0) {
		tempoBPM *= ((uint32_t)1 << (insideWorldTickMagnitude));
	}
	double timePerTimerTick = (double)110250 / tempoBPM;
	if (insideWorldTickMagnitude < 0) {
		timePerTimerTick *= ((uint32_t)1 << (-insideWorldTickMagnitude));
	}
	setTempoFromNumSamples(timePerTimerTick, shouldLogAction);
}

void Song::setTempoFromParams(int32_t magnitude, int8_t whichValue, bool shouldLogAction) {
	float newBPM = metronomeValuesBPM[whichValue];
	magnitude += getInputTickMagnitude();
	if (magnitude > 0) {
		newBPM /= ((uint32_t)1 << magnitude);
	}
	else if (magnitude < 0) {
		newBPM *= ((uint32_t)1 << (0 - magnitude));
	}

	setBPM(newBPM, shouldLogAction);
}

void Song::deleteClipObject(Clip* clip, bool songBeingDestroyedToo, InstrumentRemoval instrumentRemovalInstruction) {

	if (!songBeingDestroyedToo) {

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = setupModelStackWithTimelineCounter(modelStackMemory, this, clip);

		clip->prepareForDestruction(modelStack, instrumentRemovalInstruction);
	}

#if ALPHA_OR_BETA_VERSION
	if (clip->type == ClipType::AUDIO) {
		if (((AudioClip*)clip)->recorder) {
			FREEZE_WITH_ERROR("i001"); // Trying to diversify Qui's E278
		}
	}
#endif

	void* toDealloc = dynamic_cast<void*>(clip);
	clip->~Clip();
	delugeDealloc(toDealloc);
}

int32_t Song::getMaxMIDIChannelSuffix(int32_t channel) {
	if (channel >= 16) {
		return -1; // MPE zones - just don't do freakin suffixes?
	}

	bool* inUse = (bool*)shortStringBuffer; // Only actually needs to be 27 long
	memset(inUse, 0, 27);

	int32_t maxSuffix = -2;

	for (Output* output = firstOutput; output; output = output->next) {
		if (output->type == OutputType::MIDI_OUT) {
			MIDIInstrument* instrument = (MIDIInstrument*)output;
			if (instrument->channel == channel) {
				int32_t suffix = instrument->channelSuffix;
				if (suffix < -1 || suffix >= 26) {
					continue; // Just for safety
				}
				inUse[suffix + 1] = true;
				if (suffix > maxSuffix) {
					maxSuffix = suffix;
				}
			}
		}
	}

	// Find first empty suffix
	for (int32_t s = -1; s < 26; s++) {
		if (!inUse[s + 1]) {
			if (s < maxSuffix) {
				return maxSuffix;
			}
			else {
				return s;
			}
		}
	}

	// If here, they're all full up - that's not great
	return 25; // "Z"
}

bool Song::getAnyClipsSoloing() const {
	return anyClipsSoloing;
}

void Song::reassessWhetherAnyClipsSoloing() {
	anyClipsSoloing = false;

	// For each Clip in session
	for (int32_t c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		if (clip->soloingInSessionMode) {
			anyClipsSoloing = true;
			return;
		}
	}
}

void Song::turnSoloingIntoJustPlaying(bool getRidOfArmingToo) {
	if (!anyClipsSoloing) {
		if (getRidOfArmingToo) {

			// Just get rid of arming
			for (int32_t l = 0; l < sessionClips.getNumElements(); l++) {
				Clip* loopable = sessionClips.getClipAtIndex(l);
				if (loopable->launchStyle == LaunchStyle::DEFAULT) {
					loopable->armState = ArmState::OFF;
				}
			}
		}
		return;
	}

	// Stop all other playing-but-not-soloing Clips, and turn all soloing Clip into playing Clip!
	// For each Clip in session
	for (int32_t c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		clip->activeIfNoSolo = clip->soloingInSessionMode;
		clip->soloingInSessionMode = false;

		if (getRidOfArmingToo) {
			if (clip->launchStyle != LaunchStyle::FILL && clip->launchStyle != LaunchStyle::ONCE) {
				clip->armState = ArmState::OFF;
			}
		}
	}

	anyClipsSoloing = false;
}

float Song::getTimePerTimerTickFloat() {
	return (float)timePerTimerTickBig / 4294967296;
}

uint32_t Song::getTimePerTimerTickRounded() {
	return (uint64_t)((uint64_t)timePerTimerTickBig + (uint64_t)(uint32_t)2147483648uL) >> 32; // Rounds
}

void Song::addOutput(Output* output, bool atStart) {

	if (atStart) {
		output->next = firstOutput;
		firstOutput = output;

		arrangementYScroll++;
	}
	else {
		Output** prevPointer = &firstOutput;
		while (*prevPointer) {
			prevPointer = &(*prevPointer)->next;
		}
		*prevPointer = output;
		output->next = NULL;
	}

	if (output->soloingInArrangementMode) {
		anyOutputsSoloingInArrangement = true;
	}

	// Must resync LFOs - these (if synced) will roll even when no activeClip
	if (this == currentSong) {
		output->resyncLFOs();
	}
}

// Make sure you first free all the Instrument's voices before calling this!
void Song::deleteOutputThatIsInMainList(
    Output* output,
    bool stopAnyAuditioningFirst // Usually true, but if deleting while loading a Song due to invalid data, we don't
                                 // want this, and it'd cause an error.
) {

	removeOutputFromMainList(output, stopAnyAuditioningFirst);

	output->prepareForHibernationOrDeletion();
	deleteOutput(output);
}

// Returns index, or -1 if error
int32_t Song::removeOutputFromMainList(
    Output* output,
    bool stopAnyAuditioningFirst // Usually true, but if deleting while loading a Song due to invalid data, we don't
                                 // want this, and it'd cause an error.
) {
	bool wasSoloing = output->soloingInArrangementMode;
	bool seenAnyOtherSoloing = false;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	if (stopAnyAuditioningFirst) { // See comment above.
		output->stopAnyAuditioning(modelStack);
	}

	// Remove the Output from the main list
	Output** prevPointer;
	int32_t outputIndex = 0;
	for (prevPointer = &firstOutput; *prevPointer != output; prevPointer = &(*prevPointer)->next) {
		if (*prevPointer == NULL) {
			return -1; // Shouldn't be necessary, but better to safeguard
		}

		if ((*prevPointer)->soloingInArrangementMode) {
			seenAnyOtherSoloing = true;
		}
		outputIndex++;
	}

	*prevPointer = output->next;

	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;

	int32_t bottomYDisplay = -arrangementYScroll;
	int32_t topYDisplay = bottomYDisplay + getNumOutputs();

	bottomYDisplay = std::max(0_i32, bottomYDisplay);
	topYDisplay = std::min(kDisplayHeight - 1, topYDisplay);

	int32_t yDisplay = outputIndex - arrangementYScroll;

	if (yDisplay - bottomYDisplay < topYDisplay - yDisplay) {
		arrangementYScroll--;
	}

	// If the removed Output was soloing, and we haven't yet seen any other soloing Outputs, we'd better check out
	// the rest
	if (wasSoloing && !seenAnyOtherSoloing) {
		anyOutputsSoloingInArrangement = false;
		for (; *prevPointer; prevPointer = &(*prevPointer)->next) {
			if ((*prevPointer)->soloingInArrangementMode) {
				anyOutputsSoloingInArrangement = true;
				goto allDone;
			}
		}
	}

allDone:
	return outputIndex;
}

// Hibernates or deletes old one.
// Any audio routine calls that happen during the course of this function won't have access to either the old or new
// Instrument, because neither will be in the master list when they happen
void Song::replaceInstrument(Instrument* oldOutput, Instrument* newOutput, bool keepNoteRowsWithMIDIInput) {
	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput == newOutput) {
			display->cancelPopup();
			FREEZE_WITH_ERROR("i009");
		}
	}

	// We don't detach the Instrument's activeClip here anymore. This happens in InstrumentClip::changeInstrument(),
	// called below. If we changed it here, near-future calls to the audio routine could cause new voices to be
	// sounded, with no later unassignment.

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	oldOutput->stopAnyAuditioning(modelStack);

	// Remove the oldInstrument from the list of Instruments
	Output** prevPointer;
	for (prevPointer = &firstOutput; *prevPointer != oldOutput; prevPointer = &(*prevPointer)->next) {}
	newOutput->next = oldOutput->next;
	*prevPointer = oldOutput->next;

	Clip* favourClipForCloningParamManager = NULL;

	// Migrate input MIDI channel / device. Putting this up here before any calls to changeInstrument() is good,
	// because then if a default velocity is set, for the MIDIDevice, that gets grabbed by the Clip's ParamManager
	// during that call.
	if (newOutput->type != OutputType::KIT && oldOutput->type != OutputType::KIT) {
		((MelodicInstrument*)newOutput)->midiInput = ((MelodicInstrument*)oldOutput)->midiInput;
		((MelodicInstrument*)oldOutput)->midiInput.clear();
	}

	outputClipInstanceListIsCurrentlyInvalid = true;

	// Tell all the Clips to change their Instrument.
	// For each Clip in session and arranger for specific Output...
	int32_t numElements = sessionClips.getNumElements();
	bool doingArrangementClips = false;
	// TODO: Should use AllClips, but less obvious so later.
traverseClips:
	for (int32_t c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = sessionClips.getClipAtIndex(c);
			if (clip->output != oldOutput) {
				continue;
			}
		}
		else {
			ClipInstance* clipInstance = oldOutput->clipInstances.getElement(c);
			if (!clipInstance->clip) {
				continue;
			}
			if (!clipInstance->clip->isArrangementOnlyClip()) {
				continue;
			}
			clip = clipInstance->clip;
		}

		if (oldOutput->type != OutputType::AUDIO) {
			InstrumentClip* instrumentClip = (InstrumentClip*)clip;

			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

			Error error = instrumentClip->changeInstrument(
			    modelStackWithTimelineCounter, newOutput, NULL, InstrumentRemoval::NONE,
			    (InstrumentClip*)favourClipForCloningParamManager, keepNoteRowsWithMIDIInput,
			    true); // Will call audio routine
			           // TODO: deal with errors!
		}

		// If this is the first Clip we dealt with, tell all the rest of the Clips to just clone it from this one
		// (if there isn't already a ParamManager backed up in memory for them)
		if (!favourClipForCloningParamManager) {
			favourClipForCloningParamManager = clip;
		}
	}
	if (!doingArrangementClips) {
		doingArrangementClips = true;
		numElements = oldOutput->clipInstances.getNumElements();
		goto traverseClips;
	}

	// Migrate all ClipInstances from oldInstrument to newInstrument
	newOutput->clipInstances.swapStateWith(&oldOutput->clipInstances);

	outputClipInstanceListIsCurrentlyInvalid = false;

	// Copy default velocity
	newOutput->defaultVelocity = oldOutput->defaultVelocity;

	newOutput->colour = oldOutput->colour;
	oldOutput->colour = 0;

	newOutput->mutedInArrangementMode = oldOutput->mutedInArrangementMode;
	oldOutput->mutedInArrangementMode = false;

	newOutput->soloingInArrangementMode = oldOutput->soloingInArrangementMode;
	oldOutput->soloingInArrangementMode = false;

	newOutput->armedForRecording = oldOutput->armedForRecording;
	oldOutput->armedForRecording = false;

	// Properly do away with the oldInstrument
	deleteOrAddToHibernationListOutput(oldOutput);

	// Put the newInstrument into the master list
	*prevPointer = newOutput;

	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
}

// NOTE: for Instruments not currently in any list
void Song::deleteOrAddToHibernationListOutput(Output* output) {
	// If un-edited (which will include all CV Instruments, and any MIDI without mod knob assignments)
	if (output->type == OutputType::AUDIO || output->type == OutputType::CV || !((Instrument*)output)->editedByUser) {
		output->prepareForHibernationOrDeletion();
		deleteOutput(output);
	}

	// Otherwise, hibernate it
	else {
		addInstrumentToHibernationList((Instrument*)output);
	}
}

// NOTE: for Instruments currently in the main list
void Song::deleteOrHibernateOutput(Output* output) {
	// If edited (which won't include any CV Instruments), just hibernate it. Only allowed for audio Instruments
	if (output->type != OutputType::CV && output->type != OutputType::AUDIO) {
		Instrument* instrument = (Instrument*)output;
		if (!instrument->editedByUser) {
			goto deleteIt;
		}

		moveInstrumentToHibernationList(instrument);
	}

	// Otherwise, we can delete it
	else {
deleteIt:
		deleteOutputThatIsInMainList(output);
	}
}

void Song::deleteOutput(Output* output) {
	output->deleteBackedUpParamManagers(this);
	void* toDealloc = dynamic_cast<void*>(output);
	output->~Output();
	delugeDealloc(toDealloc);
}

void Song::moveInstrumentToHibernationList(Instrument* instrument) {

	removeOutputFromMainList(instrument);

	if (instrument->type == OutputType::MIDI_OUT) {
		setHibernatingMIDIInstrument((MIDIInstrument*)instrument);
	}
	else {
		addInstrumentToHibernationList(instrument);
	}
}

void Song::addInstrumentToHibernationList(Instrument* instrument) {
	instrument->prepareForHibernationOrDeletion();
	instrument->next = firstHibernatingInstrument;
	firstHibernatingInstrument = instrument;
	instrument->setActiveClip(nullptr); // Just be sure!
	instrument->inValidState = false;
}

void Song::removeInstrumentFromHibernationList(Instrument* instrument) {

	// Remove the Instrument from the list of Instruments of this type
	Instrument** prevPointer;
	for (prevPointer = &firstHibernatingInstrument; *prevPointer != instrument;
	     prevPointer = (Instrument**)&(*prevPointer)->next) {
		if (*prevPointer == NULL) {
			return; // Shouldn't be necessary, but better to safeguard
		}
	}

	*prevPointer = (Instrument*)instrument->next;
}

void Song::deleteOrHibernateOutputIfNoClips(Output* output) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	output->pickAnActiveClipIfPossible(modelStack, true, PgmChangeSend::ONCE, false);

	// If no other Clips have this Output...
	if (!output->getActiveClip()) {
		deleteOrHibernateOutput(output);
	}
}

void Song::deleteHibernatingInstrumentWithSlot(OutputType outputType, char const* name) {

	// Remove the Instrument from the list of Instruments of this type
	Instrument** prevPointer = &firstHibernatingInstrument;
	while (true) {
		Instrument* instrument = *prevPointer;

		if (instrument == NULL) {
			return; // Shouldn't be necessary, but better to safeguard
		}

		if (instrument->type == outputType) {

			if (!strcasecmp(name, instrument->name.get())) {
				*prevPointer = (Instrument*)instrument->next;

				deleteOutput(instrument);

				return;
			}
		}

		prevPointer = (Instrument**)&instrument->next;
	}
}

void Song::markAllInstrumentsAsEdited() {
	for (Output* output = firstOutput; output; output = output->next) {
		if (output->type == OutputType::AUDIO) {
			continue;
		}

		Instrument* instrument = (Instrument*)output;
		bool shouldMoveToEmptySlot = false; // Deprecated. See newer branch.
		instrument->beenEdited(shouldMoveToEmptySlot);
	}
}

AudioOutput* Song::getAudioOutputFromName(String* name) {
	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput->type == OutputType::AUDIO) {
			if (thisOutput->name.equalsCaseIrrespective(name)) {
				return (AudioOutput*)thisOutput;
			}
		}
	}
	return NULL;
}

// You can put name as NULL if it's MIDI or CV
Instrument* Song::getInstrumentFromPresetSlot(OutputType outputType, int32_t channel, int32_t channelSuffix,
                                              char const* name, char const* dirPath, bool searchHibernating,
                                              bool searchNonHibernating) {

	if (searchNonHibernating) {
		for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
			if (thisOutput->type == outputType) {

				bool match;

				if (outputType == OutputType::SYNTH || outputType == OutputType::KIT) {
					match = !strcasecmp(name, thisOutput->name.get())
					        && !strcasecmp(dirPath, ((Instrument*)thisOutput)->dirPath.get());
				}

				else {
					match = (((NonAudioInstrument*)thisOutput)->channel == channel
					         && (outputType == OutputType::CV
					             || ((MIDIInstrument*)thisOutput)->channelSuffix == channelSuffix));
				}

				if (match) {
					return (Instrument*)thisOutput;
				}
			}
		}
	}

	if (searchHibernating) {
		for (Output* thisOutput = firstHibernatingInstrument; thisOutput; thisOutput = thisOutput->next) {
			if (thisOutput->type == outputType) {

				bool match;

				if (outputType == OutputType::SYNTH || outputType == OutputType::KIT) {
					match = !strcasecmp(name, thisOutput->name.get())
					        && !strcasecmp(dirPath, ((Instrument*)thisOutput)->dirPath.get());
				}

				else {
					match = (((NonAudioInstrument*)thisOutput)->channel == channel
					         && ((MIDIInstrument*)thisOutput)->channelSuffix == channelSuffix);
				}

				if (match) {
					return (Instrument*)thisOutput;
				}
			}
		}
	}

	return NULL;
}

int32_t Song::getOutputIndex(Output* output) {
	int32_t count = 0;
	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput == output) {
			return count;
		}
		count++;
	}

	return 0; // fail
}

Output* Song::getOutputFromIndex(int32_t index) {
	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		if (!index) {
			return thisOutput;
		}
		index--;
	}

	return firstOutput; // fail
}

int32_t Song::getNumOutputs() {
	int32_t count = 0;
	for (Output* output = firstOutput; output; output = output->next) {
		count++;
	}
	return count;
}

void Song::reassessWhetherAnyOutputsSoloingInArrangement() {
	anyOutputsSoloingInArrangement = false;
	for (Output* output = firstOutput; output; output = output->next) {
		if (output->soloingInArrangementMode) {
			anyOutputsSoloingInArrangement = true;
			return;
		}
	}
}

bool Song::getAnyOutputsSoloingInArrangement() {
	return anyOutputsSoloingInArrangement;
}

void Song::setupPatchingForAllParamManagers() {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	for (InstrumentClip* instrumentClip : InstrumentClips::everywhere(this)) {

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(instrumentClip);

		// TODO: we probably don't need to call this so often anymore?
		AudioEngine::routineWithClusterLoading(); // -----------------------------------
		AudioEngine::logAction("aaa4.26");
		((Instrument*)instrumentClip->output)->setupPatching(modelStackWithTimelineCounter);
		AudioEngine::logAction("aaa4.27");
	}
}

// Returns NULL if couldn't find one.
// Supply stealInto to have it delete the "backed up" element, putting the contents into stealInto.
ParamManager* Song::getBackedUpParamManagerForExactClip(ModControllableAudio* modControllable, Clip* clip,
                                                        ParamManager* stealInto) {

	uint32_t keyWords[2];
	keyWords[0] = (uint32_t)modControllable;
	keyWords[1] = (uint32_t)clip;

	int32_t iCorrectClip = backedUpParamManagers.searchMultiWordExact(keyWords);

	if (iCorrectClip == -1) {
		return NULL;
	}

	BackedUpParamManager* elementCorrectClip =
	    (BackedUpParamManager*)backedUpParamManagers.getElementAddress(iCorrectClip);

	if (stealInto) {
		stealInto->stealParamCollectionsFrom(
		    &elementCorrectClip->paramManager,
		    true); // Steal expression params too - if they're here (slightly rare case).
		backedUpParamManagers.deleteAtIndex(iCorrectClip);
		return stealInto;
	}
	else {
		return &elementCorrectClip->paramManager;
	}
}

// If none for the correct Clip, return one for a different Clip - prioritizing NULL Clip.
// Returns NULL if couldn't find one.
// Supply stealInto to have it delete the "backed up" element, putting the contents into stealInto.
ParamManager* Song::getBackedUpParamManagerPreferablyWithClip(ModControllableAudio* modControllable, Clip* clip,
                                                              ParamManager* stealInto) {

	int32_t iAnyClip =
	    backedUpParamManagers.search((uint32_t)modControllable, GREATER_OR_EQUAL); // Search just by first word
	if (iAnyClip >= backedUpParamManagers.getNumElements()) {
		return NULL;
	}
	BackedUpParamManager* elementAnyClip = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(iAnyClip);
	if (elementAnyClip->modControllable != modControllable) {
		return NULL; // If nothing with even the correct modControllable at all, get out
	}

	int32_t iCorrectClip;
	BackedUpParamManager* elementCorrectClip;

	if (!clip || elementAnyClip->clip == clip) {
returnFirstForModControllableEvenIfNotRightClip:
		iCorrectClip = iAnyClip;
		elementCorrectClip = elementAnyClip;
	}
	else {
		uint32_t keyWords[2];
		keyWords[0] = (uint32_t)modControllable;
		keyWords[1] = (uint32_t)clip;
		iCorrectClip = backedUpParamManagers.searchMultiWordExact(keyWords, NULL, iAnyClip + 1);
		if (iCorrectClip == -1) {
			goto returnFirstForModControllableEvenIfNotRightClip;
		}
		elementCorrectClip = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(iCorrectClip);
	}

	if (stealInto) {
		stealInto->stealParamCollectionsFrom(
		    &elementCorrectClip->paramManager,
		    true); // Steal expression params too - if they're here (slightly rare case).
		backedUpParamManagers.deleteAtIndex(iCorrectClip);
		return stealInto;
	}
	else {
		return &elementCorrectClip->paramManager;
	}
}

// Steals stuff.
// shouldStealExpressionParamsToo should only be true to save expression params from being destructed (e.g. if the
// Clip is being destructed).
void Song::backUpParamManager(ModControllableAudio* modControllable, Clip* clip, ParamManagerForTimeline* paramManager,
                              bool shouldStealExpressionParamsToo) {

	if (!paramManager->containsAnyMainParamCollections()) {
		return;
	}

	uint32_t keyWords[2];
	keyWords[0] = (uint32_t)modControllable;
	keyWords[1] = (uint32_t)clip;

	int32_t indexToInsertAt;

	int32_t i = backedUpParamManagers.searchMultiWordExact(keyWords, &indexToInsertAt);

	BackedUpParamManager* element;

	// If one already existed...
	if (i != -1) {
		element = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(i);

		// Let's destroy it...
		element->paramManager.destructAndForgetParamCollections();

		// ...and replace it
doStealing:
		element->paramManager.stealParamCollectionsFrom(paramManager, shouldStealExpressionParamsToo);
	}

	// Otherwise, insert one
	else {
		i = indexToInsertAt;
		Error error = backedUpParamManagers.insertAtIndex(i);

		// If RAM error...
		if (error != Error::NONE) {

			// Destroy paramManager
			paramManager->destructAndForgetParamCollections();
		}

		// Or if that went fine...
		else {
			element = new (backedUpParamManagers.getElementAddress(i)) BackedUpParamManager();

			element->modControllable = modControllable;
			element->clip = clip;
			goto doStealing;
		}
	}
}

void Song::deleteBackedUpParamManagersForClip(Clip* clip) {

	AudioEngine::logAction("Song::deleteBackedUpParamManagersForClip");

	// Ok, this is the one sticky one where we actually do have to go through every element
	int32_t i = 0;

	while (i < backedUpParamManagers.getNumElements()) {

		BackedUpParamManager* backedUp = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(i);
		if (backedUp->clip == clip) {

			AudioEngine::routineWithClusterLoading(); // -----------------------------------

			// We ideally want to just set the Clip to NULL. We can just do this if the previous element didn't have
			// the same ModControllable
			if (i == 0
			    || ((BackedUpParamManager*)backedUpParamManagers.getElementAddress(i - 1))->modControllable
			           != backedUp->modControllable) {
				backedUp->clip = NULL;
				i++;
			}

			// Othwerwise...
			else {

				ParamManagerForTimeline paramManager;
				paramManager.stealParamCollectionsFrom(&backedUp->paramManager);
				ModControllableAudio* modControllable = backedUp->modControllable;

				// We have to delete that element...
				backedUpParamManagers.deleteAtIndex(i);

				// ...and then go find the first one that had this ModControllable
				int32_t j = backedUpParamManagers.search((uint32_t)modControllable, GREATER_OR_EQUAL, 0,
				                                         i); // Search by first word only
				BackedUpParamManager* firstElementWithModControllable =
				    (BackedUpParamManager*)backedUpParamManagers.getElementAddress(j);

				// If it already had a NULL Clip, we have to replace its ParamManager
				if (!firstElementWithModControllable->clip) {
					firstElementWithModControllable->paramManager.destructAndForgetParamCollections();

					firstElementWithModControllable->paramManager.stealParamCollectionsFrom(&paramManager);

					// Don't increment i, as we've deleted an element instead
				}

				// Otherwise, we insert before it
				else {
					Error error = backedUpParamManagers.insertAtIndex(j);

					// If RAM error (surely would never happen since we just deleted an element)...
					if (error != Error::NONE) {
						// Don't increment i, as we've deleted an element instead
					}

					// Or if that went fine...
					else {
						BackedUpParamManager* newElement =
						    new (backedUpParamManagers.getElementAddress(j)) BackedUpParamManager();

						newElement->modControllable = modControllable;
						newElement->clip = NULL;
						newElement->paramManager.stealParamCollectionsFrom(&paramManager);
						i++; // We deleted an element, but inserted one too
					}
				}
			}
		}
		else {
			i++;
		}
	}

	// Test that everything's still in order

#if ALPHA_OR_BETA_VERSION
	AudioEngine::routineWithClusterLoading(); // -----------------------------------

	Clip* lastClip;
	ModControllableAudio* lastModControllable;

	for (int32_t i = 0; i < backedUpParamManagers.getNumElements(); i++) {

		BackedUpParamManager* backedUp = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(i);

		if (i >= 1) {

			if (backedUp->modControllable < lastModControllable) {
				FREEZE_WITH_ERROR("E053");
			}

			else if (backedUp->modControllable == lastModControllable) {
				if (backedUp->clip < lastClip) {
					FREEZE_WITH_ERROR("E054");
				}
				else if (backedUp->clip == lastClip) {
					FREEZE_WITH_ERROR("E055");
				}
			}
		}

		lastClip = backedUp->clip;
		lastModControllable = backedUp->modControllable;
	}

#endif
}

void Song::deleteBackedUpParamManagersForModControllable(ModControllableAudio* modControllable) {

	int32_t iAnyClip =
	    backedUpParamManagers.search((uint32_t)modControllable, GREATER_OR_EQUAL); // Search by first word only

	while (true) {
		if (iAnyClip >= backedUpParamManagers.getNumElements()) {
			return;
		}
		BackedUpParamManager* elementAnyClip = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(iAnyClip);
		if (elementAnyClip->modControllable != modControllable) {
			return;
		}

		// Destruct paramManager
		elementAnyClip->~BackedUpParamManager();

		// Delete from Vector
		backedUpParamManagers.deleteAtIndex(iAnyClip);
	}
}

// TODO: should we also check whether any arranger clips active and playing in session mode? For next function
// too...
bool Song::doesOutputHaveActiveClipInSession(Output* output) {

	for (Clip* clip : AllClips::inSession(this)) {
		if (isClipActive(clip) && clip->output == output) {
			return true;
		}
	}

	return false;
}

// This is for non-audio Instruments only, so no name is relevant
bool Song::doesNonAudioSlotHaveActiveClipInSession(OutputType outputType, int32_t slot, int32_t subSlot) {

	// For each Clip in session
	for (Clip* clip : AllClips::inSession(this)) {

		if (isClipActive(clip) && clip->type == ClipType::INSTRUMENT) {

			Instrument* instrument = (Instrument*)clip->output;

			if (instrument->type == outputType && ((NonAudioInstrument*)instrument)->channel == slot
			    && (outputType == OutputType::CV || ((MIDIInstrument*)instrument)->channelSuffix == subSlot)) {
				return true;
			}
		}
	}

	return false;
}

bool Song::doesOutputHaveAnyClips(Output* output) {
	// Check arranger ones first via clipInstances
	// TODO: why is this better than arrangerOnlyClips? Is this just a performance
	// consideration?
	for (int32_t i = 0; i < output->clipInstances.getNumElements(); i++) {
		ClipInstance* thisInstance = output->clipInstances.getElement(i);
		if (thisInstance->clip) {
			return true;
		}
	}

	// Then session ones
	for (Clip* clip : AllClips::inSession(this)) {
		if (clip->output == output) {
			return true;
		}
	}

	return false;
}

void Song::restoreClipStatesBeforeArrangementPlay() {

	// For each Clip in session
	for (int32_t c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		clip->activeIfNoSolo = clip->wasActiveBefore;
		clip->soloingInSessionMode = false;
	}

	anyClipsSoloing = false;

	// Do not set the Instruments' activeClips. We want them to stay as they were when the song ended
}

// Returns 0 if they're all full
int32_t Song::getLowestSectionWithNoSessionClipForOutput(Output* output) {
	bool* sectionRepresented = (bool*)shortStringBuffer;
	memset(sectionRepresented, 0, kMaxNumSections);

	for (int32_t c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);
		if (clip->output == output && clip->section < kMaxNumSections) {
			sectionRepresented[clip->section] = true;
		}
	}

	for (int32_t s = 0; s < kMaxNumSections; s++) {
		if (!sectionRepresented[s]) {
			return s;
		}
	}

	return 0;
}

void Song::assertActiveness(ModelStackWithTimelineCounter* modelStack, int32_t endInstanceAtTime) {

	Clip* theActiveClip = (Clip*)modelStack->getTimelineCounter();

	bool anyClipStoppedSoloing = false;

	Output* output = theActiveClip->output;

	// Stop any Clips with same Output
	// For each Clip in session and arranger for specific Output
	int32_t numElements = sessionClips.getNumElements();
	bool doingArrangementClips = false;
	// TODO: Should use AllClips(), but less obvious so later.
traverseClips:
	for (int32_t c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = sessionClips.getClipAtIndex(c);
			if (clip->output != output) {
				continue;
			}
		}
		else {
			ClipInstance* clipInstance = output->clipInstances.getElement(c);
			if (!clipInstance->clip) {
				continue;
			}
			if (!clipInstance->clip->isArrangementOnlyClip()) {
				continue;
			}
			clip = clipInstance->clip;
		}

		if (clip != theActiveClip && isClipActive(clip)) {

			if (playbackHandler.isEitherClockActive() && currentSong == this) {
				clip->expectNoFurtherTicks(this, true);
				if (playbackHandler.recording == RecordingMode::ARRANGEMENT && endInstanceAtTime != -1) {
					clip->getClipToRecordTo()->endInstance(endInstanceAtTime);
				}
			}

			if (clip->soloingInSessionMode) {
				clip->soloingInSessionMode = false;
				anyClipStoppedSoloing = true;
			}
			else {
				clip->activeIfNoSolo = false;
			}
		}
	}
	if (!doingArrangementClips) {
		doingArrangementClips = true;
		numElements = output->clipInstances.getNumElements();
		goto traverseClips;
	}

	if (anyClipStoppedSoloing) {
		reassessWhetherAnyClipsSoloing();
	}
	output->setActiveClip(modelStack);
}

bool Song::isClipActive(Clip const* clip) const {
	return clip->soloingInSessionMode || (clip->activeIfNoSolo && !getAnyClipsSoloing());
}

void Song::sendAllMIDIPGMs() {
	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		thisOutput->sendMIDIPGM();
	}
}

// This is only called right after Song loaded, so we can assume all Instruments have a NULL activeClip
// It's not possible for this to stop there from being more than zero soloing Clips
void Song::sortOutWhichClipsAreActiveWithoutSendingPGMs(ModelStack* modelStack,
                                                        int32_t playbackWillStartInArrangerAtPos) {

	AudioEngine::logAction("aaa5.11");

	// If beginning playback in arranger, that's where we figure out which Clips are active - on their Outputs, and
	// generally
	if (playbackWillStartInArrangerAtPos != -1) {
		anyClipsSoloing = false;

		// We still want as many Outputs as possible to have activeClips, even if those Clips are not "active".
		// First, try arranger-only Clips.
		for (Output* output = firstOutput; output; output = output->next) {

			// Don't do any additional searching of session Clips, cos it'd be really inefficient searching all
			// session Clips for each Output
			output->pickAnActiveClipForArrangementPos(modelStack, playbackWillStartInArrangerAtPos,
			                                          PgmChangeSend::NEVER);
		}
	}

	// If not about to start playback in arranger, we give the active session Clips first dibs on being active on
	// their Output
	else {

		int32_t count = 0;

		for (int32_t c = 0; c < sessionClips.getNumElements(); c++) {
			Clip* clip = sessionClips.getClipAtIndex(c);

			if (!(count & 3)) {
				AudioEngine::routineWithClusterLoading(); // -----------------------------------
				AudioEngine::logAction("aaa5.114");
			}
			count++;

			// If Clip is supposedly active...
			if (isClipActive(clip)) {

				// If the Instrument already had a Clip, we gotta be inactive...
				if (clip->output->getActiveClip()) {
					if (getAnyClipsSoloing()) {
						clip->soloingInSessionMode = false;
					}
					else {
						clip->activeIfNoSolo = false;
					}
				}

				// Otherwise, it's ours
				else {
					clip->output->setActiveClip(modelStack->addTimelineCounter(clip), PgmChangeSend::NEVER);
				}
			}
		}

		AudioEngine::logAction("aaa5.115");

		// We still want as many Outputs as possible to have activeClips, even if those Clips are not "active".
		// First, try arranger-only Clips
		for (Output* output = firstOutput; output; output = output->next) {

			// Don't do any additional searching of session Clips, cos it'd be really inefficient searching all
			// session Clips for each Instrument
			output->pickAnActiveClipIfPossible(modelStack, false, PgmChangeSend::NEVER, false);
		}
	}
	AudioEngine::logAction("aaa5.12");

	int32_t count = 0;

	// And finally, go through session Clips again, giving any more to Instruments that can be given
	for (int32_t c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);
		if (!(count & 7)) {
			AudioEngine::routineWithClusterLoading(); // -----------------------------------
			AudioEngine::logAction("aaa5.125");
		}
		count++;

		// And if beginning arranger playback, some additional setting up to do at the same time
		if (playbackWillStartInArrangerAtPos != -1) {
			clip->wasActiveBefore = clip->activeIfNoSolo;
			clip->soloingInSessionMode = false;
			if (!clip->output->getActiveClip()) {
				clip->activeIfNoSolo = false;
			}
		}

		if (!clip->output->getActiveClip()) {
			clip->output->setActiveClip(modelStack->addTimelineCounter(clip), PgmChangeSend::NEVER);
		}
	}

	AudioEngine::logAction("aaa5.13");

	// Now, we want to ensure (in case of bad song file data) that any Output that doesn't have an activeClip (aka
	// doesn't have ANY Clip) definitely does have a backedUpParamManager. Only for audio Instruments
	for (Output* output = firstOutput; output;) {

		Output* nextOutput = output->next;

		// Also while we're at it, for SoundInstruments (Synths), get them to grab a copy of the arp settings
		if (output->getActiveClip()) {
			if (output->type == OutputType::SYNTH) {
				((SoundInstrument*)output)
				    ->defaultArpSettings.cloneFrom(&((InstrumentClip*)output->getActiveClip())->arpSettings);
			}
		}

		// Ok, back to the main task - if there's no activeClip...
		else {
			if (output->type == OutputType::SYNTH || output->type == OutputType::KIT) {
				if (!getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)output->toModControllable(),
				                                               NULL)) {
#if ALPHA_OR_BETA_VERSION
					display->displayPopup("E044");
#endif
					deleteOutputThatIsInMainList(output,
					                             false); // Do *not* try to stop any auditioning first. There is
					                                     // none, and doing so would/did cause an E170.
					goto getOut;
				}
			}

			output->setupWithoutActiveClip(modelStack);
		}

#if ALPHA_OR_BETA_VERSION
		// For Kits, ensure that every audio Drum has a ParamManager somewhere
		if (output->type == OutputType::KIT) {
			Kit* kit = (Kit*)output;
			for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					SoundDrum* soundDrum = (SoundDrum*)thisDrum;
					if (!getBackedUpParamManagerPreferablyWithClip(soundDrum,
					                                               NULL)) { // If no backedUpParamManager...
						if (!findParamManagerForDrum(kit,
						                             soundDrum)) { // If no ParamManager with a NoteRow somewhere...
							FREEZE_WITH_ERROR("E102");
						}
					}
				}
			}
		}
#endif

getOut:
		output = nextOutput;
	}

	AudioEngine::logAction("aaa5.14");
}

// Can assume that no soloing when this is called, i.e. any Clips in here which say they're active actually are
void Song::deactivateAnyArrangementOnlyClips() {
	for (Clip* clip : AllClips::inArrangementOnly(this)) {
		if (clip->activeIfNoSolo) {
			clip->expectNoFurtherTicks(this);
			clip->activeIfNoSolo = false;
		}
	}
}

Clip* Song::getLongestClip(bool includeInactive, bool includeArrangementOnly) {
	Clip* longestClip = NULL;

	// TODO: What about "includeArrangementOnly" ??? It was unused before the loop got changed
	// to use AllClips::everywhere(this), but uh-huh.

	// For each Clip in session and arranger
	for (Clip* clip : AllClips::everywhere(this)) {

		if (includeInactive || isClipActive(clip)) {
			if (!longestClip || clip->loopLength > longestClip->loopLength) {
				longestClip = clip;
			}
		}
	}

	return longestClip;
}

// If no such Clip exists, removes the multiple-or-factor criteria
// Includes arrangement-only Clips, which might still be playing
Clip* Song::getLongestActiveClipWithMultipleOrFactorLength(int32_t targetLength, bool revertToAnyActiveClipIfNone,
                                                           Clip* excludeClip) {
	Clip* foundClip = NULL;
	bool foundClipIsFitting = false;
	int32_t foundClipLength;

	// For each Clip in session and arranger
	for (Clip* clip : AllClips::everywhere(this)) {

		if (clip != excludeClip && isClipActive(clip) && clip->launchStyle != LaunchStyle::FILL) {
			int32_t clipLength = clip->loopLength;
			if (clipLength == targetLength
			    || (clipLength > targetLength && ((uint32_t)clipLength % (uint32_t)targetLength) == 0)
			    || (targetLength > clipLength && ((uint32_t)targetLength % (uint32_t)clipLength) == 0)) {
				if (!foundClipIsFitting || !foundClip || clipLength > foundClipLength) {
					foundClip = clip;
					foundClipIsFitting = true;
					foundClipLength = clipLength;
				}
			}
			else {
				if (revertToAnyActiveClipIfNone && !foundClipIsFitting) {
					foundClip = clip;
				}
			}
		}
	}

	return foundClip;
}

bool Song::isOutputActiveInArrangement(Output* output) {
	return (output->soloingInArrangementMode
	        || (!getAnyOutputsSoloingInArrangement() && !output->mutedInArrangementMode));
}

void Song::setHibernatingMIDIInstrument(MIDIInstrument* newInstrument) {
	deleteHibernatingMIDIInstrument();

	hibernatingMIDIInstrument = newInstrument;
}

void Song::deleteHibernatingMIDIInstrument() {
	if (hibernatingMIDIInstrument) {
		void* toDealloc = dynamic_cast<void*>(hibernatingMIDIInstrument);
		hibernatingMIDIInstrument->~MIDIInstrument();
		delugeDealloc(toDealloc);
		hibernatingMIDIInstrument = NULL;
	}
}

MIDIInstrument* Song::grabHibernatingMIDIInstrument(int32_t channel, int32_t channelSuffix) {
	MIDIInstrument* toReturn = hibernatingMIDIInstrument;
	hibernatingMIDIInstrument = NULL;
	if (toReturn) {
		toReturn->setActiveClip(nullptr, PgmChangeSend::NEVER); // Not really necessary?
		toReturn->inValidState = false;

		toReturn->channel = channel;
		toReturn->channelSuffix = channelSuffix;
	}
	return toReturn;
}

void Song::stopAllMIDIAndGateNotesPlaying() {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	for (InstrumentClip* instrumentClip : InstrumentClips::everywhere(this)) {
		if (isClipActive(instrumentClip) && instrumentClip->output->type != OutputType::SYNTH) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
			    modelStack->addTimelineCounter(instrumentClip);
			instrumentClip->stopAllNotesPlaying(modelStackWithTimelineCounter);
		}
	}
}

void Song::stopAllAuditioning() {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	for (Output* output = firstOutput; output; output = output->next) {
		output->stopAnyAuditioning(modelStack);
	}
}

void Song::ensureAllInstrumentsHaveAClipOrBackedUpParamManager(char const* errorMessageNormal,
                                                               char const* errorMessageHibernating) {

#if ALPHA_OR_BETA_VERSION

	// Non-hibernating Instruments
	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput->type != OutputType::SYNTH && thisOutput->type != OutputType::KIT) {
			continue;
		}

		AudioEngine::routineWithClusterLoading(); // -----------------------------------

		// If has Clip, that's fine
		if (getClipWithOutput(thisOutput)) {}

		else {
			if (!getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)thisOutput->toModControllable(),
			                                               NULL)) {
				FREEZE_WITH_ERROR(errorMessageNormal);
			}
		}
	}

	// And hibernating Instruments
	for (Instrument* thisInstrument = firstHibernatingInstrument; thisInstrument;
	     thisInstrument = (Instrument*)thisInstrument->next) {
		if (thisInstrument->type != OutputType::SYNTH && thisInstrument->type != OutputType::KIT) {
			continue;
		}

		AudioEngine::routineWithClusterLoading(); // -----------------------------------

		// If has Clip, it shouldn't!
		if (getClipWithOutput(thisInstrument)) {
			// gtridr got, V4.0.0-beta2. Before I fixed memory corruption issues, so hopefully could just be that.
			FREEZE_WITH_ERROR("E056");
		}

		else {
			if (!getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)thisInstrument->toModControllable(),
			                                               NULL)) {
				FREEZE_WITH_ERROR(errorMessageHibernating);
			}
		}
	}
#endif
}

Error Song::placeFirstInstancesOfActiveClips(int32_t pos) {

	// For each Clip in session
	for (int32_t c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		if (isClipActive(clip)) {
			int32_t clipInstanceI = clip->output->clipInstances.getNumElements();
			Error error = clip->output->clipInstances.insertAtIndex(clipInstanceI);
			if (error != Error::NONE) {
				return error;
			}

			ClipInstance* clipInstance = clip->output->clipInstances.getElement(clipInstanceI);
			clipInstance->clip = clip;
			clipInstance->length = clip->loopLength;
			clipInstance->pos = pos;
		}
	}

	return Error::NONE;
}

// Normally we leave detachClipsToo as false, becuase we need to keep them attached because
// resumeClipsClonedForArrangementRecording() is about to be called, and needs them attached, and will detach them
// itself
void Song::endInstancesOfActiveClips(int32_t pos, bool detachClipsToo) {

	// For each Clip in session
	for (int32_t c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		if (isClipActive(clip)) {

			Clip* clipNow = clip->getClipToRecordTo();

			if (detachClipsToo) {
				clipNow->beingRecordedFromClip = NULL;
			}

			int32_t clipInstanceI = clip->output->clipInstances.search(pos + 1, LESS);
			if (clipInstanceI >= 0) {
				ClipInstance* clipInstance = clip->output->clipInstances.getElement(clipInstanceI);
				if (clipInstance->clip == clipNow) {
					int32_t newLength = pos - clipInstance->pos;
					if (newLength == 0) {
						clip->output->clipInstances.deleteAtIndex(clipInstanceI);
					}
					else {
						clipInstance->length = newLength;
					}
				}
			}
		}
	}
}

void Song::resumeClipsClonedForArrangementRecording() {

	char modelStackMemoryClone[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStackClone = setupModelStackWithSong(modelStackMemoryClone, this);

	char modelStackMemoryOriginal[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStackOriginal = setupModelStackWithSong(modelStackMemoryOriginal, this);

	// For each Clip in session
	for (int32_t c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* originalClip = sessionClips.getClipAtIndex(c);

		Clip* clonedClip = originalClip->output->getActiveClip();
		if (clonedClip && clonedClip->beingRecordedFromClip == originalClip) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounterClone =
			    modelStackClone->addTimelineCounter(clonedClip);
			ModelStackWithTimelineCounter* modelStackWithTimelineCounterOriginal =
			    modelStackOriginal->addTimelineCounter(originalClip);
			clonedClip->resumeOriginalClipFromThisClone(modelStackWithTimelineCounterOriginal,
			                                            modelStackWithTimelineCounterClone);
		}
	}
}

void Song::clearArrangementBeyondPos(int32_t pos, Action* action) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStack = setupModelStackWithSongAsTimelineCounter(modelStackMemory);
	paramManager.trimToLength(pos, modelStack, action, false);

	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		int32_t i = thisOutput->clipInstances.search(pos, GREATER_OR_EQUAL);

		// We go through deleting the ClipInstances one by one. This is actually quite inefficient, but complicated
		// to improve on because the deletion of the Clips themselves, where there are arrangement-only ones, causes
		// the calling of output->pickAnActiveClipIfPossible. So we have to ensure that extra ClipInstances don't
		// exist at any instant in time, or else it'll look at those to pick the new activeClip, which might not
		// exist anymore.
		for (int32_t j = thisOutput->clipInstances.getNumElements() - 1; j >= i; j--) {
			ClipInstance* clipInstance = thisOutput->clipInstances.getElement(j);
			if (action) {
				action->recordClipInstanceExistenceChange(thisOutput, clipInstance, ExistenceChangeType::DELETE);
			}
			Clip* clip = clipInstance->clip;
			thisOutput->clipInstances.deleteAtIndex(j);

			deletingClipInstanceForClip(thisOutput, clip, action,
			                            true); // Could be bad that this calls the audio routine before we've
			                                   // actually deleted the ClipInstances...
		}

		// Shorten the previous one if need be
		int32_t numElements = thisOutput->clipInstances.getNumElements();
		if (numElements) {
			ClipInstance* clipInstance = thisOutput->clipInstances.getElement(numElements - 1);
			int32_t maxLength = pos - clipInstance->pos;
			if (clipInstance->length > maxLength) {
				clipInstance->change(action, thisOutput, clipInstance->pos, maxLength, clipInstance->clip);
			}
		}
	}
}

// Will call audio routine!!
// Note: in most cases (when action supplied), will try to pick a new activeClip even if told not to. But this
// should be ok
void Song::deletingClipInstanceForClip(Output* output, Clip* clip, Action* action, bool shouldPickNewActiveClip) {

	// If clipInstance had a Clip, and it's a (white) arrangement-only Clip, then the whole Clip needs deleting.
	if (clip && clip->isArrangementOnlyClip()) {

		void* memory = NULL;

		bool deletionDone = false;

		if (action) {
			deletionDone =
			    action->recordClipExistenceChange(this, &arrangementOnlyClips, clip, ExistenceChangeType::DELETE);
			// That call will call pickAnActiveClipIfPossible() whether we like it or not...
		}

		if (!deletionDone) { // If not enough memory to create undo-history...
			actionLogger.deleteAllLogs();
			// Delete the actual Clip.
			// Will not remove Instrument from Song. Will call audio routine! That's why we deleted the ClipInstance
			// first
			int32_t index = arrangementOnlyClips.getIndexForClip(clip);
			if (index != -1) {
				arrangementOnlyClips.deleteAtIndex(index); // Shouldn't actually ever not be found
			}
			deleteClipObject(clip, false, InstrumentRemoval::NONE);
			if (shouldPickNewActiveClip) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

				output->pickAnActiveClipIfPossible(modelStack, true);
			}
		}
	}
}

bool Song::arrangementHasAnyClipInstances() {
	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput->clipInstances.getNumElements()) {
			return true;
		}
	}
	return false;
}

void Song::setParamsInAutomationMode(bool newState) {

	if (paramsInAutomationMode == newState) {
		return;
	}

	paramsInAutomationMode = newState;

	UnpatchedParamSet* unpatchedParams = paramManager.getUnpatchedParamSet();

	// If going automated...
	if (newState) {

		// Back up the unautomated values
		for (int32_t p = 0; p < params::kMaxNumUnpatchedParams; p++) {
			unautomatedParamValues[p] = unpatchedParams->params[p].getCurrentValue();
		}
	}

	// Or if going un-automated
	else {

		// Restore the unautomated values, where automation is present
		for (int32_t p = 0; p < params::kMaxNumUnpatchedParams; p++) {
			if (unpatchedParams->params[p].isAutomated()) {
				unpatchedParams->params[p].currentValue = unautomatedParamValues[p];
			}
		}
	}

	view.notifyParamAutomationOccurred(&paramManager, true);
}

// returns true if the whole instrument should be replaced, and not just the instrument for the given clip
// returns false iff in clip view for a clip that does not have an instance in arranger. Or if called with no clip
// which could happen from the arranger audition pad availability will be unused in session or arranger view,
// available in session for active clips in clip view, and any for inactive clips
bool Song::shouldOldOutputBeReplaced(Clip* clip, Availability* availabilityRequirement) {
	// If Clip has an "instance" within its Output in arranger, then we can only change the entire Output to a
	// different Output If in session view, change the whole instrument
	if (!clip || clip->output->clipHasInstance(clip) || getRootUI() == &sessionView) {
		if (availabilityRequirement) {
			*availabilityRequirement = Availability::INSTRUMENT_UNUSED;
		}
		return true;
	}

	else {

		if (availabilityRequirement) {
			// If Clip is "active", just make sure we pick an Output that doesn't have a Clip "active" in session
			if (isClipActive(clip)) {
				*availabilityRequirement = Availability::INSTRUMENT_AVAILABLE_IN_SESSION;
			}

			// Or if it's not "active", we can give it any Output we like
			else {
				*availabilityRequirement = Availability::ANY;
			}
		}

		// We still may as well replace the Output so long as it doesn't have any *other* Clips
		return (getClipWithOutput(clip->output, false, clip) == NULL);
	}
}

Output* Song::navigateThroughPresetsForInstrument(Output* output, int32_t offset) {
	if (output->type == OutputType::AUDIO) {
		return output;
	}

	actionLogger.deleteAllLogs();

	Instrument* oldInstrument = (Instrument*)output;

	OutputType outputType = oldInstrument->type;

	currentSong->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("E063", "H063");

	// If we're in MIDI or CV mode, easy - just change the channel
	if (outputType == OutputType::MIDI_OUT || outputType == OutputType::CV) {

		NonAudioInstrument* oldNonAudioInstrument = (NonAudioInstrument*)oldInstrument;

		int32_t oldChannel = oldNonAudioInstrument->channel;
		int32_t newChannel = oldNonAudioInstrument->channel;

		int32_t oldChannelSuffix, newChannelSuffix;
		if (outputType == OutputType::MIDI_OUT) {
			oldChannelSuffix = ((MIDIInstrument*)oldNonAudioInstrument)->channelSuffix;
			newChannelSuffix = ((MIDIInstrument*)oldNonAudioInstrument)->channelSuffix;
		}

		// CV
		if (outputType == OutputType::CV) {
			do {
				newChannel = (newChannel + offset) & (NUM_CV_CHANNELS - 1);

				if (newChannel == oldChannel) {
cantDoIt:
					display->displayPopup(l10n::get(l10n::String::STRING_FOR_NO_FREE_CHANNEL_SLOTS_AVAILABLE_IN_SONG));
					return output;
				}

			} while (currentSong->getInstrumentFromPresetSlot(outputType, newChannel, -1, NULL, NULL, false));
		}

		// Or MIDI
		else {

			oldNonAudioInstrument->channel = -1; // Get it out of the way

			do {
				newChannelSuffix += offset;

				// Turned left
				if (offset == -1) {
					if (newChannelSuffix < -1) {
						newChannel = (newChannel + offset) & 15;
						newChannelSuffix = currentSong->getMaxMIDIChannelSuffix(newChannel);
					}
				}

				// Turned right
				else {
					if (newChannelSuffix >= 26 || newChannelSuffix > currentSong->getMaxMIDIChannelSuffix(newChannel)) {
						newChannel = (newChannel + offset) & 15;
						newChannelSuffix = -1;
					}
				}

				if (newChannel == oldChannel && newChannelSuffix == oldChannelSuffix) {
					oldNonAudioInstrument->channel = oldChannel; // Put it back
					goto cantDoIt;
				}

			} while (
			    currentSong->getInstrumentFromPresetSlot(outputType, newChannel, newChannelSuffix, NULL, NULL, false));

			oldNonAudioInstrument->channel = oldChannel; // Put it back, before switching notes off etc
		}

		if (oldNonAudioInstrument->getActiveClip() && playbackHandler.isEitherClockActive()) {
			oldNonAudioInstrument->getActiveClip()->expectNoFurtherTicks(currentSong);
		}

		// Because these are just MIDI / CV instruments and we're changing them for all Clips, we can just change
		// the existing Instrument object!
		oldNonAudioInstrument->channel = newChannel;
		if (outputType == OutputType::MIDI_OUT) {
			((MIDIInstrument*)oldNonAudioInstrument)->channelSuffix = newChannelSuffix;
		}

		view.displayOutputName(oldNonAudioInstrument);
	}

	// Or if we're on a Kit or Synth...
	else {
		PresetNavigationResult results =
		    loadInstrumentPresetUI.doPresetNavigation(offset, oldInstrument, Availability::INSTRUMENT_UNUSED, true);
		if (results.error == Error::NO_ERROR_BUT_GET_OUT) {
removeWorkingAnimationAndGetOut:
			if (display->haveOLED()) {
				auto oled = static_cast<deluge::hid::display::OLED*>(display);
				oled->consoleTimerEvent();
				oled->removeWorkingAnimation();
			}
			return output;
		}
		else if (results.error != Error::NONE) {
			display->displayError(results.error);
			goto removeWorkingAnimationAndGetOut;
		}

		Instrument* newInstrument = results.fileItem->instrument;
		Browser::emptyFileItems();

		currentSong->replaceInstrument(oldInstrument, newInstrument);

		oldInstrument = newInstrument;
		display->removeLoadingAnimation();
	}

	currentSong->instrumentSwapped(oldInstrument);

	currentSong->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("E064", "H064");

	return oldInstrument;
}

void Song::instrumentSwapped(Instrument* newInstrument) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	// If we're playing, in this arrangement mode... (TODO: what if it just switched on while we were loading?)
	if (arrangement.hasPlaybackActive()) {
		int32_t i = newInstrument->clipInstances.search(arrangement.getLivePos() + 1, LESS);

tryAgain:
		if (i >= 0) {

			ClipInstance* clipInstance = newInstrument->clipInstances.getElement(i);

			// If it didn't have an actual Clip, look further back in time
			if (!clipInstance->clip) {
				i--;
				goto tryAgain;
			}

			// Ok, we've got one with a Clip.

			// If it's still playing...
			if (clipInstance->pos + clipInstance->length > playbackHandler.getActualSwungTickCount()) {
				arrangement.resumeClipInstancePlayback(clipInstance); // Sets activeClip
			}

			// Otherwise, just set the activeClip anyway
			else {
				ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
				    modelStack->addTimelineCounter(clipInstance->clip);
				newInstrument->setActiveClip(modelStackWithTimelineCounter);
			}
		}
	}

	// Or if not, is there another Clip which is active, which needs sorting out with the newInstrument?
	else {
		Clip* thisClip = getClipWithOutput(newInstrument, true);
		if (thisClip) {

			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(thisClip);

			// Assert that thisClip is the active Clip with this Instrument - make any other Clips inactive
			// (activity status could have changed while we were loading...)
			assertActiveness(modelStackWithTimelineCounter);

			if (playbackHandler.isEitherClockActive()) {

				thisClip->setPosForParamManagers(modelStackWithTimelineCounter);
			}
		}
	}

	// If all else failed, just try to get any activeClip possible
	newInstrument->pickAnActiveClipIfPossible(modelStack);
}

Instrument* Song::changeOutputType(Instrument* oldInstrument, OutputType newOutputType) {

	int16_t newSlot = 0;
	int8_t newSubSlot = -1;

	int32_t oldSlot = newSlot;

	Instrument* newInstrument = NULL;

	// MIDI / CV
	if (newOutputType == OutputType::MIDI_OUT || newOutputType == OutputType::CV) {

		int32_t numChannels = (newOutputType == OutputType::MIDI_OUT) ? 16 : NUM_CV_CHANNELS;

		while (true) {

			if (!getInstrumentFromPresetSlot(newOutputType, newSlot, newSubSlot, NULL, NULL, false)) {
				break;
			}

			newSlot = (newSlot + 1) & (numChannels - 1);
			newSubSlot = -1;

			// If we've searched all channels...
			if (newSlot == oldSlot) {
				display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_AVAILABLE_CHANNELS));
				return NULL;
			}
		}

		if (newOutputType == OutputType::MIDI_OUT) {
			newInstrument = grabHibernatingMIDIInstrument(newSlot, newSubSlot);
			if (newInstrument) {
				goto gotAnInstrument;
			}
		}
		newInstrument = storageManager.createNewNonAudioInstrument(newOutputType, newSlot, newSubSlot);
		if (!newInstrument) {
			display->displayError(Error::INSUFFICIENT_RAM);
			return NULL;
		}

gotAnInstrument: {}
	}

	// Synth or Kit
	else {
		ReturnOfConfirmPresetOrNextUnlaunchedOne result;
		result.error = Browser::currentDir.set(getInstrumentFolder(newOutputType));
		if (result.error != Error::NONE) {
			display->displayError(result.error);
			return nullptr;
		}

		result = loadInstrumentPresetUI.findAnUnlaunchedPresetIncludingWithinSubfolders(
		    this, newOutputType, Availability::INSTRUMENT_UNUSED);
		if (result.error != Error::NONE) {
			display->displayError(result.error);
			return nullptr;
		}

		newInstrument = result.fileItem->instrument;
		bool isHibernating = newInstrument && !result.fileItem->instrumentAlreadyInSong;

		if (!newInstrument) {
			String newPresetName;
			result.fileItem->getDisplayNameWithoutExtension(&newPresetName);
			result.error = storageManager.loadInstrumentFromFile(this, NULL, newOutputType, false, &newInstrument,
			                                                     &result.fileItem->filePointer, &newPresetName,
			                                                     &Browser::currentDir);
		}

		Browser::emptyFileItems();

		if (result.error != Error::NONE) {
			display->displayError(result.error);
			return nullptr;
		}

		if (isHibernating) {
			removeInstrumentFromHibernationList(newInstrument);
		}

		display->displayLoadingAnimationText("Loading");

		newInstrument->loadAllAudioFiles(true);

		display->removeWorkingAnimation();
	}

#if ALPHA_OR_BETA_VERSION
	display->setText("A002");
#endif
	replaceInstrument(oldInstrument, newInstrument);
#if ALPHA_OR_BETA_VERSION
	if (display->have7SEG()) {
		view.displayOutputName(newInstrument);
	}
#endif

	instrumentSwapped(newInstrument);

	return newInstrument;
}

void Song::setupClipIndexesForSaving() {
	// For each Clip in session and arranger
	ClipArray* clipArray = &sessionClips;
	int32_t sessionIndex = 0;
	for (Clip* clip : AllClips::inSession(this)) {
		clip->indexForSaving = sessionIndex++;
	}
	int32_t arrangerOnlyIndex = 0;
	for (Clip* clip : AllClips::inArrangementOnly(this)) {
		clip->indexForSaving = arrangerOnlyIndex++;
	}
}

AudioOutput* Song::getFirstAudioOutput() {
	for (Output* output = firstOutput; output; output = output->next) {
		if (output->type == OutputType::AUDIO) {
			return (AudioOutput*)output;
		}
	}
	return NULL;
}

AudioInputChannel defaultAudioOutputInputChannel = AudioInputChannel::UNSET;

AudioOutput* Song::createNewAudioOutput(Output* replaceOutput) {
	int32_t highestNumber = 0;

	// Find highest number existent so far
	for (Output* output = firstOutput; output; output = output->next) {
		if (output->type == OutputType::AUDIO) {
			char const* nameChars = output->name.get();
			if (memcasecmp(nameChars, "AUDIO", 5)) {
				continue;
			}

			int32_t nameLength = strlen(nameChars);
			if (nameLength < 1) {
				continue;
			}

			if (!memIsNumericChars(&nameChars[5], nameLength - 5)) {
				continue;
			}

			int32_t number = stringToInt(&nameChars[5]);
			if (number > highestNumber) {
				highestNumber = number;
			}
		}
	}

	String newName;
	Error error = newName.set("AUDIO");
	if (error != Error::NONE) {
		return NULL;
	}

	error = newName.concatenateInt(highestNumber + 1);
	if (error != Error::NONE) {
		return NULL;
	}

	ParamManagerForTimeline newParamManager;
	error = newParamManager.setupUnpatched();
	if (error != Error::NONE) {
		return NULL;
	}

	void* outputMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(AudioOutput));
	if (!outputMemory) {
		return NULL;
	}

	AudioOutput* newOutput = new (outputMemory) AudioOutput();
	newOutput->name.set(&newName);

	// Set input channel to previously used one. If none selected, see what's in Song
	if (defaultAudioOutputInputChannel == AudioInputChannel::UNSET) {

		defaultAudioOutputInputChannel = AudioInputChannel::LEFT;
		for (Output* output = firstOutput; output; output = output->next) {
			if (output->type == OutputType::AUDIO) {
				defaultAudioOutputInputChannel = ((AudioOutput*)output)->inputChannel;
				break;
			}
		}
	}

	newOutput->inputChannel = defaultAudioOutputInputChannel;

	GlobalEffectableForClip::initParamsForAudioClip(&newParamManager);

	backUpParamManager((ModControllableAudio*)newOutput->toModControllable(), NULL, &newParamManager, true);

	if (replaceOutput) {
		replaceOutputLowLevel(newOutput, replaceOutput);
	}

	else {
		addOutput(newOutput);
	}
	return newOutput;
}

Output* Song::getNextAudioOutput(int32_t offset, Output* oldOutput, Availability availabilityRequirement) {

	Output* newOutput = oldOutput;

	// Forward
	if (offset < 0) { // Reverses direction
		while (true) {
			newOutput = newOutput->next;
			if (!newOutput) {
				newOutput = firstOutput;
			}
			if (newOutput == oldOutput) {
				break;
			}
			if (availabilityRequirement >= Availability::INSTRUMENT_AVAILABLE_IN_SESSION
			    && doesOutputHaveActiveClipInSession(newOutput)) {
				continue;
			}
			if (newOutput->type == OutputType::AUDIO) {
				break;
			}
		}
	}

	// Backward
	else {
		Output* investigatingOutput = oldOutput;
		while (true) {
			investigatingOutput = investigatingOutput->next;
			if (!investigatingOutput) {
				investigatingOutput = firstOutput;
			}
			if (investigatingOutput == oldOutput) {
				break;
			}
			if (availabilityRequirement >= Availability::INSTRUMENT_AVAILABLE_IN_SESSION
			    && doesOutputHaveActiveClipInSession(investigatingOutput)) {
				continue;
			}
			if (investigatingOutput->type == OutputType::AUDIO) {
				newOutput = investigatingOutput;
			}
		}
	}

	return newOutput;
}

// Unassign all Voices first
void Song::replaceOutputLowLevel(Output* newOutput, Output* oldOutput) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	oldOutput->stopAnyAuditioning(modelStack);

	Output** prevPointer;
	for (prevPointer = &firstOutput; *prevPointer != oldOutput; prevPointer = &(*prevPointer)->next) {}
	newOutput->next = oldOutput->next;
	*prevPointer = newOutput;

	// Migrate all ClipInstances from oldInstrument to newInstrument
	newOutput->clipInstances.swapStateWith(&oldOutput->clipInstances);

	newOutput->colour = oldOutput->colour;
	oldOutput->colour = 0;

	newOutput->mutedInArrangementMode = oldOutput->mutedInArrangementMode;
	oldOutput->mutedInArrangementMode = false;

	newOutput->soloingInArrangementMode = oldOutput->soloingInArrangementMode;
	oldOutput->soloingInArrangementMode = false;

	newOutput->armedForRecording = oldOutput->armedForRecording;
	oldOutput->armedForRecording = false;

	// Properly do away with the oldInstrument
	deleteOrAddToHibernationListOutput(oldOutput);

	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
}

void Song::getNoteLengthName(StringBuf& buffer, uint32_t noteLength, char const* const notesString,
                             bool clarifyPerColumn) const {
	getNoteLengthNameFromMagnitude(buffer, getNoteMagnitudeFfromNoteLength(noteLength, getInputTickMagnitude()),
	                               notesString, clarifyPerColumn);
}

Instrument* Song::getNonAudioInstrumentToSwitchTo(OutputType newOutputType, Availability availabilityRequirement,
                                                  int16_t newSlot, int8_t newSubSlot,
                                                  bool* instrumentWasAlreadyInSong) {
	int32_t numChannels = (newOutputType == OutputType::MIDI_OUT) ? 16 : NUM_CV_CHANNELS;
	Instrument* newInstrument;
	int32_t oldSlot = newSlot;

	while (true) {

		newInstrument = getInstrumentFromPresetSlot(newOutputType, newSlot, newSubSlot, NULL, NULL,
		                                            false); // This will always be false! Might rework this though

		if (availabilityRequirement == Availability::ANY) {
			break;
		}
		else if (availabilityRequirement == Availability::INSTRUMENT_AVAILABLE_IN_SESSION) {
			if (!newInstrument || !getClipWithOutput(newInstrument, true)) {
				break;
			}
		}
		else if (availabilityRequirement == Availability::INSTRUMENT_UNUSED) {
			if (!newInstrument) {
				break;
			}
		}

		newSlot = (newSlot + 1) & (numChannels - 1);
		newSubSlot = -1;

		// If we've searched all channels...
		if (newSlot == oldSlot) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_UNUSED_CHANNELS_AVAILABLE));
			return NULL;
		}
	}

	*instrumentWasAlreadyInSong = (newInstrument != NULL);

	// If that didn't work... make a new Instrument to switch to
	if (!newInstrument) {

		if (newOutputType == OutputType::MIDI_OUT) {
			newInstrument = grabHibernatingMIDIInstrument(newSlot, newSubSlot);
			if (newInstrument) {
				goto gotAnInstrument;
			}
		}
		newInstrument = storageManager.createNewNonAudioInstrument(newOutputType, newSlot, newSubSlot);
		if (!newInstrument) {
			display->displayError(Error::INSUFFICIENT_RAM);
			return NULL;
		}
	}

gotAnInstrument:
	return newInstrument;
}

void Song::removeSessionClip(Clip* clip, int32_t clipIndex, bool forceClipsAboveToMoveVertically) {

	// If this is the current Clip for the ClipView...
	if (currentClip == clip) {
		currentClip = NULL;
	}

	// Must unsolo the Clip before we delete it, in case its play-pos needs to be grabbed for another Clip
	if (clip->soloingInSessionMode) {
		session.unsoloClip(clip);
	}

	// See if any instances in arranger
	bool foundAtLeastOneInstanceInArranger = false;
	Output* output = clip->output;

	for (int32_t i = 0; i < output->clipInstances.getNumElements(); i++) {
		ClipInstance* clipInstance = output->clipInstances.getElement(i);
		if (clipInstance->clip == clip) {

			int32_t lengthGotUpTo = clipInstance->length;
			int32_t startPos = clipInstance->pos;

			bool deletedAnyElements = false;

lookAtNextOne:
			if (i + 1 < output->clipInstances.getNumElements() && (lengthGotUpTo % clip->loopLength) == 0) {

				// See if next ClipInstance has the same Clip and lines up as a repeat...
				ClipInstance* nextClipInstance =
				    output->clipInstances.getElement(i + 1); // We already checked that this exists
				if (nextClipInstance->clip == clip && startPos + lengthGotUpTo == nextClipInstance->pos) {

					lengthGotUpTo += nextClipInstance->length;

					// Delete that later ClipInstance
					arrangement.rowEdited(output, nextClipInstance->pos,
					                      nextClipInstance->pos + nextClipInstance->length, clip, NULL);
					output->clipInstances.deleteAtIndex(i + 1);
					deletedAnyElements = true;
					goto lookAtNextOne;
				}
			}

			if (deletedAnyElements) {
				clipInstance = output->clipInstances.getElement(i); // Gotta re-get, since storage has changed
			}

			// If we'd already found one, we'll have to create a clone for this one - and possibly extend it
			if (foundAtLeastOneInstanceInArranger) {
				arrangement.doUniqueCloneOnClipInstance(clipInstance, lengthGotUpTo);
			}

			// Otherwise, just extend its length if needed
			else {
				if (deletedAnyElements) {
					int32_t oldLength = clipInstance->length;
					clipInstance->length = lengthGotUpTo;
					arrangement.rowEdited(output, startPos + oldLength, startPos + lengthGotUpTo, NULL, clipInstance);
				}
			}

			foundAtLeastOneInstanceInArranger = true;
		}
	}

	int32_t clipYDisplay = clipIndex - songViewYScroll;
	int32_t bottomYDisplay = -songViewYScroll;
	int32_t topYDisplay = bottomYDisplay + sessionClips.getNumElements() - 1;
	bottomYDisplay = std::max(bottomYDisplay, 0_i32);
	topYDisplay = std::min(topYDisplay, kDisplayHeight - 1);
	int32_t amountOfStuffAbove = topYDisplay - clipYDisplay;
	int32_t amountOfStuffBelow = clipYDisplay - bottomYDisplay;

	removeSessionClipLowLevel(clip, clipIndex);

	// If there was at least one instance, don't properly delete the Clip - just put it in the arranger only. But do
	// stop it playing!
	if (foundAtLeastOneInstanceInArranger) {

		arrangementOnlyClips.insertClipAtIndex((InstrumentClip*)clip, 0);
		clip->section = 255;
	}

	// Otherwise, delete as usual
	else {
		deleteClipObject(clip, false, InstrumentRemoval::DELETE_OR_HIBERNATE_IF_UNUSED);
	}

	if (forceClipsAboveToMoveVertically || amountOfStuffAbove > amountOfStuffBelow) {
		songViewYScroll--;
	}

	AudioEngine::mustUpdateReverbParamsBeforeNextRender =
	    true; // Necessary? Maybe the Instrument would get deleted from the master list?
}

// Please stop the Clip from soloing before calling this
void Song::removeSessionClipLowLevel(Clip* clip, int32_t clipIndex) {

	if (playbackHandler.isEitherClockActive() && currentPlaybackMode == &session && clip->activeIfNoSolo) {
		clip->expectNoFurtherTicks(this);
		clip->activeIfNoSolo = false;
	}

	sessionClips.deleteAtIndex(clipIndex);
}

// originalClipIndex is optional
bool Song::deletePendingOverdubs(Output* onlyWithOutput, int32_t* originalClipIndex,
                                 bool createConsequencesForOtherLinearlyRecordingClips) {

	// You'd kind of think that we'd want to just not bother with this if playback isn't active, but we're not
	// allowed to apply that logic, cos this will get called as playback ends, but after playbackState is set to 0

	// But, we're still allowed to do this check
	if (playbackHandler.isEitherClockActive() && currentPlaybackMode != &session) {
		return false;
	}

	bool anyDeleted = false;

	// For each Clip in session
	for (int32_t c = sessionClips.getNumElements() - 1; c >= 0; c--) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		if (clip->isPendingOverdub && (!onlyWithOutput || clip->output == onlyWithOutput)) {
			removeSessionClip(clip, c, true);

			if (originalClipIndex && *originalClipIndex > c) {
				(*originalClipIndex)--;
			}

			anyDeleted = true;
		}
	}

	return anyDeleted;
}

int32_t Song::getYScrollSongViewWithoutPendingOverdubs() {
	int32_t numToSearch = std::min(sessionClips.getNumElements(), songViewYScroll + kDisplayHeight);

	int32_t outputValue = songViewYScroll;

	for (int32_t i = 0; i < numToSearch; i++) {
		Clip* clip = sessionClips.getClipAtIndex(i);
		if (clip->isPendingOverdub) {
			outputValue--;
		}
	}

	return outputValue;
}

Clip* Song::getPendingOverdubWithOutput(Output* output) {

	// For each Clip in session
	for (int32_t c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		if (clip->isPendingOverdub && clip->output == output) {
			return clip;
		}
	}

	return NULL;
}

Clip* Song::getClipWithOutputAboutToBeginLinearRecording(Output* output) {

	// For each Clip in session
	for (int32_t c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		if (clip->output == output && clip->armState != ArmState::OFF && !isClipActive(clip)
		    && clip->wantsToBeginLinearRecording(this)) {
			return clip;
		}
	}

	return NULL;
}

Clip* Song::createPendingNextOverdubBelowClip(Clip* clip, int32_t clipIndex, OverDubType newOverdubNature) {

	// No automatic overdubs are allowed during soloing, cos that's just too complicated
	if (anyClipsSoloing) {
		return NULL;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

	Clip* newClip = clip->cloneAsNewOverdub(modelStackWithTimelineCounter, newOverdubNature);

	if (newClip) {
		newClip->overdubNature = newOverdubNature;
		sessionClips.insertClipAtIndex(newClip, clipIndex);
		if (clipIndex != songViewYScroll) {
			songViewYScroll++;
		}

		// use root UI in case this is called from performance view
		sessionView.requestRendering(getRootUI());
	}

	return newClip;
}

bool Song::hasAnyPendingNextOverdubs() {

	// For each Clip in session
	for (int32_t c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		if (clip->isPendingOverdub) {
			return true;
		}
	}

	return false;
}

int32_t Song::countAudioClips() const {
	int32_t i = 0;
	for (Output* output = firstOutput; output; output = output->next) {
		if (output->type == OutputType::AUDIO) {
			if (output->getActiveClip()) {
				AudioClip* clip = (AudioClip*)output->getActiveClip();
				// this seems to be the only way to find whether the voice is sounding
				if (isClipActive(clip)) {
					i++;
				}
			}
		}
	}
	return i;
}

void Song::cullAudioClipVoice() {
	AudioClip* bestClip = NULL;
	uint64_t lowestImmunity = 0xFFFFFFFFFFFFFFFF;

	for (Output* output = firstOutput; output; output = output->next) {
		if (output->type == OutputType::AUDIO) {
			if (output->getActiveClip()) {
				AudioClip* clip = (AudioClip*)output->getActiveClip();
				if (clip->voiceSample && clip->voiceSample->oscPos > 0) {
					uint64_t immunity = clip->getCullImmunity();
					lowestImmunity = immunity;
					bestClip = clip;
				}
			}
		}
	}

	if (bestClip) {
		bestClip->unassignVoiceSample(false);
		D_PRINTLN("audio clip voice culled!");
	}
}

void Song::swapClips(Clip* newClip, Clip* oldClip, int32_t clipIndex) {
	sessionClips.setPointerAtIndex(newClip, clipIndex);

	if (oldClip == getSyncScalingClip()) {
		syncScalingClip = newClip;
	}

	if (oldClip == currentClip) {
		currentClip = newClip;
	}

	deleteClipObject(oldClip);
}

int8_t defaultAudioClipOverdubOutputCloning = -1; // -1 means no default set

Clip* Song::replaceInstrumentClipWithAudioClip(Clip* oldClip, int32_t clipIndex) {

	// Allocate memory for audio clip
	void* clipMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(AudioClip));
	if (!clipMemory) {
		return NULL;
	}

	// Suss output
	AudioOutput* newOutput = createNewAudioOutput();
	if (!newOutput) {
		delugeDealloc(clipMemory);
		return NULL;
	}

	newOutput->colour = oldClip->output->colour;

	// Create the audio clip and ParamManager
	AudioClip* newClip = new (clipMemory) AudioClip();

	// Give the new clip its stuff
	newClip->cloneFrom(oldClip);
	newClip->colourOffset = random(72);
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);
	newClip->setOutput(modelStack->addTimelineCounter(newClip), newOutput);

	if (defaultAudioClipOverdubOutputCloning == -1) {
		defaultAudioClipOverdubOutputCloning = 1;
		// For each Clip in session
		for (int32_t c = 0; c < sessionClips.getNumElements(); c++) {
			Clip* clip = sessionClips.getClipAtIndex(c);

			if (clip->type == ClipType::AUDIO && clip->armedForRecording) {
				defaultAudioClipOverdubOutputCloning = ((AudioClip*)clip)->overdubsShouldCloneOutput;
				break;
			}
		}
	}
	newClip->overdubsShouldCloneOutput = defaultAudioClipOverdubOutputCloning;

	// Might want to prevent new audio clip from being active if playback is on
	if (playbackHandler.playbackState && isClipActive(oldClip)) {
		newClip->activeIfNoSolo = false;

		// Must unsolo the Clip before we delete it, in case its play-pos needs to be grabbed for another Clip
		if (oldClip->soloingInSessionMode) {
			session.unsoloClip(oldClip);
		}
	}

	swapClips(newClip, oldClip, clipIndex);

	return newClip;
}

void Song::changeSwingInterval(int32_t newValue) {

	swingInterval = newValue;

	if (playbackHandler.isInternalClockActive()) {

		int32_t leftShift = 10 - swingInterval;
		leftShift = std::max(leftShift, 0_i32);
		uint32_t doubleSwingInterval = 3 << (leftShift);

		// Rejig the timer tick stuff
		uint64_t currentInternalTick = playbackHandler.getCurrentInternalTickCount();

		uint64_t startOfSwingWindow = currentInternalTick / doubleSwingInterval * doubleSwingInterval;

		if (startOfSwingWindow != playbackHandler.lastTimerTickActioned) {
			playbackHandler.timeLastTimerTickBig = (uint64_t)playbackHandler.getInternalTickTime(startOfSwingWindow)
			                                       << 32;

			playbackHandler.lastTimerTickActioned = startOfSwingWindow;
		}

		playbackHandler.scheduleNextTimerTick(doubleSwingInterval);

		// Reschedule all the other stuff
		playbackHandler.swungTickScheduled = false;
		playbackHandler.scheduleSwungTickFromInternalClock();

		if (playbackHandler.currentlySendingMIDIOutputClocks()) {
			playbackHandler.midiClockOutTickScheduled = false;
			playbackHandler.scheduleMIDIClockOutTick();
		}

		if (cvEngine.isTriggerClockOutputEnabled()) {
			playbackHandler.triggerClockOutTickScheduled = false;
			playbackHandler.scheduleTriggerClockOutTick();
		}
	}
}

uint32_t Song::getQuarterNoteLength() {
	return increaseMagnitude(24, getInputTickMagnitude());
}

uint32_t Song::getBarLength() {
	return increaseMagnitude(96, getInputTickMagnitude());
}

// ----- PlayPositionCounter implementation -------

bool Song::isPlayingAutomationNow() const {
	return (currentPlaybackMode == &arrangement || playbackHandler.recording == RecordingMode::ARRANGEMENT);
}

bool Song::backtrackingCouldLoopBackToEnd() const {
	return false;
}

int32_t Song::getPosAtWhichPlaybackWillCut(ModelStackWithTimelineCounter const* modelStack) const {
	return 2147483647;
}

void Song::getActiveModControllable(ModelStackWithTimelineCounter* modelStack) {
	if (affectEntire) {
		modelStack->setTimelineCounter(this);
		modelStack->addOtherTwoThingsButNoNoteRow(&globalEffectable, &paramManager);
	}
	else {
		modelStack->setTimelineCounter(NULL);
		modelStack->addOtherTwoThingsButNoNoteRow(NULL, NULL);
	}
}

void Song::expectEvent() {
	playbackHandler.expectEvent();
}

uint32_t Song::getLivePos() const {

	if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
		return playbackHandler.getActualArrangementRecordPos();
	}
	else {
		return arrangement.getLivePos();
	}
}

// I think I created this function to be called during the actioning of a swung tick, when we know that no further
// swung ticks have passed since the last actioned one
int32_t Song::getLastProcessedPos() const {

	if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
		return playbackHandler.getArrangementRecordPosAtLastActionedSwungTick();
	}
	else {
		return arrangement.lastProcessedPos;
	}
}

int32_t Song::getLoopLength() const {
	return 2147483647;
}

TimelineCounter* Song::getTimelineCounterToRecordTo() {
	return this;
}

void Song::setDefaultVelocityForAllInstruments(uint8_t newDefaultVelocity) {
	for (Output* output = firstOutput; output; output = output->next) {
		if (output->type != OutputType::AUDIO) {
			((Instrument*)output)->defaultVelocity = newDefaultVelocity;
		}
	}

	for (Instrument* instrument = firstHibernatingInstrument; instrument; instrument = (Instrument*)instrument->next) {
		instrument->defaultVelocity = newDefaultVelocity;
	}
}

int32_t Song::convertSyncLevelFromFileValueToInternalValue(int32_t fileValue) {

	// The file value is relative to insideWorldTickMagnitude etc, though if insideWorldTickMagnitude is 1 (which
	// used to be the default), it comes out as the same value anyway (kind of a side point)

	if (fileValue == 0) {
		return 0; // 0 means "off"
	}
	int32_t internalValue = fileValue + 1 - (getInputTickMagnitude());
	if (internalValue < 1) {
		internalValue = 1;
	}
	else if (internalValue > 9) {
		internalValue = 9;
	}

	return internalValue;
}

int32_t Song::convertSyncLevelFromInternalValueToFileValue(int32_t internalValue) {

	if (internalValue == 0) {
		return 0; // 0 means "off"
	}
	int32_t fileValue = internalValue - 1 + (getInputTickMagnitude());
	if (fileValue < 1) {
		fileValue = 1;
	}

	return fileValue;
}

String Song::getSongFullPath() {
	String fullPath;
	fullPath.concatenate(&dirPath);
	fullPath.concatenate("/");
	fullPath.concatenate(&name);
	fullPath.concatenate(".XML");
	return fullPath;
}

void Song::setSongFullPath(const char* fullPath) {
	if (char* filename = strrchr((char*)fullPath, '/')) {
		auto fullPathLength = strlen(fullPath);
		char* dir = new char[sizeof(char) * fullPathLength + 1];

		memset(dir, 0, sizeof(char) * fullPathLength + 1);
		strncpy(dir, fullPath, fullPathLength - strlen(filename));

		dirPath.set(dir);
		name.set(++filename);
	}
	else {
		name.set(fullPath);
	}
}

void Song::midiDeviceBendRangeUpdatedViaMessage(ModelStack* modelStack, MIDIDevice* device, int32_t channelOrZone,
                                                int32_t whichBendRange, int32_t bendSemitones) {

	// Go through all Instruments...
	for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {
		thisOutput->offerBendRangeUpdate(modelStack, device, channelOrZone, whichBendRange, bendSemitones);
	}
}

Error Song::addInstrumentsToFileItems(OutputType outputType) {
	bool doingHibernatingOnes;
	Output* thisOutput;
	if (false) {
doHibernatingInstruments:
		thisOutput = firstHibernatingInstrument;
		doingHibernatingOnes = true;
	}
	else {
		thisOutput = firstOutput;
		doingHibernatingOnes = false;
	}

	// First, check all other Instruments in memory, in case the next one in line isn't saved
	for (; thisOutput; thisOutput = thisOutput->next) {

		if (thisOutput->type != outputType) {
			continue;
		}

		Instrument* thisInstrument = (Instrument*)thisOutput;

		// If different path, it's not relevant.
		if (!thisInstrument->dirPath.equals(&Browser::currentDir)) {
			continue;
		}

		FileItem* thisItem = loadInstrumentPresetUI.getNewFileItem();

		if (!thisItem) {
			return Error::INSUFFICIENT_RAM;
		}

		Error error = thisItem->setupWithInstrument(thisInstrument, doingHibernatingOnes);

		if (error != Error::NONE) {
			return error;
		}
	}

	if (!doingHibernatingOnes) {
		goto doHibernatingInstruments;
	}

	return Error::NONE;
}

void Song::displayCurrentRootNoteAndScaleName() {
	DEF_STACK_STRING_BUF(popupMsg, 40);
	char noteName[5];
	int32_t isNatural = 1; // gets modified inside noteCodeToString to be 0 if sharp.
	noteCodeToString(currentSong->key.rootNote, noteName, &isNatural);

	popupMsg.append(noteName);
	if (display->haveOLED()) {
		popupMsg.append(" ");
		popupMsg.append(getScaleName(getCurrentPresetScale()));
	}
	display->displayPopup(popupMsg.c_str());
}

void Song::transpose(int32_t interval) {
	if (anyScaleModeClips()) {
		if (masterTransposeInterval != 0) {
			interval *= currentSong->masterTransposeInterval;
		}
		transposeAllScaleModeClips(interval);
		displayCurrentRootNoteAndScaleName();
	}
	else {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_TRANSPOSE));
	}
}

void Song::adjustMasterTransposeInterval(int32_t interval) {
	masterTransposeInterval += interval;
	if (masterTransposeInterval < 0) {
		masterTransposeInterval = 0;
	}
	displayMasterTransposeInterval();
}

void Song::displayMasterTransposeInterval() {
	DEF_STACK_STRING_BUF(popupMsg, 40);

	if (display->haveOLED()) {
		popupMsg.append("Transpose Interval: \n");
		if (masterTransposeInterval == 0) {
			popupMsg.append("Encoder");
		}
		else {
			popupMsg.appendInt(masterTransposeInterval);
			popupMsg.append(" Semitones");
		}
	}
	else {
		if (masterTransposeInterval == 0) {
			popupMsg.append("ENC");
		}
		else {
			popupMsg.appendInt(masterTransposeInterval);
		}
	}
	display->displayPopup(popupMsg.c_str());
}

ModelStackWithThreeMainThings* Song::setupModelStackWithSongAsTimelineCounter(void* memory) {
	return setupModelStackWithThreeMainThingsButNoNoteRow(memory, this, &globalEffectable, this, &paramManager);
}

ModelStackWithTimelineCounter* Song::setupModelStackWithCurrentClip(void* memory) {
	return setupModelStackWithTimelineCounter(memory, this, currentClip);
}

ModelStackWithThreeMainThings* Song::addToModelStack(ModelStack* modelStack) {
	return modelStack->addTimelineCounter(this)->addOtherTwoThingsButNoNoteRow(&globalEffectable, &paramManager);
}

ModelStackWithAutoParam* Song::getModelStackWithParam(ModelStackWithThreeMainThings* modelStack, int32_t paramID) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (modelStack) {
		modelStackWithParam = modelStack->getUnpatchedAutoParamFromId(paramID);
	}

	return modelStackWithParam;
}

/*
    // For each Clip in session and arranger
    ClipArray* clipArray = &sessionClips;
traverseClips:
    for (int32_t c = 0; c < clipArray->getNumElements(); c++) {
        Clip* clip = clipArray->getClipAtIndex(c);

    }
    if (clipArray != &arrangementOnlyClips) { clipArray = &arrangementOnlyClips; goto traverseClips; }



    // For each InstrumentClip in session and arranger
    ClipArray* clipArray = &sessionClips;
traverseClips:
    for (int32_t c = 0; c < clipArray->getNumElements(); c++) {
        Clip* clip = clipArray->getClipAtIndex(c);
        if (clip->type != ClipType::INSTRUMENT) continue;
        Clip* instrumentClip = (Clip*)clip;

    }
    if (clipArray != &arrangementOnlyClips) { clipArray = &arrangementOnlyClips; goto traverseClips; }



    // For each Clip in session
    for (int32_t c = 0; c < sessionClips.getNumElements(); c++) {
        Clip* clip = sessionClips.getClipAtIndex(c);

    }


    // For each Clip in arrangement
    for (int32_t c = 0; c < arrangementOnlyClips.getNumElements(); c++) {
        Clip* clip = arrangementOnlyClips.getClipAtIndex(c);

    }



    // For each Clip in session and arranger for specific Output
    int32_t numElements = sessionClips.getNumElements();
    bool doingArrangementClips = false;
traverseClips:
    for (int32_t c = 0; c < numElements; c++) {
        Clip* clip;
        if (!doingArrangementClips) {
            clip = sessionClips.getClipAtIndex(c);
            if (clip->output != output) continue;
        }
        else {
            ClipInstance* clipInstance = output->clipInstances.getElement(c);
            if (!clipInstance->clip) continue;
            if (!clipInstance->clip->isArrangementOnlyClip()) continue;
            clip = clipInstance->clip;
        }

    }
    if (!doingArrangementClips) { doingArrangementClips = true; numElements =
output->clipInstances.getNumElements(); goto traverseClips; }





    // For each Clip in session and arranger for specific Output - but if currentlySwappingInstrument, use master
list for arranger Clips ClipArray* clipArray = &sessionClips; bool doingClipsProvidedByOutput = false;
decideNumElements: int32_t numElements = clipArray->getNumElements(); traverseClips: for (int32_t c = 0; c <
numElements; c++) { Clip* clip; if (!doingClipsProvidedByOutput) { clip = clipArray->getClipAtIndex(c); if
(clip->output != output) continue;
        }
        else {
            ClipInstance* clipInstance = output->clipInstances.getElement(c);
            if (!clipInstance->clip) continue;
            if (!clipInstance->clip->isArrangementOnlyClip()) continue;
            clip = clipInstance->clip;
        }

    }
    if (!doingClipsProvidedByOutput && clipArray == &sessionClips) {
        if (currentlySwappingInstrument) { clipArray = &arrangementOnlyClips; goto decideNumElements; }
        else { doingClipsProvidedByOutput = true; numElements = output->clipInstances.getNumElements(); goto
traverseClips; }
    }
 */

//    for (Output* output = firstOutput; output; output = output->next) {
