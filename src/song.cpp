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

#include <ArrangerView.h>
#include <AudioEngine.h>
#include <AudioFileManager.h>
#include <ClipInstance.h>
#include <ConsequenceClipExistence.h>
#include <InstrumentClip.h>
#include <InstrumentClipMinder.h>
#include <ParamManager.h>
#include <sounddrum.h>
#include <soundinstrument.h>
#include "song.h"
#include "functions.h"
#include "View.h"
#include "storagemanager.h"
#include "matrixdriver.h"
#include "numericdriver.h"
#include "NoteRow.h"
#include "Action.h"
#include "ActionLogger.h"
//#include <algorithm>
#include <string.h>
#include <SessionView.h>
#include "playbackhandler.h"
#include "uart.h"
#include "midiengine.h"
#include "GeneralMemoryAllocator.h"
#include "Session.h"
#include "kit.h"
#include "CVInstrument.h"
#include "MIDIInstrument.h"
#include <new>
#include "Arrangement.h"
#include "AudioClip.h"
#include "AudioOutput.h"
#include "AudioClipView.h"
#include "SampleRecorder.h"
#include "CVEngine.h"
#include "PadLEDs.h"
#include "FlashStorage.h"
#include "revmodel.hpp"
#include "ModelStack.h"
#include "MIDIDevice.h"
#include "MIDIDeviceManager.h"
#include "ParamSet.h"
#include "PatchCableSet.h"
#include "Browser.h"
#include "FileItem.h"
#include "oled.h"

extern "C" {
#include "sio_char.h"
}

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

	xZoom[NAVIGATION_CLIP] = increaseMagnitude(DEFAULT_CLIP_LENGTH, insideWorldTickMagnitude - displayWidthMagnitude);
	xZoom[NAVIGATION_ARRANGEMENT] = DEFAULT_ARRANGER_ZOOM << insideWorldTickMagnitude;
	xZoomForReturnToSongView = xZoom[NAVIGATION_CLIP];

	tripletsOn = false;

	affectEntire = false;

	modeNotes[0] = 0;
	modeNotes[1] = 2;
	modeNotes[2] = 4;
	modeNotes[3] = 5;
	modeNotes[4] = 7;
	modeNotes[5] = 9;
	modeNotes[6] = 11;
	numModeNotes = 7;
	rootNote = 0;

	swingAmount = 0;

	swingInterval = 8 - insideWorldTickMagnitude; // 16th notes

	songViewYScroll = 1 - displayHeight;
	arrangementYScroll = -displayHeight;

	anyClipsSoloing = false;
	anyOutputsSoloingInArrangement = false;

	firstOutput = NULL;
	firstHibernatingInstrument = NULL;
	hibernatingMIDIInstrument = NULL;

	lastClipInstanceEnteredStartPos = -1;
	arrangerAutoScrollModeActive = false;

	paramsInAutomationMode = false;

	// Setup reverb temp variables
	reverbRoomSize = (float)30 / 50;
	reverbDamp = (float)36 / 50;
	reverbWidth = 1;
	reverbPan = 0;
	reverbCompressorVolume = getParamFromUserValue(PARAM_STATIC_COMPRESSOR_VOLUME, -1);
	reverbCompressorShape = -601295438;
	reverbCompressorSync = SYNC_LEVEL_8TH;

	dirPath.set("SONGS");
}

Song::~Song() {

	// Delete existing Clips, if any
	for (int c = 0; c < sessionClips.getNumElements(); c++) {
		if (!(c & 31)) {                              // Exact number here not fine-tuned
			AudioEngine::routineWithClusterLoading(); // -----------------------------------
		}

		Clip* clip = sessionClips.getClipAtIndex(c);
		deleteClipObject(clip, true, INSTRUMENT_REMOVAL_NONE);
	}

	for (int c = 0; c < arrangementOnlyClips.getNumElements(); c++) {
		if (!(c & 31)) {                              // Exact number here not fine-tuned
			AudioEngine::routineWithClusterLoading(); // -----------------------------------
		}

		Clip* clip = arrangementOnlyClips.getClipAtIndex(c);
		deleteClipObject(clip, true, INSTRUMENT_REMOVAL_NONE);
	}

	AudioEngine::logAction("s4");
	AudioEngine::routineWithClusterLoading(); // -----------------------------------

	// Free all ParamManagers which are backed up. The actual vector memory containing all the BackedUpParamManager objects will be freed by the Vector destructor
	deleteAllBackedUpParamManagers(false); // Don't empty vector - its destructor will do that

	deleteAllOutputs(&firstOutput);
	deleteAllOutputs((Output**)&firstHibernatingInstrument);

	deleteHibernatingMIDIInstrument();
}

#include "MenuItemIntegerRange.h"
#include "MenuItemKeyRange.h"
extern MenuItemIntegerRange defaultTempoMenu;
extern MenuItemIntegerRange defaultSwingMenu;
extern MenuItemKeyRange defaultKeyMenu;

void Song::setupDefault() {
	inClipMinderViewOnLoad = true;

	seedRandom();

	setBPM(defaultTempoMenu.getRandomValueInRange(), false);
	swingAmount = defaultSwingMenu.getRandomValueInRange() - 50;
	rootNote = defaultKeyMenu.getRandomValueInRange();

	// Do scale
	int whichScale = FlashStorage::defaultScale;
	if (whichScale == PRESET_SCALE_NONE) {
		whichScale = 0; // Major. Still need the *song*, (as opposed to the Clip) to have a scale
	}
	else if (whichScale == PRESET_SCALE_RANDOM) {
		whichScale = random(NUM_PRESET_SCALES - 1);
	}
	memcpy(modeNotes, presetScaleNotes[whichScale], sizeof(presetScaleNotes[whichScale]));
}

void Song::deleteAllOutputs(Output** prevPointer) {

	while (*prevPointer) {
		AudioEngine::logAction("s6");
		AudioEngine::routineWithClusterLoading(); // -----------------------------------
		Output* toDelete = *prevPointer;
		*prevPointer = toDelete->next;

		void* toDealloc = dynamic_cast<void*>(toDelete);
		toDelete->~Output();
		generalMemoryAllocator.dealloc(toDealloc);
	}
}

void Song::deleteAllBackedUpParamManagers(bool shouldAlsoEmptyVector) {
	for (int i = 0; i < backedUpParamManagers.getNumElements(); i++) {
		AudioEngine::routineWithClusterLoading(); // -----------------------------------
		BackedUpParamManager* backedUp = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(i);

		backedUp->~BackedUpParamManager();
	}
	if (shouldAlsoEmptyVector) backedUpParamManagers.empty();
}

void Song::deleteAllBackedUpParamManagersWithClips() {

	// We'll aim to repeatedly find the longest runs possible of ones with Clips, to delete all in one go

	for (int i = 0; i < backedUpParamManagers.getNumElements(); i++) {
		BackedUpParamManager* firstBackedUp = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(i);

		// If no Clip, just go onto the next
		if (!firstBackedUp->clip) continue;

		ModControllableAudio* modControllable = firstBackedUp->modControllable;
		int searchedUpToAndIncluding = i;

keepSearchingForward:

		// If still here, this is the first one with a Clip for this ModControllable. Find the end of this ModControllable's ones
		int endIThisModControllable = backedUpParamManagers.search(
		    (uint32_t)modControllable + 4, GREATER_OR_EQUAL, searchedUpToAndIncluding + 1); // Search just by first word

		// But if that next one, for the next ModControllable, also has a Clip, we can just keep looking forwards til we find one with no Clip
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
		for (int j = i; j < endIThisModControllable; j++) {
			BackedUpParamManager* backedUp = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(j);

			AudioEngine::routineWithClusterLoading(); // -----------------------------------

			backedUp->~BackedUpParamManager();
		}

		int numToDelete = endIThisModControllable - i;

		backedUpParamManagers.deleteAtIndex(i, numToDelete);

		// i will increment, which is fine, cos we've already determined that the next element (if there is one) has no Clip, so we can skip it
	}
}

bool Song::mayDoubleTempo() {
	return ((timePerTimerTickBig >> 33) > minTimePerTimerTick);
}

// Returns true if a Clip created
bool Song::ensureAtLeastOneSessionClip() {
	// If no Clips added, make just one blank one - we can't have none!
	if (!sessionClips.getNumElements()) {

		void* memory = generalMemoryAllocator.alloc(sizeof(InstrumentClip), NULL, false, true);
		InstrumentClip* firstClip = new (memory) InstrumentClip(this);

		sessionClips.insertClipAtIndex(firstClip, 0);

		firstClip->loopLength = DEFAULT_CLIP_LENGTH << insideWorldTickMagnitude;

		ParamManager newParamManager; // Deliberate don't set up.

		ReturnOfConfirmPresetOrNextUnlaunchedOne result;
		result.error = storageManager.initSD(); // Still want this here?
		if (result.error) goto couldntLoad;

		result.error = Browser::currentDir.set("SYNTHS");
		if (result.error) goto couldntLoad;

		result =
		    Browser::findAnUnlaunchedPresetIncludingWithinSubfolders(NULL, INSTRUMENT_TYPE_SYNTH, AVAILABILITY_ANY);

		Instrument* newInstrument;

		if (!result.error) {
			String newPresetName;
			result.fileItem->getDisplayNameWithoutExtension(&newPresetName);
			result.error = storageManager.loadInstrumentFromFile(this, firstClip, INSTRUMENT_TYPE_SYNTH, false,
			                                                     &newInstrument, &result.fileItem->filePointer,
			                                                     &newPresetName, &Browser::currentDir);

			Browser::emptyFileItems();
			if (result.error) goto couldntLoad;
		}
		else {
couldntLoad:
			newInstrument = storageManager.createNewInstrument(INSTRUMENT_TYPE_SYNTH, &newParamManager);

			// If that failed (really unlikely) we're really screwed
			if (!newInstrument) {
				result.error = ERROR_INSUFFICIENT_RAM;
reallyScrewed:
				numericDriver.displayError(result.error);
				while (1) {}
			}

			int error2 =
			    newInstrument->dirPath.set("SYNTHS"); // Use new error2 variable to preserve error result from before.
			if (error2) {
gotError2:
				result.error = error2;
				goto reallyScrewed;
			}

			error2 = newInstrument->name.set("0");
			if (error2) goto gotError2;

			((SoundInstrument*)newInstrument)->setupAsDefaultSynth(&newParamManager);
			numericDriver.displayError(result.error); // E.g. show the CARD error.
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

void Song::transposeAllScaleModeClips(int offset) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);
		if (clip->type != CLIP_TYPE_INSTRUMENT) continue;
		InstrumentClip* instrumentClip = (InstrumentClip*)clip;

		if (instrumentClip->isScaleModeClip()) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
			    modelStack->addTimelineCounter(instrumentClip);
			instrumentClip->transpose(offset, modelStackWithTimelineCounter);
		}
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips;
	}

	rootNote += offset;
}

bool Song::anyScaleModeClips() {

	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);
		if (clip->type != CLIP_TYPE_INSTRUMENT) continue;
		InstrumentClip* instrumentClip = (InstrumentClip*)clip;

		if (instrumentClip->isScaleModeClip()) return true;
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips;
	}

	return false;
}

void Song::setRootNote(int newRootNote, InstrumentClip* clipToAvoidAdjustingScrollFor) {

	int oldRootNote = rootNote;
	rootNote = newRootNote;
	int oldNumModeNotes = numModeNotes;
	bool notesWithinOctavePresent[12];
	for (int i = 0; i < 12; i++)
		notesWithinOctavePresent[i] = false;

	// All InstrumentClips in session and arranger
	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);
		if (clip->type != CLIP_TYPE_INSTRUMENT) continue;
		InstrumentClip* instrumentClip = (InstrumentClip*)clip;

		if (instrumentClip->isScaleModeClip())
			instrumentClip->seeWhatNotesWithinOctaveArePresent(notesWithinOctavePresent, rootNote, this);
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips;
	}

	// Determine the majorness or minorness of the scale
	int majorness = 0;

	// The 3rd is the main indicator of majorness, to my ear
	if (notesWithinOctavePresent[4]) majorness++;
	if (notesWithinOctavePresent[3]) majorness--;

	// If it's still a tie, try the 2nd, 6th, and 7th to help us decide
	if (majorness == 0) {
		if (notesWithinOctavePresent[1]) majorness--;
		if (notesWithinOctavePresent[8]) majorness--;
		if (notesWithinOctavePresent[9]) majorness++;
	}

	bool moreMajor = (majorness >= 0);

	modeNotes[0] = 0;
	numModeNotes = 1;

	// 2nd
	addMajorDependentModeNotes(1, true, notesWithinOctavePresent);

	// 3rd
	addMajorDependentModeNotes(3, moreMajor, notesWithinOctavePresent);

	// 4th, 5th
	if (notesWithinOctavePresent[5]) {
		addModeNote(5);
		if (notesWithinOctavePresent[6]) {
			addModeNote(6);
			if (notesWithinOctavePresent[7]) addModeNote(7);
		}
		else addModeNote(7);
	}
	else {
		if (notesWithinOctavePresent[6]) {
			if (notesWithinOctavePresent[7] || moreMajor) {
				addModeNote(6);
				addModeNote(7);
			}
			else {
				addModeNote(5);
				addModeNote(6);
			}
		}
		else {
			addModeNote(5);
			addModeNote(7);
		}
	}

	// 6th
	addMajorDependentModeNotes(8, moreMajor, notesWithinOctavePresent);

	// 7th
	addMajorDependentModeNotes(10, moreMajor, notesWithinOctavePresent);

	// Adjust scroll for Clips with the scale. Crudely - not as high quality as happens for the Clip being processed in enterScaleMode();
	int numMoreNotes = (int)numModeNotes - oldNumModeNotes;

	// Compensation for the change in root note itself
	int rootNoteChange = rootNote - oldRootNote;
	int rootNoteChangeEffect = rootNoteChange * (12 - numModeNotes)
	                           / 12; // I wasn't quite sure whether this should use numModeNotes or oldNumModeNotes

	// All InstrumentClips in session and arranger
	clipArray = &sessionClips;
traverseClips2:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);
		if (clip->type != CLIP_TYPE_INSTRUMENT) continue;
		InstrumentClip* instrumentClip = (InstrumentClip*)clip;

		if (instrumentClip != clipToAvoidAdjustingScrollFor && instrumentClip->isScaleModeClip()) {

			// Compensation for the change in number of mode notes
			int oldScrollRelativeToRootNote = instrumentClip->yScroll - oldRootNote;
			int numOctaves = oldScrollRelativeToRootNote / oldNumModeNotes;

			instrumentClip->yScroll += numMoreNotes * numOctaves + rootNoteChangeEffect;
		}
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips2;
	}
}

void Song::addModeNote(uint8_t modeNote) {
	modeNotes[numModeNotes] = modeNote;
	numModeNotes++;
}

// Sets up a mode-note, optionally specifying that we prefer it a semitone higher, although this may be overridden by what actual note is present
void Song::addMajorDependentModeNotes(uint8_t i, bool preferHigher, bool notesWithinOctavePresent[]) {
	// If lower one present...
	if (notesWithinOctavePresent[i]) {
		// If higher one present as well...
		if (notesWithinOctavePresent[i + 1]) {
			addModeNote(i);
			addModeNote(i + 1);
		}
		// Or if just the lower one
		else addModeNote(i);
	}
	// Or, if lower one absent...
	else {
		// We probably want the higher one
		if (notesWithinOctavePresent[i + 1] || preferHigher) addModeNote(i + 1);
		// Or if neither present and we prefer the lower one, do that
		else addModeNote(i);
	}
}

bool Song::yNoteIsYVisualWithinOctave(int yNote, int yVisualWithinOctave) {
	int yNoteWithinOctave = getYNoteWithinOctaveFromYNote(yNote);
	return (modeNotes[yVisualWithinOctave] == yNoteWithinOctave);
}

uint8_t Song::getYNoteWithinOctaveFromYNote(int yNote) {
	uint16_t yNoteRelativeToRoot = yNote - rootNote + 120;
	int yNoteWithinOctave = yNoteRelativeToRoot % 12;
	return yNoteWithinOctave;
}

bool Song::modeContainsYNote(int yNote) {
	int yNoteWithinOctave = (uint16_t)(yNote - rootNote + 120) % 12;
	return modeContainsYNoteWithinOctave(yNoteWithinOctave);
}

bool Song::modeContainsYNoteWithinOctave(uint8_t yNoteWithinOctave) {
	for (int i = 0; i < numModeNotes; i++) {
		if (modeNotes[i] == yNoteWithinOctave) return true;
	}
	return false;
}

// Flattens or sharpens a given note-within-octave in the current scale
void Song::changeMusicalMode(uint8_t yVisualWithinOctave, int8_t change) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	// All InstrumentClips in session and arranger
	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);
		if (clip->type != CLIP_TYPE_INSTRUMENT) continue;
		InstrumentClip* instrumentClip = (InstrumentClip*)clip;

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(instrumentClip);

		instrumentClip->musicalModeChanged(yVisualWithinOctave, change, modelStackWithTimelineCounter);
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips;
	}

	// If we were shifting the root note, then we actually want to shift all the other scale-notes
	if (yVisualWithinOctave == 0) {
		for (int i = 1; i < numModeNotes; i++) {
			modeNotes[i] -= change;
		}
		rootNote += change;
	}

	// Or if just shifting a non-root note, just shift its scale-note
	else modeNotes[yVisualWithinOctave] += change;
}

bool Song::isYNoteAllowed(int yNote, bool inKeyMode) {
	if (!inKeyMode) return true;
	return modeContainsYNoteWithinOctave(getYNoteWithinOctaveFromYNote(yNote));
}

int Song::getYVisualFromYNote(int yNote, bool inKeyMode) {
	if (!inKeyMode) return yNote;
	int yNoteRelativeToRoot = yNote - rootNote;
	int yNoteWithinOctave = (uint16_t)(yNoteRelativeToRoot + 120) % 12;

	int octave = (uint16_t)(yNoteRelativeToRoot + 120 - yNoteWithinOctave) / 12 - 10;

	int yVisualWithinOctave = 0;
	for (int i = 0; i < numModeNotes && modeNotes[i] <= yNoteWithinOctave; i++)
		yVisualWithinOctave = i;
	return yVisualWithinOctave + octave * numModeNotes + rootNote;
}

int Song::getYNoteFromYVisual(int yVisual, bool inKeyMode) {
	if (!inKeyMode) return yVisual;
	int yVisualRelativeToRoot = yVisual - rootNote;
	int yVisualWithinOctave = yVisualRelativeToRoot % numModeNotes;
	if (yVisualWithinOctave < 0) yVisualWithinOctave += numModeNotes;

	int octave = (yVisualRelativeToRoot - yVisualWithinOctave) / numModeNotes;

	int yNoteWithinOctave = modeNotes[yVisualWithinOctave];
	return yNoteWithinOctave + octave * 12 + rootNote;
}

bool Song::mayMoveModeNote(int16_t yVisualWithinOctave, int8_t newOffset) {
	// If it's the root note and moving down, special criteria
	if (yVisualWithinOctave == 0 && newOffset == -1) {
		return (modeNotes[numModeNotes - 1]
		        < 11); // May ony move down if the top note in scale isn't directly below (at semitone 11)
	}

	else
		return ((newOffset == 1 &&                      // We're moving up and
		         modeNotes[yVisualWithinOctave] < 11 && // We're not already at the top of the scale and
		         (yVisualWithinOctave == numModeNotes - 1
		          || // Either we're the top note, so don't need to check for a higher neighbour
		          modeNotes[yVisualWithinOctave + 1]
		              > modeNotes[yVisualWithinOctave] + 1) // Or the next note up has left us space to move up
		         )
		        || (newOffset == -1 && // We're moving down and
		            modeNotes[yVisualWithinOctave] > 1
		            && // We're not already at the bottom of the scale (apart from the root note) and
		            modeNotes[yVisualWithinOctave - 1]
		                < modeNotes[yVisualWithinOctave] - 1 // The next note down has left us space to move down
		            ));
}

void Song::removeYNoteFromMode(int yNoteWithinOctave) {

	// Why did I get rid of this again?

	/*
	int i = 0;
	while (i < numModeNotes) {
		if (modeNotes[i] == yNoteWithinOctave) goto foundIt;
		i++;
	}

	return;

	foundIt:

	numModeNotes--;
	while (i < numModeNotes) {
		modeNotes[i] = modeNotes[i + 1];
		i++;
	}

	*/

	// For each InstrumentClip in session and arranger
	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);
		if (clip->type != CLIP_TYPE_INSTRUMENT) continue;
		InstrumentClip* instrumentClip = (InstrumentClip*)clip;

		instrumentClip->noteRemovedFromMode(yNoteWithinOctave, this);
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips;
	}
}

bool Song::areAllClipsInSectionPlaying(int section) {
	if (getAnyClipsSoloing()) return false;

	for (int l = 0; l < sessionClips.getNumElements(); l++) {
		Clip* clip = sessionClips.getClipAtIndex(l);
		if (clip->section == section && !isClipActive(clip)) return false;
	}

	return true;
}

uint32_t Song::getInputTickScale() {
	if (!syncScalingClip) return 3;
	uint32_t inputTickScale = syncScalingClip->loopLength;
	while (!(inputTickScale & 1))
		inputTickScale >>= 1;
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

	// Chances are we'll have to change the input tick magnitude to account for the magnitudinal difference between say a 1-based and 9-based time
	if ((float)newScale * 1.41 < oldScale) {
		do {
			newScale *= 2;
			insideWorldTickMagnitude++;
			//insideWorldTickMagnitudeOffsetFromBPM--;
		} while ((float)newScale * 1.41 < oldScale);
	}

	else
		while ((float)oldScale * 1.41 < newScale) {
			oldScale *= 2;
			insideWorldTickMagnitude--;
			//insideWorldTickMagnitudeOffsetFromBPM++;
		}

	// We then do a very similar process again, to calculate insideWorldTickMagnitudeOffsetFromBPM in such a way that, say, 8th-notes will always appear about the same
	// length to the user

	oldScale = 3;
	newScale = getInputTickScale();
	insideWorldTickMagnitudeOffsetFromBPM = 0;

	if ((float)newScale * 1.41 < oldScale) {
		do {
			newScale *= 2;
			//insideWorldTickMagnitude++;
			insideWorldTickMagnitudeOffsetFromBPM--;
		} while ((float)newScale * 1.41 < oldScale);
	}

	else
		while ((float)oldScale * 1.41 < newScale) {
			oldScale *= 2;
			//insideWorldTickMagnitude--;
			insideWorldTickMagnitudeOffsetFromBPM++;
		}
}

// This is a little bit of an ugly hack - normally this is true, but we'll briefly set it to false while doing that "revert" that we do when (re)lengthening a Clip
bool allowResyncingDuringClipLengthChange = true;

// If action is NULL, that means this is being called as part of an undo, so don't do any extra stuff.
// Currently mayReSyncClip is only set to false in a call that happens when we've just finished recording that Clip.
void Song::setClipLength(Clip* clip, uint32_t newLength, Action* action, bool mayReSyncClip) {
	uint32_t oldLength = clip->loopLength;

	if (clip == syncScalingClip) {
		uint32_t oldScale = getInputTickScale();
		clip->loopLength = newLength;
		inputTickScalePotentiallyJustChanged(oldScale);
	}
	else clip->loopLength = newLength;

	if (action)
		action->recordClipLengthChange(clip, oldLength); // Records just the simple fact that clip->length has changed

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

	clip->increaseLengthWithRepeats(modelStack, oldLength << 1, INDEPENDENT_NOTEROW_LENGTH_INCREASE_DOUBLE, false,
	                                action);

	if (clip == syncScalingClip) inputTickScalePotentiallyJustChanged(oldScale);

	clip->output->clipLengthChanged(clip, oldLength);

	if (playbackHandler.isEitherClockActive() && isClipActive(clip)) {
		currentPlaybackMode->reSyncClip(modelStack);
	}
}

Clip* Song::getClipWithOutput(Output* output, bool mustBeActive, Clip* excludeClip) {

	// For each clip in session and arranger for specific Output
	int numElements = sessionClips.getNumElements();
	bool doingArrangementClips = false;
traverseClips:
	for (int c = 0; c < numElements; c++) {
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

		if (clip == excludeClip) continue;

		if (mustBeActive && !isClipActive(clip)) continue;
		return clip;
	}
	if (!doingArrangementClips) {
		doingArrangementClips = true;
		numElements = output->clipInstances.getNumElements();
		goto traverseClips;
	}

	return NULL;
}

Clip* Song::getSessionClipWithOutput(Output* output, int requireSection, Clip* excludeClip, int* clipIndex,
                                     bool excludePendingOverdubs) {

	// For each clip in session
	for (int c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		if (clip->output == output) {
			if (clip == excludeClip) continue;
			if (requireSection != -1 && clip->section != requireSection) continue;
			if (excludePendingOverdubs && clip->isPendingOverdub) continue;

			if (clipIndex) *clipIndex = c;
			return clip;
		}
	}

	return NULL;
}

Clip* Song::getNextSessionClipWithOutput(int offset, Output* output, Clip* prevClip) {

	int oldIndex = -1;
	if (prevClip) {
		oldIndex = sessionClips.getIndexForClip(prevClip);
	}

	if (oldIndex == -1) {
		if (offset < 0) oldIndex = sessionClips.getNumElements();
	}

	int newIndex = oldIndex;
	while (true) {
		newIndex += offset;
		if (newIndex == -1 || newIndex == sessionClips.getNumElements()) return NULL;

		Clip* clip = sessionClips.getClipAtIndex(newIndex);
		if (clip->output == output) return clip;
	}
}

void Song::writeToFile() {

	setupClipIndexesForSaving();

	storageManager.writeOpeningTagBeginning("song");

	storageManager.writeFirmwareVersion();
	storageManager.writeEarliestCompatibleFirmwareVersion("4.1.0-alpha");

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	storageManager.writeAttribute("previewNumPads", "40");
#else
	storageManager.writeAttribute("previewNumPads", "144");
#endif

	storageManager.write("\n");
	storageManager.printIndents();
	storageManager.write("preview=\"");

	for (int y = 0; y < displayHeight; y++) {
		for (int x = 0; x < displayWidth + sideBarWidth; x++) {
			for (int colour = 0; colour < 3; colour++) {
				char buffer[3];
				byteToHex(PadLEDs::imageStore[y][x][colour], buffer);
				storageManager.write(buffer);
			}
		}
	}
	storageManager.write("\"");

	if (getRootUI() == &arrangerView) {
		storageManager.writeAttribute("inArrangementView", 1);
		goto weAreInArrangementEditorOrInClipInstance;
	}

	if (lastClipInstanceEnteredStartPos != -1) {
		storageManager.writeAttribute("currentTrackInstanceArrangementPos", lastClipInstanceEnteredStartPos);

weAreInArrangementEditorOrInClipInstance:
		storageManager.writeAttribute("xScrollSongView", xScrollForReturnToSongView);
		storageManager.writeAttribute("xZoomSongView", xZoomForReturnToSongView);
	}

	storageManager.writeAttribute("arrangementAutoScrollOn", arrangerAutoScrollModeActive);

	storageManager.writeAttribute("xScroll", xScroll[NAVIGATION_CLIP]);
	storageManager.writeAttribute("xZoom", xZoom[NAVIGATION_CLIP]);
	storageManager.writeAttribute("yScrollSongView", songViewYScroll);
	storageManager.writeAttribute("yScrollArrangementView", arrangementYScroll);
	storageManager.writeAttribute("xScrollArrangementView", xScroll[NAVIGATION_ARRANGEMENT]);
	storageManager.writeAttribute("xZoomArrangementView", xZoom[NAVIGATION_ARRANGEMENT]);
	storageManager.writeAttribute("timePerTimerTick", timePerTimerTickBig >> 32);
	storageManager.writeAttribute("timerTickFraction", (uint32_t)timePerTimerTickBig);
	storageManager.writeAttribute("rootNote", rootNote);
	storageManager.writeAttribute("inputTickMagnitude",
	                              insideWorldTickMagnitude + insideWorldTickMagnitudeOffsetFromBPM);
	storageManager.writeAttribute("swingAmount", swingAmount);
	storageManager.writeAbsoluteSyncLevelToFile(this, "swingInterval", (SyncLevel)swingInterval);

	if (tripletsOn) storageManager.writeAttribute("tripletsLevel", tripletsLevel);

	storageManager.writeAttribute("affectEntire", affectEntire);
	storageManager.writeAttribute("activeModFunction", globalEffectable.modKnobMode);

	globalEffectable.writeAttributesToFile(false);

	storageManager
	    .writeOpeningTagEnd(); // -------------------------------------------------------------- Attributes end

	storageManager.writeOpeningTag("modeNotes");
	for (int i = 0; i < numModeNotes; i++) {
		storageManager.writeTag("modeNote", modeNotes[i]);
	}
	storageManager.writeClosingTag("modeNotes");

	storageManager.writeOpeningTagBeginning("reverb");
	uint32_t roomSize = AudioEngine::reverb.getroomsize() * (uint32_t)2147483648u;
	uint32_t dampening = AudioEngine::reverb.getdamp() * (uint32_t)2147483648u;
	uint32_t width = AudioEngine::reverb.getwidth() * (uint32_t)2147483648u;

	roomSize = getMin(roomSize, (uint32_t)2147483647);
	dampening = getMin(dampening, (uint32_t)2147483647);
	width = getMin(width, (uint32_t)2147483647);

	storageManager.writeAttribute("roomSize", roomSize);
	storageManager.writeAttribute("dampening", dampening);
	storageManager.writeAttribute("width", width);
	storageManager.writeAttribute("pan", AudioEngine::reverbPan);
	storageManager.writeOpeningTagEnd();

	storageManager.writeOpeningTagBeginning("compressor");
	storageManager.writeAttribute("attack", AudioEngine::reverbCompressor.attack);
	storageManager.writeAttribute("release", AudioEngine::reverbCompressor.release);
	storageManager.writeAttribute("volume", AudioEngine::reverbCompressorVolume);
	storageManager.writeAttribute("shape", AudioEngine::reverbCompressorShape);
	storageManager.writeAttribute("syncLevel", AudioEngine::reverbCompressor.syncLevel);
	storageManager.closeTag();

	storageManager.writeClosingTag("reverb");

	globalEffectable.writeTagsToFile(NULL, false);

	int32_t* valuesForOverride = paramsInAutomationMode ? unautomatedParamValues : NULL;

	storageManager.writeOpeningTagBeginning("songParams");
	GlobalEffectableForClip::writeParamAttributesToFile(&paramManager, true, valuesForOverride);
	storageManager.writeOpeningTagEnd();
	GlobalEffectableForClip::writeParamTagsToFile(&paramManager, true, valuesForOverride);
	storageManager.writeClosingTag("songParams");

	storageManager.writeOpeningTag("instruments");
	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		thisOutput->writeToFile(NULL, this);
	}
	storageManager.writeClosingTag("instruments");

	storageManager.writeOpeningTag("sections");
	for (int s = 0; s < MAX_NUM_SECTIONS; s++) {
		if (true || sections[s].launchMIDICommand.containsSomething() || sections[s].numRepetitions) {
			storageManager.writeOpeningTagBeginning("section");
			storageManager.writeAttribute("id", s, false);
			if (true || sections[s].numRepetitions)
				storageManager.writeAttribute("numRepeats", sections[s].numRepetitions, false);
			if (sections[s].launchMIDICommand.containsSomething()) {
				// Annoyingly, I used one-off tag names here, rather than it conforming to what the LearnedMIDI class now uses.
				storageManager.writeAttribute("midiCommandChannel", sections[s].launchMIDICommand.channelOrZone, false);
				storageManager.writeAttribute("midiCommandNote", sections[s].launchMIDICommand.noteOrCC, false);
				if (sections[s].launchMIDICommand.device) {
					storageManager.writeOpeningTagEnd();
					sections[s].launchMIDICommand.device->writeReferenceToFile("midiCommandDevice");
					storageManager.writeClosingTag("section");
					continue; // We now don't need to close the tag.
				}
			}
			storageManager.closeTag();
		}
	}
	storageManager.writeClosingTag("sections");

	storageManager.writeOpeningTag("sessionClips");
	for (int c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);
		clip->writeToFile(this);
	}
	storageManager.writeClosingTag("sessionClips");

	if (arrangementOnlyClips.getNumElements()) {

		storageManager.writeOpeningTag("arrangementOnlyTracks");
		for (int c = 0; c < arrangementOnlyClips.getNumElements(); c++) {
			Clip* clip = arrangementOnlyClips.getClipAtIndex(c);
			if (!clip->output->clipHasInstance(clip))
				continue; // Get rid of any redundant Clips. There shouldn't be any, but occasionally they somehow get left over.
			clip->writeToFile(this);
		}
		storageManager.writeClosingTag("arrangementOnlyTracks");
	}

	storageManager.writeClosingTag("song");
}

int Song::readFromFile() {

	outputClipInstanceListIsCurrentlyInvalid = true;

	Uart::println("");
	Uart::println("loading song!!!!!!!!!!!!!!");

	char const* tagName;

	for (int s = 0; s < MAX_NUM_SECTIONS; s++) {
		sections[s].numRepetitions = -1;
	}

	uint64_t newTimePerTimerTick = (uint64_t)1 << 32; // TODO: make better!

	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		//Uart::println(tagName); delayMS(30);
		switch (*(uint32_t*)tagName) {

		// "reverb"
		case 0x65766572:
			if (*(((uint32_t*)tagName) + 1) & 0x00FFFFFFFF != 0x00007262) goto unknownTag;
			while (*(tagName = storageManager.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "roomSize")) {
					reverbRoomSize = (float)storageManager.readTagOrAttributeValueInt() / 2147483648u;
					storageManager.exitTag("roomSize");
				}
				else if (!strcmp(tagName, "dampening")) {
					reverbDamp = (float)storageManager.readTagOrAttributeValueInt() / 2147483648u;
					storageManager.exitTag("dampening");
				}
				else if (!strcmp(tagName, "width")) {
					int widthInt = storageManager.readTagOrAttributeValueInt();
					if (widthInt == -2147483648)
						widthInt =
						    2147483647; // Was being saved incorrectly in V2.1.0-beta1 and alphas, so we fix it on read here!
					reverbWidth = (float)widthInt / 2147483648u;
					storageManager.exitTag("width");
				}
				else if (!strcmp(tagName, "pan")) {
					reverbPan = storageManager.readTagOrAttributeValueInt();
					storageManager.exitTag("pan");
				}
				else if (!strcmp(tagName, "compressor")) {
					while (*(tagName = storageManager.readNextTagOrAttributeName())) {
						if (!strcmp(tagName, "attack")) {
							reverbCompressorAttack = storageManager.readTagOrAttributeValueInt();
							storageManager.exitTag("attack");
						}
						else if (!strcmp(tagName, "release")) {
							reverbCompressorRelease = storageManager.readTagOrAttributeValueInt();
							storageManager.exitTag("release");
						}
						else if (!strcmp(tagName, "volume")) {
							reverbCompressorVolume = storageManager.readTagOrAttributeValueInt();
							storageManager.exitTag("volume");
						}
						else if (!strcmp(tagName, "shape")) {
							reverbCompressorShape = storageManager.readTagOrAttributeValueInt();
							storageManager.exitTag("shape");
						}
						else if (!strcmp(tagName, "syncLevel")) {
							reverbCompressorSync = storageManager.readAbsoluteSyncLevelFromFile(this);
							reverbCompressorSync = (SyncLevel)getMin((uint8_t)reverbCompressorSync, (uint8_t)9);
							storageManager.exitTag("syncLevel");
						}
						else {
							storageManager.exitTag(tagName);
						}
					}
					storageManager.exitTag("compressor");
				}
				else {
					storageManager.exitTag(tagName);
				}
			}
			storageManager.exitTag();
			break;

		// "xScr..."
		case 'rcSx':

			switch (*(((uint32_t*)tagName) + 1)) {

			// "xScroll"
			case 0x006c6c6f:
				xScroll[NAVIGATION_CLIP] = storageManager.readTagOrAttributeValueInt();
				xScroll[NAVIGATION_CLIP] = getMax((int32_t)0, xScroll[NAVIGATION_CLIP]);
				break;

			// "xScrollSongView"
			case 0x536c6c6f:
				if (*(((uint32_t*)tagName) + 2) == 0x56676e6f && *(((uint32_t*)tagName) + 3) == 0x00776965) {
					xScrollForReturnToSongView = storageManager.readTagOrAttributeValueInt();
					xScrollForReturnToSongView = getMax((int32_t)0, xScrollForReturnToSongView);
					break;
				}
				else goto unknownTag;

			// "xScrollArrangementView"
			case 0x416c6c6f:
				if (*(((uint32_t*)tagName) + 2) == 0x6e617272 && *(((uint32_t*)tagName) + 3) == 0x656d6567
				    && *(((uint32_t*)tagName) + 4) == 0x6956746e
				    && (*(((uint32_t*)tagName) + 5) & 0x00FFFFFF) == 0x00007765) {
					xScroll[NAVIGATION_ARRANGEMENT] = storageManager.readTagOrAttributeValueInt();
					break;
				}
				else goto unknownTag;

			default:
				goto unknownTag;
			}

			storageManager.exitTag();
			break;

		// "xZoo..."
		case 0x6f6f5a78:

			// "xZoomSongView"
			if (*(((uint32_t*)tagName) + 1) == 0x6e6f536d) {
				if (*(((uint32_t*)tagName) + 2) == 0x65695667
				    && (*(((uint32_t*)tagName) + 3) & 0x0000FFFF) == 0x00000077) {
					xZoomForReturnToSongView = storageManager.readTagOrAttributeValueInt();
					xZoomForReturnToSongView = getMax((int32_t)1, xZoomForReturnToSongView);
				}
				else goto unknownTag;
			}

			// "xZoom"
			else if ((*(((uint32_t*)tagName) + 1) & 0x0000FFFF) == 0x0000006d) {
				xZoom[NAVIGATION_CLIP] = storageManager.readTagOrAttributeValueInt();
				xZoom[NAVIGATION_CLIP] = getMax((uint32_t)1, xZoom[NAVIGATION_CLIP]);
			}
			else goto unknownTag;

			storageManager.exitTag();
			break;

		// "yScr..."
		case 0x72635379:

			switch (*(((uint32_t*)tagName) + 1)) {

			// "yScrollSongView"
			case 0x536c6c6f:
				if (*(((uint32_t*)tagName) + 2) == 0x56676e6f && *(((uint32_t*)tagName) + 3) == 0x00776569) {
					songViewYScroll = storageManager.readTagOrAttributeValueInt();
					songViewYScroll = getMax(1 - displayHeight, songViewYScroll);
					break;
				}
				else goto unknownTag;

			// "yScrollArrangementView"
			case 0x416c6c6f:
				if (*(((uint32_t*)tagName) + 2) == 0x6e617272 && *(((uint32_t*)tagName) + 3) == 0x656d6567
				    && *(((uint32_t*)tagName) + 4) == 0x6956746e
				    && (*(((uint32_t*)tagName) + 5) & 0x00FFFFFF) == 0x00007765) {
					arrangementYScroll = storageManager.readTagOrAttributeValueInt();
					arrangementYScroll = getMax(1 - displayHeight, arrangementYScroll);
					break;
				}
				else goto unknownTag;

			default:
				goto unknownTag;
			}
			storageManager.exitTag();
			break;

		default:
unknownTag:

			if (!strcmp(tagName, "xZoomArrangementView")) {
				xZoom[NAVIGATION_ARRANGEMENT] = storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("xZoomArrangementView");
			}

			else if (!strcmp(
			             tagName,
			             "inArrangementView")) { // For V2.0 pre-beta songs. There'd be another way to detect this...
				lastClipInstanceEnteredStartPos = 0;
				storageManager.exitTag("inArrangementView");
			}

			else if (!strcmp(tagName, "currentTrackInstanceArrangementPos")) {
				lastClipInstanceEnteredStartPos = storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("currentTrackInstanceArrangementPos");
			}

			else if (!strcmp(tagName, "arrangementAutoScrollOn")) {
				arrangerAutoScrollModeActive = storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("arrangementAutoScrollOn");
			}

			else if (!strcmp(tagName, "timePerTimerTick")) {
				newTimePerTimerTick = (newTimePerTimerTick & (uint64_t)0xFFFFFFFF)
				                      | ((uint64_t)storageManager.readTagOrAttributeValueInt() << 32);
				storageManager.exitTag("timePerTimerTick");
			}

			else if (!strcmp(tagName, "timerTickFraction")) {
				newTimePerTimerTick = (newTimePerTimerTick & ((uint64_t)0xFFFFFFFF << 32))
				                      | (uint64_t)(uint32_t)storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("timerTickFraction");
			}

			else if (!strcmp(tagName, "inputTickMagnitude")) {
				insideWorldTickMagnitude = storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("inputTickMagnitude");
			}

			else if (!strcmp(tagName, "rootNote")) {
				rootNote = storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("rootNote");
			}

			else if (!strcmp(tagName, "swingAmount")) {
				swingAmount = storageManager.readTagOrAttributeValueInt();
				swingAmount = getMin(swingAmount, (int8_t)49);
				swingAmount = getMax(swingAmount, (int8_t)-49);
				storageManager.exitTag("swingAmount");
			}

			else if (!strcmp(tagName, "swingInterval")) {
				// swingInterval, unlike other "sync" type params, we're going to read as just its plain old int value, and only shift it by insideWorldTickMagnitude
				// after reading the whole song. This is because these two attributes could easily be stored in either order in the file, so we won't know both until
				// the end. Also, in firmware pre V3.1.0-alpha, all "sync" values were stored as plain old ints, to be read irrespective of insideWorldTickMagnitude
				swingInterval = storageManager.readTagOrAttributeValueInt();
				swingInterval = getMin(swingInterval, (uint8_t)9);
				storageManager.exitTag("swingInterval");
			}

			else if (!strcmp(tagName, "tripletsLevel")) {
				tripletsLevel = storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("tripletsLevel");
				tripletsOn = true;
			}

			else if (!strcmp(tagName, "activeModFunction")) {
				globalEffectable.modKnobMode = storageManager.readTagOrAttributeValueInt();
				globalEffectable.modKnobMode = getMin(globalEffectable.modKnobMode, (uint8_t)(NUM_MOD_BUTTONS - 1));
				storageManager.exitTag("activeModFunction");
			}

			else if (!strcmp(tagName, "affectEntire")) {
				affectEntire = storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("affectEntire");
			}

			else if (!strcmp(tagName, "modeNotes")) {
				numModeNotes = 0;
				uint8_t lowestCurrentAllowed = 0;

				// Read in all the modeNotes
				while (*(tagName = storageManager.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "modeNote")) {
						modeNotes[numModeNotes] = storageManager.readTagOrAttributeValueInt();
						modeNotes[numModeNotes] =
						    getMin((uint8_t)11, getMax(lowestCurrentAllowed, modeNotes[numModeNotes]));
						lowestCurrentAllowed = modeNotes[numModeNotes] + 1;
						numModeNotes++;
						storageManager.exitTag("modeNote");
					}
					else storageManager.exitTag(tagName);
				}
				storageManager.exitTag("modeNotes");
			}

			else if (!strcmp(tagName, "sections")) {
				// Read in all the sections
				while (*(tagName = storageManager.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "section")) {

						uint8_t id = 255;
						MIDIDevice* device = NULL;
						uint8_t channel = 255;
						uint8_t note = 255;
						int16_t numRepeats = 0;

						while (*(tagName = storageManager.readNextTagOrAttributeName())) {
							if (!strcmp(tagName, "id")) {
								id = storageManager.readTagOrAttributeValueInt();
							}

							else if (!strcmp(tagName, "numRepeats")) {
								numRepeats = storageManager.readTagOrAttributeValueInt();
								if (numRepeats < -1 || numRepeats > 9999) numRepeats = 0;
							}

							// Annoyingly, I used one-off tag names here, rather than it conforming to what the LearnedMIDI class now uses.
							else if (!strcmp(tagName, "midiCommandDevice")) {
								device = MIDIDeviceManager::readDeviceReferenceFromFile();
							}

							else if (!strcmp(tagName, "midiCommandChannel")) {
								channel = storageManager.readTagOrAttributeValueInt();
							}

							else if (!strcmp(tagName, "midiCommandNote")) {
								note = storageManager.readTagOrAttributeValueInt();
							}

							storageManager.exitTag(tagName);
						}

						if (id < MAX_NUM_SECTIONS) {
							if (channel < 16 && note < 128) {
								sections[id].launchMIDICommand.device = device;
								sections[id].launchMIDICommand.channelOrZone = channel;
								sections[id].launchMIDICommand.noteOrCC = note;
							}
							sections[id].numRepetitions = numRepeats;
						}
						storageManager.exitTag("section");
					}
					else storageManager.exitTag(tagName);
				}
				storageManager.exitTag("sections");
			}

			else if (!strcmp(tagName, "instruments")) {

				Output** lastPointer = &firstOutput;
				while (*(tagName = storageManager.readNextTagOrAttributeName())) {

					void* memory;
					Output* newOutput;
					char const* defaultDirPath;
					int error;

					if (!strcmp(tagName, "audioTrack")) {
						memory = generalMemoryAllocator.alloc(sizeof(AudioOutput), NULL, false, true);
						if (!memory) return ERROR_INSUFFICIENT_RAM;
						newOutput = new (memory) AudioOutput();
						goto loadOutput;
					}

					else if (!strcmp(tagName, "sound")) {
						memory = generalMemoryAllocator.alloc(sizeof(SoundInstrument), NULL, false, true);
						if (!memory) return ERROR_INSUFFICIENT_RAM;
						newOutput = new (memory) SoundInstrument();
						defaultDirPath = "SYNTHS";

setDirPathFirst:
						error = ((Instrument*)newOutput)->dirPath.set(defaultDirPath);
						if (error) {
gotError:
							newOutput->~Output();
							generalMemoryAllocator.dealloc(memory);
							return error;
						}

loadOutput:
						error = newOutput->readFromFile(
						    this, NULL,
						    0); // If it finds any default params, it'll make a ParamManager and "back it up"
						if (error) goto gotError;

						*lastPointer = newOutput;
						lastPointer = &newOutput->next;
					}

					else if (!strcmp(tagName, "kit")) {
						memory = generalMemoryAllocator.alloc(sizeof(Kit), NULL, false, true);
						if (!memory) return ERROR_INSUFFICIENT_RAM;
						newOutput = new (memory) Kit();
						defaultDirPath = "KITS";
						goto setDirPathFirst;
					}

					else if (!strcmp(tagName, "midiChannel") || !strcmp(tagName, "mpeZone")) {
						memory = generalMemoryAllocator.alloc(sizeof(MIDIInstrument), NULL, false, true);
						if (!memory) return ERROR_INSUFFICIENT_RAM;
						newOutput = new (memory) MIDIInstrument();
						goto loadOutput;
					}

					else if (!strcmp(tagName, "cvChannel")) {
						memory = generalMemoryAllocator.alloc(sizeof(CVInstrument), NULL, false, true);
						if (!memory) return ERROR_INSUFFICIENT_RAM;
						newOutput = new (memory) CVInstrument();
						goto loadOutput;
					}

					storageManager.exitTag();
				}
				storageManager.exitTag("instruments");
			}

			else if (!strcmp(tagName, "songParams")) {
				GlobalEffectableForClip::readParamsFromFile(&paramManager, 2147483647);
				storageManager.exitTag("songParams");
			}

			else if (!strcmp(tagName, "tracks") || !strcmp(tagName, "sessionClips")) {
				int error = readClipsFromFile(&sessionClips);
				if (error) return error;
				storageManager.exitTag();
			}

			else if (!strcmp(tagName, "arrangementOnlyTracks") || !strcmp(tagName, "arrangementOnlyClips")) {
				int error = readClipsFromFile(&arrangementOnlyClips);
				if (error) return error;
				storageManager.exitTag();
			}

			else {
				int result = globalEffectable.readTagFromFile(tagName, &paramManager, 2147483647, this);
				if (result == NO_ERROR) {}
				else if (result != RESULT_TAG_UNUSED) return result;
				else {
					int result = storageManager.tryReadingFirmwareTagFromFile(tagName);
					if (result && result != RESULT_TAG_UNUSED) return result;
					if (ALPHA_OR_BETA_VERSION) {
						Uart::print("unknown tag: ");
						Uart::println(tagName);
					}
					storageManager.exitTag(tagName);
				}
			}
		}
	}

	if (storageManager.firmwareVersionOfFileBeingRead >= FIRMWARE_3P1P0_ALPHA2) {
		// Basically, like all other "sync" type parameters, the file value and internal value are different for swingInterval.
		// But unlike other ones, which get converted as we go, we do this one at the end once we know we have enough info to do the conversion
		swingInterval = convertSyncLevelFromFileValueToInternalValue(swingInterval);
	}

	setTimePerTimerTick(newTimePerTimerTick);

	// Ensure all arranger-only Clips have their section as 255
	for (int t = 0; t < arrangementOnlyClips.getNumElements(); t++) {
		Clip* clip = arrangementOnlyClips.getClipAtIndex(t);

		clip->section = 255;
		clip->gotInstanceYet = false;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	int count = 0;
	// Match all Clips up with their Output
	// For each Clip in session and arranger
	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* thisClip = clipArray->getClipAtIndex(c); // TODO: deal with other Clips too!

		if (!(count & 31)) {
			AudioEngine::routineWithClusterLoading(); // -----------------------------------
			AudioEngine::logAction("aaa0");
		}
		count++;

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(thisClip);

		int error = thisClip->claimOutput(modelStackWithTimelineCounter);
		if (error) return error;

		// Correct different non-synced rates of old song files
		// In a perfect world, we'd do this for Kits, MIDI and CV too
		if (storageManager.firmwareVersionOfFileBeingRead < FIRMWARE_1P5P0_PREBETA
		    && thisClip->output->type == INSTRUMENT_TYPE_SYNTH) {
			if (((InstrumentClip*)thisClip)->arpSettings.mode && !((InstrumentClip*)thisClip)->arpSettings.syncLevel) {
				ParamManagerForTimeline* thisParamManager = &thisClip->paramManager;
				thisParamManager->getPatchedParamSet()->params[PARAM_GLOBAL_ARP_RATE].shiftValues((1 << 30)
				                                                                                  + (1 << 28));
			}
		}
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips;
	}

	AudioEngine::logAction("matched up");

	AudioEngine::routineWithClusterLoading(); // -----------------------------------

	anyOutputsSoloingInArrangement = false;

	Uart::println("aaa1");

	// Match all ClipInstances up with their Clip. And while we're at it, check if any Outputs are soloing in arranger
	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput->soloingInArrangementMode) anyOutputsSoloingInArrangement = true;

		for (int i = 0; i < thisOutput->clipInstances.getNumElements(); i++) {
			ClipInstance* thisInstance = thisOutput->clipInstances.getElement(i);

			// Grab out the encoded Clip reference and turn it into an actual Clip*
			uint32_t clipCode = (uint32_t)thisInstance->clip;

			// Special case for NULL Clip
			if (clipCode == 0xFFFFFFFF) {
				thisInstance->clip = NULL;
			}

			else {
				unsigned int lookingForIndex = clipCode & ~((uint32_t)1 << 31);
				bool isArrangementClip = clipCode >> 31;

				ClipArray* clips = isArrangementClip ? &arrangementOnlyClips : &sessionClips;

				if (lookingForIndex >= clips->getNumElements()) {
#if ALPHA_OR_BETA_VERSION
					numericDriver.displayPopup("E248");
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
					numericDriver.displayPopup("E041");
#endif
					goto skipInstance;
				}

				// If arrangement-only and it somehow had already got a ClipInstance...
				if (isArrangementClip && thisInstance->clip->gotInstanceYet) {

#if ALPHA_OR_BETA_VERSION
					numericDriver.displayPopup("E042");
#endif
					goto skipInstance;
				}

				// If still here, can mark the Clip as claimed
				thisInstance->clip->gotInstanceYet = true;
			}
		}

		// If saved before V2.1, set sample-based synth instruments to linear interpolation, cos that's how it was
		if (storageManager.firmwareVersionOfFileBeingRead < FIRMWARE_2P1P0_BETA) {
			if (thisOutput->type == INSTRUMENT_TYPE_SYNTH) {
				SoundInstrument* sound = (SoundInstrument*)thisOutput;

				for (int s = 0; s < NUM_SOURCES; s++) {
					Source* source = &sound->sources[s];
					if (source->oscType == OSC_TYPE_SAMPLE) {
						source->sampleControls.interpolationMode = INTERPOLATION_MODE_LINEAR;
					}
				}
			}
		}
	}

	outputClipInstanceListIsCurrentlyInvalid = false; // All clipInstances are valid now.

	Uart::println("aaa2");

	// Ensure no arrangement-only Clips with no ClipInstance
	// For each Clip in arrangement
	for (int c = 0; c < arrangementOnlyClips.getNumElements(); c++) {
		Clip* clip = arrangementOnlyClips.getClipAtIndex(c);

		if (!clip->gotInstanceYet) {
#if ALPHA_OR_BETA_VERSION
			numericDriver.displayPopup("E043");
#endif
			if (currentClip == clip) currentClip = NULL;
			if (syncScalingClip == clip) syncScalingClip = NULL;

			arrangementOnlyClips.deleteAtIndex(c);
			deleteClipObject(clip, false, INSTRUMENT_REMOVAL_NONE);
			c--;
		}
	}

	// Pre V1.2...
	if (storageManager.firmwareVersionOfFileBeingRead < FIRMWARE_1P2P0) {

		deleteAllBackedUpParamManagers(true); // Before V1.2, lots of extras of these could be created during loading
		globalEffectable.compensateVolumeForResonance(&paramManager);
	}

	if (syncScalingClip) {
		Clip* newInputTickScaleClip = syncScalingClip;
		syncScalingClip = NULL; // We shouldn't have set this manually earlier, anyway - we just saved hassle
		setInputTickScaleClip(newInputTickScaleClip);
	}

	Uart::println("aaa3");
	AudioEngine::logAction("aaa3.1");

	AudioEngine::routineWithClusterLoading(); // -----------------------------------

	reassessWhetherAnyClipsSoloing();

	AudioEngine::logAction("aaa4.2");

	setupPatchingForAllParamManagers();
	AudioEngine::logAction("aaa4.3");

	AudioEngine::routineWithClusterLoading(); // -----------------------------------

	int playbackWillStartInArrangerAtPos = playbackHandler.playbackState ? lastClipInstanceEnteredStartPos : -1;

	AudioEngine::logAction("aaa5.1");
	sortOutWhichClipsAreActiveWithoutSendingPGMs(modelStack, playbackWillStartInArrangerAtPos);
	AudioEngine::logAction("aaa5.2");

	AudioEngine::routineWithClusterLoading(); // -----------------------------------

	return NO_ERROR;
}

int Song::readClipsFromFile(ClipArray* clipArray) {
	char const* tagName;

	while (*(tagName = storageManager.readNextTagOrAttributeName())) {

		int allocationSize;
		int clipType;

		if (!strcmp(tagName, "track") || !strcmp(tagName, "instrumentClip")) {
			allocationSize = sizeof(InstrumentClip);
			clipType = CLIP_TYPE_INSTRUMENT;

readClip:
			if (!clipArray->ensureEnoughSpaceAllocated(1)) return ERROR_INSUFFICIENT_RAM;

			void* memory = generalMemoryAllocator.alloc(allocationSize, NULL, false, true);
			if (!memory) return ERROR_INSUFFICIENT_RAM;

			Clip* newClip;
			if (clipType == CLIP_TYPE_INSTRUMENT) {
				newClip = new (memory) InstrumentClip();
			}
			else {
				newClip = new (memory) AudioClip();
			}

			int error = newClip->readFromFile(this);
			if (error) {
				newClip->~Clip();
				generalMemoryAllocator.dealloc(memory);
				return error;
			}

			clipArray->insertClipAtIndex(newClip, clipArray->getNumElements()); // We made sure enough space, above

			storageManager.exitTag();
		}
		else if (!strcmp(tagName, "audioClip")) {
			allocationSize = sizeof(AudioClip);
			clipType = CLIP_TYPE_AUDIO;

			goto readClip;
		}
		else storageManager.exitTag(tagName);
	}

	return NO_ERROR;
}

// Needs to be in a separate function than the above because the main song XML file needs to be closed first before this is called, because this will open other (sample) files
void Song::loadAllSamples(bool mayActuallyReadFiles) {

	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		thisOutput->loadAllAudioFiles(mayActuallyReadFiles);
	}

	// For each Clip in session and arranger
	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {

		// If not reading files, high chance that we'll be searching through memory a lot and not reading the card (which would call the audio routine), so we'd
		// better call the audio routine here.
		if (!mayActuallyReadFiles && !(c & 7)) { // 31 bad. 15 seems to pass. 7 to be safe
			AudioEngine::logAction("Song::loadAllSamples");
			AudioEngine::routineWithClusterLoading(); // -----------------------------------
		}

		Clip* clip = clipArray->getClipAtIndex(c);
		if (clip->type == CLIP_TYPE_AUDIO) {
			((AudioClip*)clip)->loadSample(mayActuallyReadFiles);
		}
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips;
	}
}

void Song::loadCrucialSamplesOnly() {

	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput->activeClip && isClipActive(thisOutput->activeClip)) {
			thisOutput->loadCrucialAudioFilesOnly();
		}
	}

	// For each Clip in session and arranger
	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);
		if (clip->isActiveOnOutput() && clip->type == CLIP_TYPE_AUDIO) {
			((AudioClip*)clip)->loadSample(true);
		}
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips;
	}
}

void Song::deleteSoundsWhichWontSound() {

	// Delete Clips inactive on Output
	// For each Clip in session and arranger
	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);

		AudioEngine::routineWithClusterLoading(); // -----------------------------------
		if (!clip->isActiveOnOutput() && clip != view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
			deleteClipObject(clip, false, INSTRUMENT_REMOVAL_NONE);
			clipArray->deleteAtIndex(c);
			c--;
		}
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips;
	}

	// Now there's only one Clip left per Output

	// Delete Clips which won't sound
	// For each Clip in session and arranger
	clipArray = &sessionClips;
traverseClips2:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);

		AudioEngine::routineWithClusterLoading(); // -----------------------------------
		if (clip->deleteSoundsWhichWontSound(this)) {
			deleteClipObject(clip, false, INSTRUMENT_REMOVAL_DELETE);
			clipArray->deleteAtIndex(c);
			c--;
		}
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips2;
	}

	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		thisOutput->clipInstances.empty();
	}

	deleteAllOutputs((Output**)&firstHibernatingInstrument);
	deleteHibernatingMIDIInstrument();

	// Can't delete the ones with no Clips, cos these might be needed by their ModControllables
	deleteAllBackedUpParamManagersWithClips();
}

void Song::renderAudio(StereoSample* outputBuffer, int numSamples, int32_t* reverbBuffer, int32_t sideChainHitPending) {

	//int32_t volumePostFX = getParamNeutralValue(PARAM_GLOBAL_VOLUME_POST_FX);
	int32_t volumePostFX = getFinalParameterValueVolume(
	                           134217728, cableToLinearParamShortcut(paramManager.getUnpatchedParamSet()->getValue(
	                                          PARAM_UNPATCHED_GLOBALEFFECTABLE_VOLUME)))
	                       >> 1;

	// A "post-FX volume" calculation also happens in audioDriver.render(), which is a bit more relevant really because that's where filters are happening

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	for (Output* output = firstOutput; output; output = output->next) {
		if (!output->inValidState) continue;

		bool isClipActiveNow = (output->activeClip && isClipActive(output->activeClip->getClipBeingRecordedFrom()));

		//AudioEngine::logAction("outp->render");
		output->renderOutput(modelStack, outputBuffer, outputBuffer + numSamples, numSamples, reverbBuffer,
		                     volumePostFX >> 1, sideChainHitPending, !isClipActiveNow, isClipActiveNow);
		//AudioEngine::logAction("/outp->render");
	}

	// If recording the "MIX", this is the place where we want to grab it - before any master FX or volume applied
	// Go through each SampleRecorder, feeding them audio
	for (SampleRecorder* recorder = AudioEngine::firstRecorder; recorder; recorder = recorder->next) {

		if (recorder->status >= RECORDER_STATUS_FINISHED_CAPTURING_BUT_STILL_WRITING) continue;

		if (recorder->mode == AUDIO_INPUT_CHANNEL_MIX) {
			recorder->feedAudio((int32_t*)outputBuffer, numSamples, true);
		}
	}

	DelayWorkingState delayWorkingState;
	globalEffectable.setupDelayWorkingState(&delayWorkingState, &paramManager);

	globalEffectable.processFXForGlobalEffectable(outputBuffer, numSamples, &volumePostFX, &paramManager,
	                                              &delayWorkingState, 8);

	int32_t postReverbVolume = paramNeutralValues[PARAM_GLOBAL_VOLUME_POST_REVERB_SEND];
	int32_t reverbSendAmount =
	    getFinalParameterValueVolume(paramNeutralValues[PARAM_GLOBAL_REVERB_AMOUNT],
	                                 cableToLinearParamShortcut(paramManager.getUnpatchedParamSet()->getValue(
	                                     PARAM_UNPATCHED_GLOBALEFFECTABLE_REVERB_SEND_AMOUNT)));

	globalEffectable.processReverbSendAndVolume(outputBuffer, numSamples, reverbBuffer, volumePostFX, postReverbVolume,
	                                            reverbSendAmount >> 1);

	if (playbackHandler.isEitherClockActive() && !playbackHandler.ticksLeftInCountIn
	    && currentPlaybackMode == &arrangement) {

		if (paramManager.getUnpatchedParamSetSummary()->whichParamsAreInterpolating[0]
#if MAX_NUM_UNPATCHED_PARAMS > 32
		    || paramManager.getUnpatchedParamSetSummary()->whichParamsAreInterpolating[1]
#endif
		) {

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
	if (currentSong == this && (playbackHandler.playbackState & PLAYBACK_CLOCK_INTERNAL_ACTIVE)) {
		uint32_t timeSinceLastTimerTick =
		    AudioEngine::audioSampleTimer - (uint32_t)(playbackHandler.timeLastTimerTickBig >> 32);

		// Using intermediary float here is best: newTimeBig might be a huge number, and below about 2bpm (so not that low), multiplying it by timeSinceLastTimerTick can overflow a uint64_t
		timeSinceLastTimerTick = (float)timeSinceLastTimerTick * newTimeBig / timePerTimerTickBig;

		playbackHandler.timeLastTimerTickBig =
		    (uint64_t)(uint32_t)(AudioEngine::audioSampleTimer - timeSinceLastTimerTick) << 32;

		uint32_t timeTilNextTimerTick =
		    (uint32_t)(playbackHandler.timeNextTimerTickBig >> 32) - AudioEngine::audioSampleTimer;

		// Using intermediary float here is best: newTimeBig might be a huge number, and below about 2bpm (so not that low), multiplying it by timeSinceLastTimerTick can overflow a uint64_t
		timeTilNextTimerTick = (float)timeTilNextTimerTick * newTimeBig / timePerTimerTickBig;

		playbackHandler.timeNextTimerTickBig = (uint64_t)(AudioEngine::audioSampleTimer + timeTilNextTimerTick) << 32;
	}

	timePerTimerTickBig = newTimeBig;

	divideByTimePerTimerTick = ((uint64_t)1 << 63) / ((newTimeBig * 3) >> 1);

	// Reschedule upcoming swung, MIDI and trigger clock out ticks
	if (currentSong == this && (playbackHandler.playbackState & PLAYBACK_CLOCK_INTERNAL_ACTIVE)) {
		playbackHandler.scheduleSwungTickFromInternalClock();
		if (cvEngine.isTriggerClockOutputEnabled()) playbackHandler.scheduleTriggerClockOutTick();
		if (playbackHandler.currentlySendingMIDIOutputClocks()) playbackHandler.scheduleMIDIClockOutTick();
	}
}

bool Song::hasAnySwing() {
	return (swingAmount != 0);
}

void Song::resyncLFOsAndArpeggiators() {

	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput->activeClip) thisOutput->resyncLFOs();
		// Yes, do it even for Clips that aren't actually "playing" / active in the Song
	}
}

NoteRow* Song::findNoteRowForDrum(Kit* kit, Drum* drum, Clip* stopTraversalAtClip) {

	// If currently swapping an Instrument, it can't be assumed that all arranger-only Clips for this Instrument are in its clipInstances, which otherwise is a nice time-saver

	// For each Clip in session and arranger for specific Output - but if currentlySwappingInstrument, use master list for arranger Clips
	ClipArray* clipArray = &sessionClips;
	bool doingClipsProvidedByOutput = false;
decideNumElements:
	int numElements = clipArray->getNumElements();
traverseClips:
	for (int c = 0; c < numElements; c++) {
		InstrumentClip* instrumentClip;
		if (!doingClipsProvidedByOutput) {
			Clip* clip = clipArray->getClipAtIndex(c);
			if (clip == stopTraversalAtClip) return NULL;
			if (clip->output != kit) continue;
			instrumentClip = (InstrumentClip*)clip;
		}
		else {
			ClipInstance* clipInstance = kit->clipInstances.getElement(c);
			if (!clipInstance->clip) continue;
			if (!clipInstance->clip->isArrangementOnlyClip()) continue;
			instrumentClip = (InstrumentClip*)clipInstance->clip;
		}

		NoteRow* noteRow = instrumentClip->getNoteRowForDrum(drum);
		if (noteRow) return noteRow;
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
	if (!noteRow) return NULL;
	return &noteRow->paramManager;
}

void Song::setupPatchingForAllParamManagersForDrum(SoundDrum* drum) {

	// If currently swapping an Instrument, it can't be assumed that all arranger-only Clips for this Instrument are in its clipInstances, which otherwise is a nice time-saver

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	// We don't know the Kit / Instrument. That's unfortunate. We'll work it out if we can
	Output* output = NULL;

	// For each Clip in session and arranger for specific Output - but if currentlySwappingInstrument, use master list for arranger Clips
	ClipArray* clipArray = &sessionClips;
	bool doingClipsProvidedByOutput = false;
decideNumElements:
	int numElements = clipArray->getNumElements();
traverseClips:
	for (int c = 0; c < numElements; c++) {
		InstrumentClip* instrumentClip;
		if (!doingClipsProvidedByOutput) {
			Clip* clip = clipArray->getClipAtIndex(c);

			if (output) {
				if (clip->output != output) continue;
			}
			else {
				if (clip->output->type != INSTRUMENT_TYPE_KIT) continue;
			}

			instrumentClip = (InstrumentClip*)clip;
		}
		else {
			ClipInstance* clipInstance = output->clipInstances.getElement(c);
			if (!clipInstance->clip) continue;
			if (!clipInstance->clip->isArrangementOnlyClip()) continue;
			instrumentClip = (InstrumentClip*)clipInstance->clip;
		}

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(instrumentClip);

		ModelStackWithNoteRow* modelStackWithNoteRow =
		    instrumentClip->getNoteRowForDrum(modelStackWithTimelineCounter, drum);

		NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
		if (noteRow) {
			if (!output)
				output =
				    instrumentClip->output; // The "if" check at the start should be unnecessary... but just in case.

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
		// If Instrument is currently being "swapped", or if we still don't actually know the Output, then we have to do things the slow way
		// and search every arrangementOnlyClip
		if (outputClipInstanceListIsCurrentlyInvalid || !output) {
			clipArray = &arrangementOnlyClips;
			goto decideNumElements;
		}

		// Or more ideally, we would have found the Output along the way, so we can just grab its arrangement-only Clips directly from it
		else {
			doingClipsProvidedByOutput = true;
			numElements = output->clipInstances.getNumElements();
			goto traverseClips;
		}
	}
}

void Song::setupPatchingForAllParamManagersForInstrument(SoundInstrument* sound) {

	// If currently swapping an Instrument, it can't be assumed that all arranger-only Clips for this Instrument are in its clipInstances, which otherwise is a nice time-saver

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithModControllable* modelStack = setupModelStackWithSong(modelStackMemory, this)
	                                                ->addTimelineCounter(NULL)
	                                                ->addModControllableButNoNoteRow(sound);

	// For each Clip in session and arranger for specific Output
	int numElements = sessionClips.getNumElements();
	bool doingArrangementClips = false;
traverseClips:
	for (int c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = sessionClips.getClipAtIndex(c);
			if (clip->output != sound) continue;
		}
		else {
			ClipInstance* clipInstance = sound->clipInstances.getElement(c);
			if (!clipInstance->clip) continue;
			if (!clipInstance->clip->isArrangementOnlyClip()) continue;
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

	if (!device->hasDefaultVelocityToLevelSet()) return;

	// TODO: backed up ParamManagers?

	// If currently swapping an Instrument, it can't be assumed that all arranger-only Clips for this Instrument are in its clipInstances, which otherwise is a nice time-saver

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithModControllable* modelStack = setupModelStackWithSong(modelStackMemory, this)
	                                                ->addTimelineCounter(NULL)
	                                                ->addModControllableButNoNoteRow(instrument);

	// For each Clip in session and arranger for specific Output
	int numElements = sessionClips.getNumElements();
	bool doingArrangementClips = false;
traverseClips:
	for (int c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = sessionClips.getClipAtIndex(c);
			if (clip->output != instrument) continue;
		}
		else {
			ClipInstance* clipInstance = instrument->clipInstances.getElement(c);
			if (!clipInstance->clip) continue;
			if (!clipInstance->clip->isArrangementOnlyClip()) continue;
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

	if (!device->hasDefaultVelocityToLevelSet()) return;

	// TODO: backed up ParamManagers?

	// If currently swapping an Instrument, it can't be assumed that all arranger-only Clips for this Instrument are in its clipInstances, which otherwise is a nice time-saver

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	// For each Clip in session and arranger for specific Output
	int numElements = sessionClips.getNumElements();
	bool doingArrangementClips = false;
traverseClips:
	for (int c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = sessionClips.getClipAtIndex(c);
			if (clip->output != kit) continue;
		}
		else {
			ClipInstance* clipInstance = kit->clipInstances.getElement(c);
			if (!clipInstance->clip) continue;
			if (!clipInstance->clip->isArrangementOnlyClip()) continue;
			clip = clipInstance->clip;
		}

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

		ModelStackWithNoteRow* modelStackWithNoteRow =
		    ((InstrumentClip*)clip)->getNoteRowForDrum(modelStackWithTimelineCounter, drum);
		NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
		if (!noteRow) continue;

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
	//if (!device->hasDefaultVelocityToLevelSet()) return; // In this case, we'll take 0 to actually mean zero.

	// TODO: backed up ParamManagers?

	// If currently swapping an Instrument, it can't be assumed that all arranger-only Clips for this Instrument are in its clipInstances, which otherwise is a nice time-saver

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	// For each Clip in session and arranger
	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

		Output* output = clip->output;

		if (output->type == INSTRUMENT_TYPE_SYNTH) {
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

		else if (output->type == INSTRUMENT_TYPE_KIT) {
			Kit* kit = (Kit*)output;

			for (Drum* drum = kit->firstDrum; drum; drum = drum->next) {
				if (drum->type == DRUM_TYPE_SOUND && drum->midiInput.containsSomething()
				    && drum->midiInput.device == device) {

					ModelStackWithNoteRow* modelStackWithNoteRow =
					    ((InstrumentClip*)clip)->getNoteRowForDrum(modelStackWithTimelineCounter, drum);
					NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
					if (!noteRow) continue;

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
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips;
	}
}

int Song::cycleThroughScales() {
	// Can only do it if there are 7 notes in current scale
	if (numModeNotes != 7) return 255;

	int currentScale = getCurrentPresetScale();

	int newScale = currentScale + 1;
	if (newScale >= NUM_PRESET_SCALES) newScale = 0;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	// Firstly, make all current mode notes as high as they can possibly go, so there'll be no crossing over when we go to actually do it, below
	// For each InstrumentClip in session and arranger
	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);
		if (clip->type != CLIP_TYPE_INSTRUMENT) continue;
		InstrumentClip* instrumentClip = (InstrumentClip*)clip;

		if (instrumentClip->isScaleModeClip()) {
			for (int n = 6; n >= 1; n--) {
				int newNote = 5 + n;
				int oldNote = modeNotes[n];
				if (oldNote != newNote) {
					ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);
					instrumentClip->musicalModeChanged(n, newNote - oldNote, modelStackWithTimelineCounter);
				}
			}
		}
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips;
	}

	for (int n = 1; n < 7; n++) {
		modeNotes[n] = 5 + n;
	}

	// And now, set the mode notes to what they're actually supposed to be
	// For each InstrumentClip in session and arranger
	clipArray = &sessionClips;
traverseClips2:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);
		if (clip->type != CLIP_TYPE_INSTRUMENT) continue;
		InstrumentClip* instrumentClip = (InstrumentClip*)clip;

		if (instrumentClip->isScaleModeClip()) {
			for (int n = 1; n < 7; n++) {
				int newNote = presetScaleNotes[newScale][n];
				int oldNote = modeNotes[n];
				if (oldNote != newNote) {
					ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);
					instrumentClip->musicalModeChanged(n, newNote - oldNote, modelStackWithTimelineCounter);
				}
			}
		}
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips2;
	}

	for (int n = 1; n < 7; n++) {
		modeNotes[n] = presetScaleNotes[newScale][n];
	}

	return newScale;
}

// Returns 255 if none
int Song::getCurrentPresetScale() {
	if (numModeNotes != 7) return 255;

	for (int p = 0; p < NUM_PRESET_SCALES; p++) {
		for (int n = 1; n < 7; n++) {
			if (modeNotes[n] != presetScaleNotes[p][n]) goto notThisOne;
		}

		// If we're here, must be this one!
		return p;

notThisOne : {}
	}

	return 255;
}

// What does this do exactly, again?
void Song::ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(Sound* sound) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	// For each InstrumentClip in session and arranger
	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);
		if (clip->type != CLIP_TYPE_INSTRUMENT) continue;

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

		InstrumentClip* instrumentClip = (InstrumentClip*)clip;

		instrumentClip->ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(modelStackWithTimelineCounter, sound);
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips;
	}
}

void Song::setTempoFromNumSamples(double newTempoSamples, bool shouldLogAction) {
	uint64_t newTimePerTimerTickBig;

	if (newTempoSamples >= (double)4294967296) {
		newTimePerTimerTickBig = -1; // Sets it to maximum unsigned 64-bit int
	}
	else {
		newTimePerTimerTickBig = newTempoSamples * 4294967296 + 0.5;
		if ((newTimePerTimerTickBig >> 32) < minTimePerTimerTick) {
			newTimePerTimerTickBig = (uint64_t)minTimePerTimerTick << 32;
		}
	}

	setTimePerTimerTick(newTimePerTimerTickBig, shouldLogAction);
}

void Song::setBPM(float tempoBPM, bool shouldLogAction) {
	if (insideWorldTickMagnitude > 0) tempoBPM *= ((uint32_t)1 << (insideWorldTickMagnitude));
	double timePerTimerTick = (double)110250 / tempoBPM;
	if (insideWorldTickMagnitude < 0) timePerTimerTick *= ((uint32_t)1 << (-insideWorldTickMagnitude));
	setTempoFromNumSamples(timePerTimerTick, shouldLogAction);
}

void Song::setTempoFromParams(int32_t magnitude, int8_t whichValue, bool shouldLogAction) {
	float newBPM = metronomeValuesBPM[whichValue];
	magnitude += insideWorldTickMagnitude + insideWorldTickMagnitudeOffsetFromBPM;
	if (magnitude > 0) newBPM /= ((uint32_t)1 << magnitude);
	else if (magnitude < 0) newBPM *= ((uint32_t)1 << (0 - magnitude));

	setBPM(newBPM, shouldLogAction);
}

void Song::deleteClipObject(Clip* clip, bool songBeingDestroyedToo, int instrumentRemovalInstruction) {

	if (!songBeingDestroyedToo) {

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = setupModelStackWithTimelineCounter(modelStackMemory, this, clip);

		clip->prepareForDestruction(modelStack, instrumentRemovalInstruction);
	}

#if ALPHA_OR_BETA_VERSION
	if (clip->type == CLIP_TYPE_AUDIO) {
		if (((AudioClip*)clip)->recorder) numericDriver.freezeWithError("i001"); // Trying to diversify Qui's E278
	}
#endif

	void* toDealloc = dynamic_cast<void*>(clip);
	clip->~Clip();
	generalMemoryAllocator.dealloc(toDealloc);
}

int Song::getMaxMIDIChannelSuffix(int channel) {
	if (channel >= 16) return -1; // MPE zones - just don't do freakin suffixes?

	bool* inUse = (bool*)shortStringBuffer; // Only actually needs to be 27 long
	memset(inUse, 0, 27);

	int maxSuffix = -2;

	for (Output* output = firstOutput; output; output = output->next) {
		if (output->type == INSTRUMENT_TYPE_MIDI_OUT) {
			MIDIInstrument* instrument = (MIDIInstrument*)output;
			if (instrument->channel == channel) {
				int suffix = instrument->channelSuffix;
				if (suffix < -1 || suffix >= 26) continue; // Just for safety
				inUse[suffix + 1] = true;
				if (suffix > maxSuffix) maxSuffix = suffix;
			}
		}
	}

	// Find first empty suffix
	for (int s = -1; s < 26; s++) {
		if (!inUse[s + 1]) {
			if (s < maxSuffix) return maxSuffix;
			else return s;
		}
	}

	// If here, they're all full up - that's not great
	return 25; // "Z"
}

bool Song::getAnyClipsSoloing() {
	return anyClipsSoloing;
}

void Song::reassessWhetherAnyClipsSoloing() {
	anyClipsSoloing = false;

	// For each Clip in session
	for (int c = 0; c < sessionClips.getNumElements(); c++) {
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
			for (int l = 0; l < sessionClips.getNumElements(); l++) {
				Clip* loopable = sessionClips.getClipAtIndex(l);
				loopable->armState = ARM_STATE_OFF;
			}
		}
		return;
	}

	// Stop all other playing-but-not-soloing Clips, and turn all soloing Clip into playing Clip!
	// For each Clip in session
	for (int c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		clip->activeIfNoSolo = clip->soloingInSessionMode;
		clip->soloingInSessionMode = false;

		if (getRidOfArmingToo) {
			clip->armState = ARM_STATE_OFF;
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
		while (*prevPointer)
			prevPointer = &(*prevPointer)->next;
		*prevPointer = output;
		output->next = NULL;
	}

	if (output->soloingInArrangementMode) anyOutputsSoloingInArrangement = true;

	// Must resync LFOs - these (if synced) will roll even when no activeClip
	if (playbackHandler.isEitherClockActive() && this == currentSong) output->resyncLFOs();
}

// Make sure you first free all the Instrument's voices before calling this!
void Song::deleteOutputThatIsInMainList(
    Output* output,
    bool
        stopAnyAuditioningFirst // Usually true, but if deleting while loading a Song due to invalid data, we don't want this, and it'd cause an error.
) {

	removeOutputFromMainList(output, stopAnyAuditioningFirst);

	output->prepareForHibernationOrDeletion();
	deleteOutput(output);
}

// Returns index, or -1 if error
int Song::removeOutputFromMainList(
    Output* output,
    bool
        stopAnyAuditioningFirst // Usually true, but if deleting while loading a Song due to invalid data, we don't want this, and it'd cause an error.
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
	int outputIndex = 0;
	for (prevPointer = &firstOutput; *prevPointer != output; prevPointer = &(*prevPointer)->next) {
		if (*prevPointer == NULL) return -1; // Shouldn't be necessary, but better to safeguard

		if ((*prevPointer)->soloingInArrangementMode) seenAnyOtherSoloing = true;
		outputIndex++;
	}

	*prevPointer = output->next;

	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;

	int bottomYDisplay = -arrangementYScroll;
	int topYDisplay = bottomYDisplay + getNumOutputs();

	bottomYDisplay = getMax(0, bottomYDisplay);
	topYDisplay = getMin(displayHeight - 1, topYDisplay);

	int yDisplay = outputIndex - arrangementYScroll;

	if (yDisplay - bottomYDisplay < topYDisplay - yDisplay) arrangementYScroll--;

	// If the removed Output was soloing, and we haven't yet seen any other soloing Outputs, we'd better check out the rest
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
// Any audio routine calls that happen during the course of this function won't have access to either the old or new Instrument,
// because neither will be in the master list when they happen
void Song::replaceInstrument(Instrument* oldOutput, Instrument* newOutput, bool keepNoteRowsWithMIDIInput) {

	// We don't detach the Instrument's activeClip here anymore. This happens in InstrumentClip::changeInstrument(), called below.
	// If we changed it here, near-future calls to the audio routine could cause new voices to be sounded, with no later unassignment.

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	oldOutput->stopAnyAuditioning(modelStack);

	// Remove the oldInstrument from the list of Instruments
	Output** prevPointer;
	for (prevPointer = &firstOutput; *prevPointer != oldOutput; prevPointer = &(*prevPointer)->next) {}
	newOutput->next = oldOutput->next;
	*prevPointer = oldOutput->next;

	Clip* favourClipForCloningParamManager = NULL;

	// Migrate input MIDI channel / device. Putting this up here before any calls to changeInstrument() is good, because
	// then if a default velocity is set, for the MIDIDevice, that gets grabbed by the Clip's ParamManager during that call.
	if (newOutput->type != INSTRUMENT_TYPE_KIT && oldOutput->type != INSTRUMENT_TYPE_KIT) {
		((MelodicInstrument*)newOutput)->midiInput = ((MelodicInstrument*)oldOutput)->midiInput;
		((MelodicInstrument*)oldOutput)->midiInput.clear();
	}

	outputClipInstanceListIsCurrentlyInvalid = true;

	// Tell all the Clips to change their Instrument.
	// For each Clip in session and arranger for specific Output...
	int numElements = sessionClips.getNumElements();
	bool doingArrangementClips = false;
traverseClips:
	for (int c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = sessionClips.getClipAtIndex(c);
			if (clip->output != oldOutput) continue;
		}
		else {
			ClipInstance* clipInstance = oldOutput->clipInstances.getElement(c);
			if (!clipInstance->clip) continue;
			if (!clipInstance->clip->isArrangementOnlyClip()) continue;
			clip = clipInstance->clip;
		}

		if (oldOutput->type != OUTPUT_TYPE_AUDIO) {
			InstrumentClip* instrumentClip = (InstrumentClip*)clip;

			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

			int error = instrumentClip->changeInstrument(modelStackWithTimelineCounter, newOutput, NULL,
			                                             INSTRUMENT_REMOVAL_NONE,
			                                             (InstrumentClip*)favourClipForCloningParamManager,
			                                             keepNoteRowsWithMIDIInput, true); // Will call audio routine
			// TODO: deal with errors!

			if (newOutput->type == INSTRUMENT_TYPE_KIT) instrumentClip->onKeyboardScreen = false;
		}

		// If this is the first Clip we dealt with, tell all the rest of the Clips to just clone it from this one (if there isn't already a ParamManager backed up in memory for them)
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
	if (output->type == OUTPUT_TYPE_AUDIO || output->type == INSTRUMENT_TYPE_CV
	    || !((Instrument*)output)->editedByUser) {
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
	if (output->type != INSTRUMENT_TYPE_CV && output->type != OUTPUT_TYPE_AUDIO) {
		Instrument* instrument = (Instrument*)output;
		if (!instrument->editedByUser) goto deleteIt;

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
	generalMemoryAllocator.dealloc(toDealloc);
}

void Song::moveInstrumentToHibernationList(Instrument* instrument) {

	removeOutputFromMainList(instrument);

	if (instrument->type == INSTRUMENT_TYPE_MIDI_OUT) {
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
	instrument->activeClip = NULL; // Just be sure!
	instrument->inValidState = false;
}

void Song::removeInstrumentFromHibernationList(Instrument* instrument) {

	// Remove the Instrument from the list of Instruments of this type
	Instrument** prevPointer;
	for (prevPointer = &firstHibernatingInstrument; *prevPointer != instrument;
	     prevPointer = (Instrument**)&(*prevPointer)->next) {
		if (*prevPointer == NULL) return; // Shouldn't be necessary, but better to safeguard
	}

	*prevPointer = (Instrument*)instrument->next;
}

void Song::deleteOrHibernateOutputIfNoClips(Output* output) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	output->pickAnActiveClipIfPossible(modelStack, true, PGM_CHANGE_SEND_ONCE, false);

	// If no other Clips have this Output...
	if (!output->activeClip) {
		deleteOrHibernateOutput(output);
	}
}

void Song::deleteHibernatingInstrumentWithSlot(int instrumentType, char const* name) {

	// Remove the Instrument from the list of Instruments of this type
	Instrument** prevPointer = &firstHibernatingInstrument;
	while (true) {
		Instrument* instrument = *prevPointer;

		if (instrument == NULL) return; // Shouldn't be necessary, but better to safeguard

		if (instrument->type == instrumentType) {

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
		if (output->type == OUTPUT_TYPE_AUDIO) continue;

		Instrument* instrument = (Instrument*)output;
		bool shouldMoveToEmptySlot = false; // Deprecated. See newer branch.
		instrument->beenEdited(shouldMoveToEmptySlot);
	}
}

AudioOutput* Song::getAudioOutputFromName(String* name) {
	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput->type == OUTPUT_TYPE_AUDIO) {
			if (thisOutput->name.equalsCaseIrrespective(name)) {
				return (AudioOutput*)thisOutput;
			}
		}
	}
	return NULL;
}

// You can put name as NULL if it's MIDI or CV
Instrument* Song::getInstrumentFromPresetSlot(int instrumentType, int channel, int channelSuffix, char const* name,
                                              char const* dirPath, bool searchHibernating, bool searchNonHibernating) {

	if (searchNonHibernating) {
		for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
			if (thisOutput->type == instrumentType) {

				bool match;

				if (instrumentType == INSTRUMENT_TYPE_SYNTH || instrumentType == INSTRUMENT_TYPE_KIT) {
					match = !strcasecmp(name, thisOutput->name.get())
					        && !strcasecmp(dirPath, ((Instrument*)thisOutput)->dirPath.get());
				}

				else {
					match = (((NonAudioInstrument*)thisOutput)->channel == channel
					         && (instrumentType == INSTRUMENT_TYPE_CV
					             || ((MIDIInstrument*)thisOutput)->channelSuffix == channelSuffix));
				}

				if (match) return (Instrument*)thisOutput;
			}
		}
	}

	if (searchHibernating) {
		for (Output* thisOutput = firstHibernatingInstrument; thisOutput; thisOutput = thisOutput->next) {
			if (thisOutput->type == instrumentType) {

				bool match;

				if (instrumentType == INSTRUMENT_TYPE_SYNTH || instrumentType == INSTRUMENT_TYPE_KIT) {
					match = !strcasecmp(name, thisOutput->name.get())
					        && !strcasecmp(dirPath, ((Instrument*)thisOutput)->dirPath.get());
				}

				else {
					match = (((NonAudioInstrument*)thisOutput)->channel == channel
					         && ((MIDIInstrument*)thisOutput)->channelSuffix == channelSuffix);
				}

				if (match) return (Instrument*)thisOutput;
			}
		}
	}

	return NULL;
}

int Song::getOutputIndex(Output* output) {
	int count = 0;
	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput == output) return count;
		count++;
	}

	return 0; // fail
}

Output* Song::getOutputFromIndex(int index) {
	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		if (!index) return thisOutput;
		index--;
	}

	return firstOutput; // fail
}

int Song::getNumOutputs() {
	int count = 0;
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

	// For each InstrumentClip in session and arranger
	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);
		if (clip->type != CLIP_TYPE_INSTRUMENT) continue;

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

		InstrumentClip* instrumentClip = (InstrumentClip*)clip;

		// TODO: we probably don't need to call this so often anymore?
		AudioEngine::routineWithClusterLoading(); // -----------------------------------
		AudioEngine::logAction("aaa4.26");
		((Instrument*)instrumentClip->output)->setupPatching(modelStackWithTimelineCounter);
		AudioEngine::logAction("aaa4.27");
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips;
	}
}

// Returns NULL if couldn't find one.
// Supply stealInto to have it delete the "backed up" element, putting the contents into stealInto.
ParamManager* Song::getBackedUpParamManagerForExactClip(ModControllableAudio* modControllable, Clip* clip,
                                                        ParamManager* stealInto) {

	uint32_t keyWords[2];
	keyWords[0] = (uint32_t)modControllable;
	keyWords[1] = (uint32_t)clip;

	int iCorrectClip = backedUpParamManagers.searchMultiWordExact(keyWords);

	if (iCorrectClip == -1) return NULL;

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

	int iAnyClip =
	    backedUpParamManagers.search((uint32_t)modControllable, GREATER_OR_EQUAL); // Search just by first word
	if (iAnyClip >= backedUpParamManagers.getNumElements()) return NULL;
	BackedUpParamManager* elementAnyClip = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(iAnyClip);
	if (elementAnyClip->modControllable != modControllable)
		return NULL; // If nothing with even the correct modControllable at all, get out

	int iCorrectClip;
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
		if (iCorrectClip == -1) goto returnFirstForModControllableEvenIfNotRightClip;
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
// shouldStealExpressionParamsToo should only be true to save expression params from being destructed (e.g. if the Clip is being destructed).
void Song::backUpParamManager(ModControllableAudio* modControllable, Clip* clip, ParamManagerForTimeline* paramManager,
                              bool shouldStealExpressionParamsToo) {

	if (!paramManager->containsAnyMainParamCollections()) return;

	uint32_t keyWords[2];
	keyWords[0] = (uint32_t)modControllable;
	keyWords[1] = (uint32_t)clip;

	int indexToInsertAt;

	int i = backedUpParamManagers.searchMultiWordExact(keyWords, &indexToInsertAt);

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
		int error = backedUpParamManagers.insertAtIndex(i);

		// If RAM error...
		if (error) {

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
	int i = 0;

	while (i < backedUpParamManagers.getNumElements()) {

		BackedUpParamManager* backedUp = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(i);
		if (backedUp->clip == clip) {

			AudioEngine::routineWithClusterLoading(); // -----------------------------------

			// We ideally want to just set the Clip to NULL. We can just do this if the previous element didn't have the same ModControllable
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
				int j = backedUpParamManagers.search((uint32_t)modControllable, GREATER_OR_EQUAL, 0,
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
					int error = backedUpParamManagers.insertAtIndex(j);

					// If RAM error (surely would never happen since we just deleted an element)...
					if (error) {
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

	for (int i = 0; i < backedUpParamManagers.getNumElements(); i++) {

		BackedUpParamManager* backedUp = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(i);

		if (i >= 1) {

			if (backedUp->modControllable < lastModControllable) {
				numericDriver.freezeWithError("E053");
			}

			else if (backedUp->modControllable == lastModControllable) {
				if (backedUp->clip < lastClip) {
					numericDriver.freezeWithError("E054");
				}
				else if (backedUp->clip == lastClip) {
					numericDriver.freezeWithError("E055");
				}
			}
		}

		lastClip = backedUp->clip;
		lastModControllable = backedUp->modControllable;
	}

#endif
}

void Song::deleteBackedUpParamManagersForModControllable(ModControllableAudio* modControllable) {

	int iAnyClip =
	    backedUpParamManagers.search((uint32_t)modControllable, GREATER_OR_EQUAL); // Search by first word only

	while (true) {
		if (iAnyClip >= backedUpParamManagers.getNumElements()) return;
		BackedUpParamManager* elementAnyClip = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(iAnyClip);
		if (elementAnyClip->modControllable != modControllable) return;

		// Destruct paramManager
		elementAnyClip->~BackedUpParamManager();

		// Delete from Vector
		backedUpParamManagers.deleteAtIndex(iAnyClip);
	}
}

// TODO: should we also check whether any arranger clips active and playing in session mode? For next function too...
bool Song::doesOutputHaveActiveClipInSession(Output* output) {

	// For each Clip in session
	for (int c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		if (isClipActive(clip) && clip->output == output) return true;
	}

	return false;
}

// This is for non-audio Instruments only, so no name is relevant
bool Song::doesNonAudioSlotHaveActiveClipInSession(int instrumentType, int slot, int subSlot) {

	// For each Clip in session
	for (int c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		if (isClipActive(clip) && clip->type == CLIP_TYPE_INSTRUMENT) {

			Instrument* instrument = (Instrument*)clip->output;

			if (instrument->type == instrumentType && ((NonAudioInstrument*)instrument)->channel == slot
			    && (instrumentType == INSTRUMENT_TYPE_CV || ((MIDIInstrument*)instrument)->channelSuffix == subSlot)) {
				return true;
			}
		}
	}

	return false;
}

bool Song::doesOutputHaveAnyClips(Output* output) {
	// Check arranger ones first via clipInstances
	for (int i = 0; i < output->clipInstances.getNumElements(); i++) {
		ClipInstance* thisInstance = output->clipInstances.getElement(i);
		if (thisInstance->clip) return true;
	}

	// Then session ones
	for (int c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);
		if (clip->output == output) return true;
	}

	return false;
}

void Song::restoreClipStatesBeforeArrangementPlay() {

	// For each Clip in session
	for (int c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		clip->activeIfNoSolo = clip->wasActiveBefore;
		clip->soloingInSessionMode = false;
	}

	anyClipsSoloing = false;

	// Do not set the Instruments' activeClips. We want them to stay as they were when the song ended
}

// Returns 0 if they're all full
int Song::getLowestSectionWithNoSessionClipForOutput(Output* output) {
	bool* sectionRepresented = (bool*)shortStringBuffer;
	memset(sectionRepresented, 0, MAX_NUM_SECTIONS);

	for (int c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);
		if (clip->output == output && clip->section < MAX_NUM_SECTIONS) {
			sectionRepresented[clip->section] = true;
		}
	}

	for (int s = 0; s < MAX_NUM_SECTIONS; s++) {
		if (!sectionRepresented[s]) return s;
	}

	return 0;
}

void Song::assertActiveness(ModelStackWithTimelineCounter* modelStack, int32_t endInstanceAtTime) {

	Clip* theActiveClip = (Clip*)modelStack->getTimelineCounter();

	bool anyClipStoppedSoloing = false;

	Output* output = theActiveClip->output;

	// Stop any Clips with same Output
	// For each Clip in session and arranger for specific Output
	int numElements = sessionClips.getNumElements();
	bool doingArrangementClips = false;
traverseClips:
	for (int c = 0; c < numElements; c++) {
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

		if (clip != theActiveClip && isClipActive(clip)) {

			if (playbackHandler.isEitherClockActive() && currentSong == this) {
				clip->expectNoFurtherTicks(this, true);
				if (playbackHandler.recording == RECORDING_ARRANGEMENT && endInstanceAtTime != -1) {
					clip->getClipToRecordTo()->endInstance(endInstanceAtTime);
				}
			}

			if (clip->soloingInSessionMode) {
				clip->soloingInSessionMode = false;
				anyClipStoppedSoloing = true;
			}
			else clip->activeIfNoSolo = false;
		}
	}
	if (!doingArrangementClips) {
		doingArrangementClips = true;
		numElements = output->clipInstances.getNumElements();
		goto traverseClips;
	}

	if (anyClipStoppedSoloing) reassessWhetherAnyClipsSoloing();
	output->setActiveClip(modelStack);
}

bool Song::isClipActive(Clip* clip) {
	return clip->soloingInSessionMode || (clip->activeIfNoSolo && !getAnyClipsSoloing());
}

void Song::sendAllMIDIPGMs() {
	for (Output* thisOutput = firstOutput; thisOutput; thisOutput = thisOutput->next) {
		thisOutput->sendMIDIPGM();
	}
}

// This is only called right after Song loaded, so we can assume all Instruments have a NULL activeClip
// It's not possible for this to stop there from being more than zero soloing Clips
void Song::sortOutWhichClipsAreActiveWithoutSendingPGMs(ModelStack* modelStack, int playbackWillStartInArrangerAtPos) {

	AudioEngine::logAction("aaa5.11");

	// If beginning playback in arranger, that's where we figure out which Clips are active - on their Outputs, and generally
	if (playbackWillStartInArrangerAtPos != -1) {
		anyClipsSoloing = false;

		// We still want as many Outputs as possible to have activeClips, even if those Clips are not "active". First, try arranger-only Clips.
		for (Output* output = firstOutput; output; output = output->next) {

			// Don't do any additional searching of session Clips, cos it'd be really inefficient searching all session Clips for each Output
			output->pickAnActiveClipForArrangementPos(modelStack, playbackWillStartInArrangerAtPos,
			                                          PGM_CHANGE_SEND_NEVER);
		}
	}

	// If not about to start playback in arranger, we give the active session Clips first dibs on being active on their Output
	else {

		int count = 0;

		for (int c = 0; c < sessionClips.getNumElements(); c++) {
			Clip* clip = sessionClips.getClipAtIndex(c);

			if (!(count & 3)) {
				AudioEngine::routineWithClusterLoading(); // -----------------------------------
				AudioEngine::logAction("aaa5.114");
			}
			count++;

			// If Clip is supposedly active...
			if (isClipActive(clip)) {

				// If the Instrument already had a Clip, we gotta be inactive...
				if (clip->output->activeClip) {
					if (getAnyClipsSoloing()) clip->soloingInSessionMode = false;
					else clip->activeIfNoSolo = false;
				}

				// Otherwise, it's ours
				else {
					clip->output->setActiveClip(modelStack->addTimelineCounter(clip), PGM_CHANGE_SEND_NEVER);
				}
			}
		}

		AudioEngine::logAction("aaa5.115");

		// We still want as many Outputs as possible to have activeClips, even if those Clips are not "active". First, try arranger-only Clips
		for (Output* output = firstOutput; output; output = output->next) {

			// Don't do any additional searching of session Clips, cos it'd be really inefficient searching all session Clips for each Instrument
			output->pickAnActiveClipIfPossible(modelStack, false, PGM_CHANGE_SEND_NEVER, false);
		}
	}
	AudioEngine::logAction("aaa5.12");

	int count = 0;

	// And finally, go through session Clips again, giving any more to Instruments that can be given
	for (int c = 0; c < sessionClips.getNumElements(); c++) {
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
			if (!clip->output->activeClip) clip->activeIfNoSolo = false;
		}

		if (!clip->output->activeClip) {
			clip->output->setActiveClip(modelStack->addTimelineCounter(clip), PGM_CHANGE_SEND_NEVER);
		}
	}

	AudioEngine::logAction("aaa5.13");

	// Now, we want to ensure (in case of bad song file data) that any Output that doesn't have an activeClip (aka doesn't have ANY Clip)
	// definitely does have a backedUpParamManager. Only for audio Instruments
	for (Output* output = firstOutput; output;) {

		Output* nextOutput = output->next;

		// Also while we're at it, for SoundInstruments (Synths), get them to grab a copy of the arp settings
		if (output->activeClip) {
			if (output->type == INSTRUMENT_TYPE_SYNTH) {
				((SoundInstrument*)output)
				    ->defaultArpSettings.cloneFrom(&((InstrumentClip*)output->activeClip)->arpSettings);
			}
		}

		// Ok, back to the main task - if there's no activeClip...
		else {
			if (output->type == INSTRUMENT_TYPE_SYNTH || output->type == INSTRUMENT_TYPE_KIT) {
				if (!getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)output->toModControllable(),
				                                               NULL)) {
#if ALPHA_OR_BETA_VERSION
					numericDriver.displayPopup("E044");
#endif
					deleteOutputThatIsInMainList(
					    output,
					    false); // Do *not* try to stop any auditioning first. There is none, and doing so would/did cause an E170.
					goto getOut;
				}
			}

			output->setupWithoutActiveClip(modelStack);
		}

#if ALPHA_OR_BETA_VERSION
		// For Kits, ensure that every audio Drum has a ParamManager somewhere
		if (output->type == INSTRUMENT_TYPE_KIT) {
			Kit* kit = (Kit*)output;
			for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {
				if (thisDrum->type == DRUM_TYPE_SOUND) {
					SoundDrum* soundDrum = (SoundDrum*)thisDrum;
					if (!getBackedUpParamManagerPreferablyWithClip(soundDrum, NULL)) { // If no backedUpParamManager...
						if (!findParamManagerForDrum(kit,
						                             soundDrum)) { // If no ParamManager with a NoteRow somewhere...
							numericDriver.freezeWithError("E102");
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
	for (int c = 0; c < arrangementOnlyClips.getNumElements(); c++) {
		Clip* clip = arrangementOnlyClips.getClipAtIndex(c);
		if (clip->activeIfNoSolo) {
			clip->expectNoFurtherTicks(this);
			clip->activeIfNoSolo = false;
		}
	}
}

Clip* Song::getLongestClip(bool includeInactive, bool includeArrangementOnly) {
	Clip* longestClip = NULL;

	// For each Clip in session and arranger
	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);

		if (includeInactive || isClipActive(clip)) {
			if (!longestClip || clip->loopLength > longestClip->loopLength) longestClip = clip;
		}
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips;
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
	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);

		if (clip != excludeClip && isClipActive(clip)) {
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
				if (revertToAnyActiveClipIfNone && !foundClipIsFitting) foundClip = clip;
			}
		}
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips;
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
		hibernatingMIDIInstrument->~Instrument();
		generalMemoryAllocator.dealloc(toDealloc);
		hibernatingMIDIInstrument = NULL;
	}
}

MIDIInstrument* Song::grabHibernatingMIDIInstrument(int channel, int channelSuffix) {
	MIDIInstrument* toReturn = hibernatingMIDIInstrument;
	hibernatingMIDIInstrument = NULL;
	if (toReturn) {
		toReturn->activeClip = NULL; // Not really necessary?
		toReturn->inValidState = false;

		toReturn->channel = channel;
		toReturn->channelSuffix = channelSuffix;
	}
	return toReturn;
}

void Song::stopAllMIDIAndGateNotesPlaying() {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	// For each InstrumentClip in session and arranger
	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);
		if (clip->type != CLIP_TYPE_INSTRUMENT) continue;
		InstrumentClip* instrumentClip = (InstrumentClip*)clip;

		if (isClipActive(instrumentClip) && instrumentClip->output->type != INSTRUMENT_TYPE_SYNTH) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
			    modelStack->addTimelineCounter(instrumentClip);
			instrumentClip->stopAllNotesPlaying(modelStackWithTimelineCounter);
		}
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips;
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
		if (thisOutput->type != INSTRUMENT_TYPE_SYNTH && thisOutput->type != INSTRUMENT_TYPE_KIT) continue;

		AudioEngine::routineWithClusterLoading(); // -----------------------------------

		// If has Clip, that's fine
		if (getClipWithOutput(thisOutput)) {}

		else {
			if (!getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)thisOutput->toModControllable(),
			                                               NULL)) {
				numericDriver.freezeWithError(errorMessageNormal);
			}
		}
	}

	// And hibernating Instruments
	for (Instrument* thisInstrument = firstHibernatingInstrument; thisInstrument;
	     thisInstrument = (Instrument*)thisInstrument->next) {
		if (thisInstrument->type != INSTRUMENT_TYPE_SYNTH && thisInstrument->type != INSTRUMENT_TYPE_KIT) continue;

		AudioEngine::routineWithClusterLoading(); // -----------------------------------

		// If has Clip, it shouldn't!
		if (getClipWithOutput(thisInstrument)) {
			numericDriver.freezeWithError(
			    "E056"); // gtridr got, V4.0.0-beta2. Before I fixed memory corruption issues, so hopefully could just be that.
		}

		else {
			if (!getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)thisInstrument->toModControllable(),
			                                               NULL)) {
				numericDriver.freezeWithError(errorMessageHibernating);
			}
		}
	}
#endif
}

int Song::placeFirstInstancesOfActiveClips(int32_t pos) {

	// For each Clip in session
	for (int c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		if (isClipActive(clip)) {
			int clipInstanceI = clip->output->clipInstances.getNumElements();
			int error = clip->output->clipInstances.insertAtIndex(clipInstanceI);
			if (error) return error;

			ClipInstance* clipInstance = clip->output->clipInstances.getElement(clipInstanceI);
			clipInstance->clip = clip;
			clipInstance->length = clip->loopLength;
			clipInstance->pos = pos;
		}
	}

	return NO_ERROR;
}

// Normally we leave detachClipsToo as false, becuase we need to keep them attached because resumeClipsClonedForArrangementRecording() is about to be called, and needs them attached, and will detach them itself
void Song::endInstancesOfActiveClips(int32_t pos, bool detachClipsToo) {

	// For each Clip in session
	for (int c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		if (isClipActive(clip)) {

			Clip* clipNow = clip->getClipToRecordTo();

			if (detachClipsToo) clipNow->beingRecordedFromClip = NULL;

			int clipInstanceI = clip->output->clipInstances.search(pos + 1, LESS);
			if (clipInstanceI >= 0) {
				ClipInstance* clipInstance = clip->output->clipInstances.getElement(clipInstanceI);
				if (clipInstance->clip == clipNow) {
					int newLength = pos - clipInstance->pos;
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
	for (int c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* originalClip = sessionClips.getClipAtIndex(c);

		Clip* clonedClip = originalClip->output->activeClip;
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
		int i = thisOutput->clipInstances.search(pos, GREATER_OR_EQUAL);

		// We go through deleting the ClipInstances one by one. This is actually quite inefficient, but complicated to improve on because the deletion of the Clips themselves,
		// where there are arrangement-only ones, causes the calling of output->pickAnActiveClipIfPossible. So we have to ensure that extra ClipInstances don't exist at any instant in time,
		// or else it'll look at those to pick the new activeClip, which might not exist anymore.
		for (int j = thisOutput->clipInstances.getNumElements() - 1; j >= i; j--) {
			ClipInstance* clipInstance = thisOutput->clipInstances.getElement(j);
			if (action) action->recordClipInstanceExistenceChange(thisOutput, clipInstance, DELETE);
			Clip* clip = clipInstance->clip;
			thisOutput->clipInstances.deleteAtIndex(j);

			deletingClipInstanceForClip(
			    thisOutput, clip, action,
			    true); // Could be bad that this calls the audio routine before we've actually deleted the ClipInstances...
		}

		// Shorten the previous one if need be
		int numElements = thisOutput->clipInstances.getNumElements();
		if (numElements) {
			ClipInstance* clipInstance = thisOutput->clipInstances.getElement(numElements - 1);
			int maxLength = pos - clipInstance->pos;
			if (clipInstance->length > maxLength) {
				clipInstance->change(action, thisOutput, clipInstance->pos, maxLength, clipInstance->clip);
			}
		}
	}
}

// Will call audio routine!!
// Note: in most cases (when action supplied), will try to pick a new activeClip even if told not to. But this should be ok
void Song::deletingClipInstanceForClip(Output* output, Clip* clip, Action* action, bool shouldPickNewActiveClip) {

	// If clipInstance had a Clip, and it's a (white) arrangement-only Clip, then the whole Clip needs deleting.
	if (clip && clip->isArrangementOnlyClip()) {

		void* memory = NULL;

		bool deletionDone = false;

		if (action) {
			deletionDone = action->recordClipExistenceChange(this, &arrangementOnlyClips, clip, DELETE);
			// That call will call pickAnActiveClipIfPossible() whether we like it or not...
		}

		if (!deletionDone) { // If not enough memory to create undo-history...
			actionLogger.deleteAllLogs();
			// Delete the actual Clip.
			// Will not remove Instrument from Song. Will call audio routine! That's why we deleted the ClipInstance first
			int index = arrangementOnlyClips.getIndexForClip(clip);
			if (index != -1) arrangementOnlyClips.deleteAtIndex(index); // Shouldn't actually ever not be found
			deleteClipObject(clip, false, INSTRUMENT_REMOVAL_NONE);
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
		if (thisOutput->clipInstances.getNumElements()) return true;
	}
	return false;
}

void Song::setParamsInAutomationMode(bool newState) {

	if (paramsInAutomationMode == newState) return;

	paramsInAutomationMode = newState;

	UnpatchedParamSet* unpatchedParams = paramManager.getUnpatchedParamSet();

	// If going automated...
	if (newState) {

		// Back up the unautomated values
		for (int p = 0; p < MAX_NUM_UNPATCHED_PARAMS; p++) {
			unautomatedParamValues[p] = unpatchedParams->params[p].getCurrentValue();
		}
	}

	// Or if going un-automated
	else {

		// Restore the unautomated values, where automation is present
		for (int p = 0; p < MAX_NUM_UNPATCHED_PARAMS; p++) {
			if (unpatchedParams->params[p].isAutomated()) {
				unpatchedParams->params[p].currentValue = unautomatedParamValues[p];
			}
		}
	}

	view.notifyParamAutomationOccurred(&paramManager, true);
}

bool Song::canOldOutputBeReplaced(Clip* clip, int* availabilityRequirement) {
	// If Clip has an "instance" within its Output in arranger, then we can only change the entire Output to a different Output
	if (clip->output->clipHasInstance(clip)) {
		if (availabilityRequirement) *availabilityRequirement = AVAILABILITY_INSTRUMENT_UNUSED;
		return true;
	}

	else {

		if (availabilityRequirement) {
			// If Clip is "active", just make sure we pick an Output that doesn't have a Clip "active" in session
			if (isClipActive(clip)) {
				*availabilityRequirement = AVAILABILITY_INSTRUMENT_AVAILABLE_IN_SESSION;
			}

			// Or if it's not "active", we can give it any Output we like
			else {
				*availabilityRequirement = AVAILABILITY_ANY;
			}
		}

		// We still may as well replace the Output so long as it doesn't have any *other* Clips
		return (getClipWithOutput(clip->output, false, clip) == NULL);
	}
}

void Song::instrumentSwapped(Instrument* newInstrument) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	// If we're playing, in this arrangement mode... (TODO: what if it just switched on while we were loading?)
	if (arrangement.hasPlaybackActive()) {
		int i = newInstrument->clipInstances.search(arrangement.getLivePos() + 1, LESS);

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

			// Assert that thisClip is the active Clip with this Instrument - make any other Clips inactive (activity status could have changed while we were loading...)
			assertActiveness(modelStackWithTimelineCounter);

			if (playbackHandler.isEitherClockActive()) {

				thisClip->setPosForParamManagers(modelStackWithTimelineCounter);
			}
		}
	}

	// If all else failed, just try to get any activeClip possible
	newInstrument->pickAnActiveClipIfPossible(modelStack);
}

Instrument* Song::changeInstrumentType(Instrument* oldInstrument, int newInstrumentType) {

	int16_t newSlot = 0;
	int8_t newSubSlot = -1;

	int oldSlot = newSlot;

	Instrument* newInstrument = NULL;

	// MIDI / CV
	if (newInstrumentType == INSTRUMENT_TYPE_MIDI_OUT || newInstrumentType == INSTRUMENT_TYPE_CV) {

		int numChannels = (newInstrumentType == INSTRUMENT_TYPE_MIDI_OUT) ? 16 : NUM_CV_CHANNELS;

		while (true) {

			if (!getInstrumentFromPresetSlot(newInstrumentType, newSlot, newSubSlot, NULL, NULL, false)) {
				break;
			}

			newSlot = (newSlot + 1) & (numChannels - 1);
			newSubSlot = -1;

			// If we've searched all channels...
			if (newSlot == oldSlot) {
				numericDriver.displayPopup(HAVE_OLED ? "No available channels" : "CANT");
				return NULL;
			}
		}

		if (newInstrumentType == INSTRUMENT_TYPE_MIDI_OUT) {
			newInstrument = grabHibernatingMIDIInstrument(newSlot, newSubSlot);
			if (newInstrument) goto gotAnInstrument;
		}
		newInstrument = storageManager.createNewNonAudioInstrument(newInstrumentType, newSlot, newSubSlot);
		if (!newInstrument) {
			numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
			return NULL;
		}

gotAnInstrument : {}
	}

	// Synth or Kit
	else {
		ReturnOfConfirmPresetOrNextUnlaunchedOne result;
		result.error = Browser::currentDir.set(getInstrumentFolder(newInstrumentType));
		if (result.error) {
displayError:
			numericDriver.displayError(result.error);
			return NULL;
		}

		result = Browser::findAnUnlaunchedPresetIncludingWithinSubfolders(this, newInstrumentType,
		                                                                  AVAILABILITY_INSTRUMENT_UNUSED);
		if (result.error) goto displayError;

		newInstrument = result.fileItem->instrument;
		bool isHibernating = newInstrument && !result.fileItem->instrumentAlreadyInSong;

		if (!newInstrument) {
			String newPresetName;
			result.fileItem->getDisplayNameWithoutExtension(&newPresetName);
			result.error = storageManager.loadInstrumentFromFile(this, NULL, newInstrumentType, false, &newInstrument,
			                                                     &result.fileItem->filePointer, &newPresetName,
			                                                     &Browser::currentDir);
		}

		Browser::emptyFileItems();

		if (result.error) goto displayError;

		if (isHibernating) removeInstrumentFromHibernationList(newInstrument);

#if HAVE_OLED
		OLED::displayWorkingAnimation("Loading");
#else
		numericDriver.displayLoadingAnimation();
#endif

		newInstrument->loadAllAudioFiles(true);

#if HAVE_OLED
		OLED::removeWorkingAnimation();
#endif
	}

#if ALPHA_OR_BETA_VERSION
	numericDriver.setText("A002");
#endif
	replaceInstrument(oldInstrument, newInstrument);
#if ALPHA_OR_BETA_VERSION && !HAVE_OLED
	view.displayOutputName(newInstrument);
#endif

	instrumentSwapped(newInstrument);

	return newInstrument;
}

void Song::setupClipIndexesForSaving() {

	// For each Clip in session and arranger
	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);
		clip->indexForSaving = c;
	}
	if (clipArray != &arrangementOnlyClips) {
		clipArray = &arrangementOnlyClips;
		goto traverseClips;
	}
}

AudioOutput* Song::getFirstAudioOutput() {
	for (Output* output = firstOutput; output; output = output->next) {
		if (output->type == OUTPUT_TYPE_AUDIO) {
			return (AudioOutput*)output;
		}
	}
	return NULL;
}

int8_t defaultAudioOutputInputChannel = -1;

AudioOutput* Song::createNewAudioOutput(Output* replaceOutput) {
	int highestNumber = 0;

	// Find highest number existent so far
	for (Output* output = firstOutput; output; output = output->next) {
		if (output->type == OUTPUT_TYPE_AUDIO) {
			char const* nameChars = output->name.get();
			if (memcasecmp(nameChars, "AUDIO", 5)) continue;

			int nameLength = strlen(nameChars);
			if (nameLength < 1) continue;

			if (!memIsNumericChars(&nameChars[5], nameLength - 5)) continue;

			int number = stringToInt(&nameChars[5]);
			if (number > highestNumber) highestNumber = number;
		}
	}

	String newName;
	int error = newName.set("AUDIO");
	if (error) return NULL;

	error = newName.concatenateInt(highestNumber + 1);
	if (error) return NULL;

	ParamManagerForTimeline newParamManager;
	error = newParamManager.setupUnpatched();
	if (error) return NULL;

	void* outputMemory = generalMemoryAllocator.alloc(sizeof(AudioOutput), NULL, false, true);
	if (!outputMemory) return NULL;

	AudioOutput* newOutput = new (outputMemory) AudioOutput();
	newOutput->name.set(&newName);

	// Set input channel to previously used one. If none selected, see what's in Song
	if (defaultAudioOutputInputChannel == -1) {

		defaultAudioOutputInputChannel = AUDIO_INPUT_CHANNEL_LEFT;
		for (Output* output = firstOutput; output; output = output->next) {
			if (output->type == OUTPUT_TYPE_AUDIO) {
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

Output* Song::getNextAudioOutput(int offset, Output* oldOutput, int availabilityRequirement) {

	Output* newOutput = oldOutput;

	// Forward
	if (offset < 0) { // Reverses direction
		while (true) {
			newOutput = newOutput->next;
			if (!newOutput) newOutput = firstOutput;
			if (newOutput == oldOutput) break;
			if (availabilityRequirement >= AVAILABILITY_INSTRUMENT_AVAILABLE_IN_SESSION
			    && doesOutputHaveActiveClipInSession(newOutput))
				continue;
			if (newOutput->type == OUTPUT_TYPE_AUDIO) break;
		}
	}

	// Backward
	else {
		Output* investigatingOutput = oldOutput;
		while (true) {
			investigatingOutput = investigatingOutput->next;
			if (!investigatingOutput) investigatingOutput = firstOutput;
			if (investigatingOutput == oldOutput) break;
			if (availabilityRequirement >= AVAILABILITY_INSTRUMENT_AVAILABLE_IN_SESSION
			    && doesOutputHaveActiveClipInSession(investigatingOutput))
				continue;
			if (investigatingOutput->type == OUTPUT_TYPE_AUDIO) newOutput = investigatingOutput;
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

// Must supply a char[5] buffer. Or char[30] should be more than adequate for OLED.
void Song::getNoteLengthName(char* text, uint32_t noteLength, bool clarifyPerColumn) {
	int32_t magnitude = -5 - (insideWorldTickMagnitude + insideWorldTickMagnitudeOffsetFromBPM);
	uint32_t level = 3;

	while (level < noteLength) {
		magnitude++;
		level <<= 1;
	}

	getNoteLengthNameFromMagnitude(text, magnitude);
}

Instrument* Song::getNonAudioInstrumentToSwitchTo(int newInstrumentType, int availabilityRequirement, int16_t newSlot,
                                                  int8_t newSubSlot, bool* instrumentWasAlreadyInSong) {
	int numChannels = (newInstrumentType == INSTRUMENT_TYPE_MIDI_OUT) ? 16 : NUM_CV_CHANNELS;
	Instrument* newInstrument;
	int oldSlot = newSlot;

	while (true) {

		newInstrument = getInstrumentFromPresetSlot(newInstrumentType, newSlot, newSubSlot, NULL, NULL,
		                                            false); // This will always be false! Might rework this though

		if (availabilityRequirement == AVAILABILITY_ANY) {
			break;
		}
		else if (availabilityRequirement == AVAILABILITY_INSTRUMENT_AVAILABLE_IN_SESSION) {
			if (!newInstrument || !getClipWithOutput(newInstrument, true)) break;
		}
		else if (availabilityRequirement == AVAILABILITY_INSTRUMENT_UNUSED) {
			if (!newInstrument) break;
		}

		newSlot = (newSlot + 1) & (numChannels - 1);
		newSubSlot = -1;

		// If we've searched all channels...
		if (newSlot == oldSlot) {
			numericDriver.displayPopup(HAVE_OLED ? "No unused channels available" : "CANT");
			return NULL;
		}
	}

	*instrumentWasAlreadyInSong = (newInstrument != NULL);

	// If that didn't work... make a new Instrument to switch to
	if (!newInstrument) {

		if (newInstrumentType == INSTRUMENT_TYPE_MIDI_OUT) {
			newInstrument = grabHibernatingMIDIInstrument(newSlot, newSubSlot);
			if (newInstrument) goto gotAnInstrument;
		}
		newInstrument = storageManager.createNewNonAudioInstrument(newInstrumentType, newSlot, newSubSlot);
		if (!newInstrument) {
			numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
			return NULL;
		}
	}

gotAnInstrument:
	return newInstrument;
}

void Song::removeSessionClip(Clip* clip, int clipIndex, bool forceClipsAboveToMoveVertically) {

	// If this is the current Clip for the ClipView...
	if (currentClip == clip) currentClip = NULL;

	// Must unsolo the Clip before we delete it, in case its play-pos needs to be grabbed for another Clip
	if (clip->soloingInSessionMode) {
		session.unsoloClip(clip);
	}

	// See if any instances in arranger
	bool foundAtLeastOneInstanceInArranger = false;
	Output* output = clip->output;

	for (int i = 0; i < output->clipInstances.getNumElements(); i++) {
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

			if (deletedAnyElements)
				clipInstance = output->clipInstances.getElement(i); // Gotta re-get, since storage has changed

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

	int clipYDisplay = clipIndex - songViewYScroll;
	int bottomYDisplay = -songViewYScroll;
	int topYDisplay = bottomYDisplay + sessionClips.getNumElements() - 1;
	bottomYDisplay = getMax(bottomYDisplay, 0);
	topYDisplay = getMin(topYDisplay, displayHeight - 1);
	int amountOfStuffAbove = topYDisplay - clipYDisplay;
	int amountOfStuffBelow = clipYDisplay - bottomYDisplay;

	removeSessionClipLowLevel(clip, clipIndex);

	// If there was at least one instance, don't properly delete the Clip - just put it in the arranger only. But do stop it playing!
	if (foundAtLeastOneInstanceInArranger) {

		arrangementOnlyClips.insertClipAtIndex((InstrumentClip*)clip, 0);
		clip->section = 255;
	}

	// Otherwise, delete as usual
	else {
		deleteClipObject(clip, false, INSTRUMENT_REMOVAL_DELETE_OR_HIBERNATE_IF_UNUSED);
	}

	if (forceClipsAboveToMoveVertically || amountOfStuffAbove > amountOfStuffBelow) songViewYScroll--;

	AudioEngine::mustUpdateReverbParamsBeforeNextRender =
	    true; // Necessary? Maybe the Instrument would get deleted from the master list?
}

// Please stop the Clip from soloing before calling this
void Song::removeSessionClipLowLevel(Clip* clip, int clipIndex) {

	if (playbackHandler.isEitherClockActive() && currentPlaybackMode == &session && clip->activeIfNoSolo) {
		clip->expectNoFurtherTicks(this);
		clip->activeIfNoSolo = false;
	}

	sessionClips.deleteAtIndex(clipIndex);
}

// originalClipIndex is optional
bool Song::deletePendingOverdubs(Output* onlyWithOutput, int* originalClipIndex,
                                 bool createConsequencesForOtherLinearlyRecordingClips) {

	// You'd kind of think that we'd want to just not bother with this if playback isn't active, but we're not allowed to apply that logic,
	// cos this will get called as playback ends, but after playbackState is set to 0

	// But, we're still allowed to do this check
	if (playbackHandler.isEitherClockActive() && currentPlaybackMode != &session) return false;

	bool anyDeleted = false;

	// For each Clip in session
	for (int c = sessionClips.getNumElements() - 1; c >= 0; c--) {
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

int Song::getYScrollSongViewWithoutPendingOverdubs() {
	int numToSearch = getMin(sessionClips.getNumElements(), songViewYScroll + displayHeight);

	int outputValue = songViewYScroll;

	for (int i = 0; i < numToSearch; i++) {
		Clip* clip = sessionClips.getClipAtIndex(i);
		if (clip->isPendingOverdub) outputValue--;
	}

	return outputValue;
}

Clip* Song::getPendingOverdubWithOutput(Output* output) {

	// For each Clip in session
	for (int c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		if (clip->isPendingOverdub && clip->output == output) return clip;
	}

	return NULL;
}

Clip* Song::getClipWithOutputAboutToBeginLinearRecording(Output* output) {

	// For each Clip in session
	for (int c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		if (clip->output == output && clip->armState && !isClipActive(clip)
		    && clip->wantsToBeginLinearRecording(this)) {
			return clip;
		}
	}

	return NULL;
}

Clip* Song::createPendingNextOverdubBelowClip(Clip* clip, int clipIndex, int newOverdubNature) {

	// No automatic overdubs are allowed during soloing, cos that's just too complicated
	if (anyClipsSoloing) return NULL;

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

		uiNeedsRendering(&sessionView);
	}

	return newClip;
}

bool Song::hasAnyPendingNextOverdubs() {

	// For each Clip in session
	for (int c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

		if (clip->isPendingOverdub) return true;
	}

	return false;
}

void Song::cullAudioClipVoice() {
	AudioClip* bestClip = NULL;
	uint64_t lowestImmunity = 0xFFFFFFFFFFFFFFFF;

	for (Output* output = firstOutput; output; output = output->next) {
		if (output->type == OUTPUT_TYPE_AUDIO) {
			if (output->activeClip) {
				AudioClip* clip = (AudioClip*)output->activeClip;
				if (clip->voiceSample) {
					uint64_t immunity = clip->getCullImmunity();
					lowestImmunity = immunity;
					bestClip = clip;
				}
			}
		}
	}

	if (bestClip) {
		bestClip->unassignVoiceSample();
		Uart::println("audio clip voice culled!");
	}
}

void Song::swapClips(Clip* newClip, Clip* oldClip, int clipIndex) {
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

Clip* Song::replaceInstrumentClipWithAudioClip(Clip* oldClip, int clipIndex) {

	// Allocate memory for audio clip
	void* clipMemory = generalMemoryAllocator.alloc(sizeof(AudioClip), NULL, false, true);
	if (!clipMemory) {
		return NULL;
	}

	// Suss output
	AudioOutput* newOutput = createNewAudioOutput();
	if (!newOutput) {
		generalMemoryAllocator.dealloc(clipMemory);
		return NULL;
	}

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
		for (int c = 0; c < sessionClips.getNumElements(); c++) {
			Clip* clip = sessionClips.getClipAtIndex(c);

			if (clip->type == CLIP_TYPE_AUDIO && clip->armedForRecording) {
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

void Song::changeSwingInterval(int newValue) {

	swingInterval = newValue;

	if (playbackHandler.playbackState & PLAYBACK_CLOCK_INTERNAL_ACTIVE) {

		int leftShift = 10 - swingInterval;
		leftShift = getMax(leftShift, 0);
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
	return increaseMagnitude(24, insideWorldTickMagnitude + insideWorldTickMagnitudeOffsetFromBPM);
}

uint32_t Song::getBarLength() {
	return increaseMagnitude(96, insideWorldTickMagnitude + insideWorldTickMagnitudeOffsetFromBPM);
}

// ----- PlayPositionCounter implementation -------

bool Song::isPlayingAutomationNow() {
	return (currentPlaybackMode == &arrangement || playbackHandler.recording == RECORDING_ARRANGEMENT);
}

bool Song::backtrackingCouldLoopBackToEnd() {
	return false;
}

int32_t Song::getPosAtWhichPlaybackWillCut(ModelStackWithTimelineCounter const* modelStack) {
	return 2147483647;
}

void Song::getActiveModControllable(ModelStackWithTimelineCounter* modelStack) {
	if (DELUGE_MODEL == DELUGE_MODEL_40_PAD || affectEntire) {
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

uint32_t Song::getLivePos() {

	if (playbackHandler.recording == RECORDING_ARRANGEMENT) return playbackHandler.getActualArrangementRecordPos();
	else return arrangement.getLivePos();
}

// I think I created this function to be called during the actioning of a swung tick, when we know that no further swung ticks have passed since the last actioned one
int32_t Song::getLastProcessedPos() {

	if (playbackHandler.recording == RECORDING_ARRANGEMENT)
		return playbackHandler.getArrangementRecordPosAtLastActionedSwungTick();
	else return arrangement.lastProcessedPos;
}

int32_t Song::getLoopLength() {
	return 2147483647;
}

TimelineCounter* Song::getTimelineCounterToRecordTo() {
	return this;
}

void Song::setDefaultVelocityForAllInstruments(uint8_t newDefaultVelocity) {
	for (Output* output = firstOutput; output; output = output->next) {
		if (output->type != OUTPUT_TYPE_AUDIO) {
			((Instrument*)output)->defaultVelocity = newDefaultVelocity;
		}
	}

	for (Instrument* instrument = firstHibernatingInstrument; instrument; instrument = (Instrument*)instrument->next) {
		instrument->defaultVelocity = newDefaultVelocity;
	}
}

int Song::convertSyncLevelFromFileValueToInternalValue(int fileValue) {

	// The file value is relative to insideWorldTickMagnitude etc, though if insideWorldTickMagnitude is 1 (which used to be the default), it comes out as the same value anyway (kind of a side point)

	if (fileValue == 0) return 0; // 0 means "off"
	int internalValue = fileValue + 1 - (insideWorldTickMagnitude + insideWorldTickMagnitudeOffsetFromBPM);
	if (internalValue < 1) internalValue = 1;
	else if (internalValue > 9) internalValue = 9;

	return internalValue;
}

int Song::convertSyncLevelFromInternalValueToFileValue(int internalValue) {

	if (internalValue == 0) return 0; // 0 means "off"
	int fileValue = internalValue - 1 + (insideWorldTickMagnitude + insideWorldTickMagnitudeOffsetFromBPM);
	if (fileValue < 1) fileValue = 1;

	return fileValue;
}

void Song::midiDeviceBendRangeUpdatedViaMessage(ModelStack* modelStack, MIDIDevice* device, int channelOrZone,
                                                int whichBendRange, int bendSemitones) {

	// Go through all Instruments...
	for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {
		thisOutput->offerBendRangeUpdate(modelStack, device, channelOrZone, whichBendRange, bendSemitones);
	}
}

int Song::addInstrumentsToFileItems(int instrumentType) {
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

		if (thisOutput->type != instrumentType) continue;

		Instrument* thisInstrument = (Instrument*)thisOutput;

		// If different path, it's not relevant.
		if (!thisInstrument->dirPath.equals(&Browser::currentDir)) continue;

		FileItem* thisItem = Browser::getNewFileItem();
		if (!thisItem) {
			return ERROR_INSUFFICIENT_RAM;
		}

		int error = thisItem->setupWithInstrument(thisInstrument, doingHibernatingOnes);
		if (error) return error;
	}

	if (!doingHibernatingOnes) goto doHibernatingInstruments;

	return NO_ERROR;
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

/*
	// For each Clip in session and arranger
	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);

	}
	if (clipArray != &arrangementOnlyClips) { clipArray = &arrangementOnlyClips; goto traverseClips; }



	// For each InstrumentClip in session and arranger
	ClipArray* clipArray = &sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);
		if (clip->type != CLIP_TYPE_INSTRUMENT) continue;
		Clip* instrumentClip = (Clip*)clip;

	}
	if (clipArray != &arrangementOnlyClips) { clipArray = &arrangementOnlyClips; goto traverseClips; }



	// For each Clip in session
	for (int c = 0; c < sessionClips.getNumElements(); c++) {
		Clip* clip = sessionClips.getClipAtIndex(c);

	}


	// For each Clip in arrangement
	for (int c = 0; c < arrangementOnlyClips.getNumElements(); c++) {
		Clip* clip = arrangementOnlyClips.getClipAtIndex(c);

	}



	// For each Clip in session and arranger for specific Output
	int numElements = sessionClips.getNumElements();
	bool doingArrangementClips = false;
traverseClips:
	for (int c = 0; c < numElements; c++) {
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
	if (!doingArrangementClips) { doingArrangementClips = true; numElements = output->clipInstances.getNumElements(); goto traverseClips; }





	// For each Clip in session and arranger for specific Output - but if currentlySwappingInstrument, use master list for arranger Clips
	ClipArray* clipArray = &sessionClips;
	bool doingClipsProvidedByOutput = false;
decideNumElements:
	int numElements = clipArray->getNumElements();
traverseClips:
	for (int c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingClipsProvidedByOutput) {
			clip = clipArray->getClipAtIndex(c);
			if (clip->output != output) continue;
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
		else { doingClipsProvidedByOutput = true; numElements = output->clipInstances.getNumElements(); goto traverseClips; }
	}
 */

//    for (Output* output = firstOutput; output; output = output->next) {
