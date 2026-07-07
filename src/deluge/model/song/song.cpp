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
#include "deluge-project/serialization.h"
#include "dsp/reverb/reverb.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
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
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/sample/sample_recorder.h"
#include "model/scale/preset_scales.h"
#include "model/scale/scale_change.h"
#include "model/scale/utils.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/clip_iterators.h"
#include "model/voice/voice_sample.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/sound/sound_instrument.h"
#include "processing/stem_export/stem_export.h"
#include "storage/flash_storage.h"
#include "storage/storage_manager.h"
#include "util/etl_string.h"
#include "util/lookuptables/lookuptables.h"
#include "util/string.h"
#include "util/try.h"
#include <cstring>
#include <hid/buttons.h>
#include <iterator>
#include <new>
#include <stdint.h>

extern "C" {}

namespace params = deluge::modulation::params;

/// Do not call in static/global constructors, song won't exist yet.
/// Tolerates a null currentSong: during deleteOldSongBeforeLoadingNew() the global is
/// nulled before ~Song() tears down its clips, and ~Clip() calls back in here. On the
/// MCU the null deref read mapped-but-garbage memory (branch harmlessly not taken); on
/// the host 0x0 is unmapped and faults, so return nullptr for the same observable result.
Clip* getCurrentClip() {
	return currentSong ? currentSong->getCurrentClip() : nullptr;
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
using deluge::project::SessionMacroKind;

Song::Song() {
	outputClipInstanceListIsCurrentlyInvalid = false;
	project.transport.inside_world_tick_magnitude = FlashStorage::defaultMagnitude;
	project.transport.inside_world_tick_magnitude_offset_from_bpm = 0;
	project.transport.sync_scaling_clip = nullptr;
	runtimeView.currentClip = nullptr;
	project.metadata.slot = deluge::project::kNoSlot;
	project.metadata.sub_slot = -1;

	persistentView.xScroll[NAVIGATION_CLIP] = 0;
	persistentView.xScroll[NAVIGATION_ARRANGEMENT] = 0;
	persistentView.xScrollForReturnToSongView = 0;

	persistentView.xZoom[NAVIGATION_CLIP] =
	    increaseMagnitude(kDefaultClipLength, project.transport.inside_world_tick_magnitude - kDisplayWidthMagnitude);
	persistentView.xZoom[NAVIGATION_ARRANGEMENT] = kDefaultArrangerZoom
	                                               << project.transport.inside_world_tick_magnitude;
	persistentView.xZoomForReturnToSongView = persistentView.xZoom[NAVIGATION_CLIP];

	persistentView.tripletsOn = false;

	persistentView.affectEntire = false;

	runtimeView.fillModeActive = false;

	project.scale.key.modeNotes = presetScaleNotes[MAJOR_SCALE];
	project.scale.disabled_preset_scales = FlashStorage::defaultDisabledPresetScales;

	project.groove.swing_amount = 0;

	project.groove.swing_interval = FlashStorage::defaultSwingInterval;

	persistentView.songViewYScroll = 1 - kDisplayHeight;
	persistentView.arrangementYScroll = -kDisplayHeight;

	anyClipsSoloing = false;
	anyOutputsSoloingInArrangement = false;

	project.media.first_output = nullptr;
	project.media.first_hibernating_instrument = nullptr;
	project.media.hibernating_midi_instrument = nullptr;

	persistentView.lastClipInstanceEnteredStartPos = -1;
	persistentView.arrangerAutoScrollModeActive = false;

	runtimeView.paramsInAutomationMode = false;

	// Setup reverb temp variables
	project.reverb.room_size = (float)30 / 50;
	project.reverb.damping = (float)36 / 50;
	project.reverb.hpf = 0;
	project.reverb.lpf = (float)50 / 50;
	project.reverb.width = 1;
	project.reverb.pan = 0;
	project.reverb.sidechain_volume = getParamFromUserValue(params::STATIC_SIDECHAIN_VOLUME, -1);
	project.reverb.sidechain_shape = -601295438;
	project.reverb.sidechain_sync = SYNC_LEVEL_8TH;
	model = deluge::dsp::Reverb::Model::MUTABLE;

	// setup base compressor gain to match 1.0
	project.global_fx.global_effectable.compressor.setBaseGain(0.85);

	// initialize automation arranger view variables
	lastSelectedParamID = params::kNoParamID;
	lastSelectedParamKind = params::Kind::NONE;
	lastSelectedParamShortcutX = kNoSelection;
	lastSelectedParamShortcutY = kNoSelection;
	lastSelectedParamArrayPosition = 0;
	// end initialize of automation arranger view variables

	masterTransposeInterval = 0;

	project.metadata.dir_path = "SONGS";

	thresholdRecordingMode = FlashStorage::defaultThresholdRecordingMode;
}

Song::~Song() {

	// Delete existing Clips, if any
	for (int32_t c = 0; c < std::ssize(project.clips.session_clips); c++) {
		if (!(c & 31)) { // Rohan: Exact number here not fine-tuned
			AudioEngine::routineWithClusterLoading();
		}

		Clip* clip = project.clips.session_clips[c];
		deleteClipObject(clip, true, InstrumentRemoval::NONE);
	}

	for (int32_t c = 0; c < std::ssize(project.clips.arrangement_only_clips); c++) {
		if (!(c & 31)) { // Rohan: Exact number here not fine-tuned
			AudioEngine::routineWithClusterLoading();
		}

		Clip* clip = project.clips.arrangement_only_clips[c];
		deleteClipObject(clip, true, InstrumentRemoval::NONE);
	}

	AudioEngine::logAction("s4");
	AudioEngine::routineWithClusterLoading();

	// Free all ParamManagers which are backed up
	deleteAllBackedUpParamManagers();

	deleteAllOutputs(&project.media.first_output);
	deleteAllOutputs((Output**)&project.media.first_hibernating_instrument);

	deleteHibernatingMIDIInstrument();
}

#include "gui/menu_item/integer_range.h"
#include "gui/menu_item/key_range.h"
#include "libdeluge/system.h"
extern gui::menu_item::IntegerRange defaultTempoMenu;
extern gui::menu_item::IntegerRange defaultSwingAmountMenu;
extern gui::menu_item::KeyRange defaultKeyMenu;

Clip* Song::getCurrentClip() {
	return runtimeView.currentClip;
}

// It's not super clear to me why this isn't done as part of the constructor.
void Song::setupDefault() {
	runtimeView.inClipMinderViewOnLoad = true;

	seedRandom();

	setBPM(defaultTempoMenu.getRandomValueInRange(), false);
	project.groove.swing_amount = defaultSwingAmountMenu.getRandomValueInRange() - 50;
	project.scale.key.rootNote = defaultKeyMenu.getRandomValueInRange();

	// Do scale
	project.scale.disabled_preset_scales = FlashStorage::defaultDisabledPresetScales;
	ensureNotAllPresetScalesDisabled(project.scale.disabled_preset_scales);
	Scale whichScale = flashStorageCodeToScale(FlashStorage::defaultScale);
	if (whichScale == RANDOM_SCALE) {
		do {
			whichScale = static_cast<Scale>(random(LAST_PRESET_SCALE));
		} while (project.scale.disabled_preset_scales[whichScale]);
	}
	else if (whichScale > LAST_PRESET_SCALE) {
		// Both USER_SCALE and NO_SCALE, and any bogus values. Same handling for different reasons.
		//
		// - USER_SCALE isn't yet stored, so though you can set it as default, we can't
		//   activate it.
		//
		// - for NO_SCALE *song* still needs a scale.
		//
		// - bogus values just need ignoring
		whichScale = MAJOR_SCALE;
	}

	// Whichever scale we ended up with, make sure it's enabled.
	project.scale.disabled_preset_scales[whichScale] = false;

	project.scale.key.modeNotes = presetScaleNotes[whichScale];

	thresholdRecordingMode = FlashStorage::defaultThresholdRecordingMode;
}

void Song::deleteAllOutputs(Output** prevPointer) {

	while (*prevPointer) {
		AudioEngine::logAction("s6");
		AudioEngine::routineWithClusterLoading();
		Output* toDelete = *prevPointer;
		*prevPointer = toDelete->next;

		void* toDealloc = dynamic_cast<void*>(toDelete);
		toDelete->~Output();
		delugeDealloc(toDealloc);
	}
}

void Song::deleteAllBackedUpParamManagers() {
	while (!project.param_backup.map.empty()) {
		AudioEngine::routineWithClusterLoading();
		project.param_backup.map.erase(project.param_backup.map.begin());
	}
}

void Song::deleteAllBackedUpParamManagersWithClips() {
	for (auto it = project.param_backup.map.begin(); it != project.param_backup.map.end();) {
		if (it->first.second != nullptr) {
			AudioEngine::routineWithClusterLoading();
			it = project.param_backup.map.erase(it);
		}
		else {
			++it;
		}
	}
}

bool Song::mayDoubleTempo() {
	return ((project.transport.samples_per_timer_tick_big >> 33) > kMinTimePerTimerTick);
}

// Returns true if a Clip created
bool Song::ensureAtLeastOneSessionClip() {
	// If no Clips added, make just one blank one - we can't have none!
	if (!project.clips.session_clips.empty()) {
		return false;
	}

	void* memory = deluge::memory::alloc_fast(sizeof(InstrumentClip));
	InstrumentClip* firstClip = new (memory) InstrumentClip(this);

	(void)project.clips.session_clips.insertClipAt(firstClip,
	                                               0); // OOM impossible-in-practice this early; ignored as before

	firstClip->loopLength = kDefaultClipLength << project.transport.inside_world_tick_magnitude;

	ParamManager newParamManager; // Deliberate don't set up.

	Instrument* newInstrument = nullptr;

	std::expected<FileItem*, Error> result;

	Error error;
	error = StorageManager::initSD(); // Still want this here?
	if (error != Error::NONE) {
		goto couldntLoad;
	}

	Browser::currentDir = "SYNTHS";

	result = loadInstrumentPresetUI.findAnUnlaunchedPresetIncludingWithinSubfolders(nullptr, OutputType::SYNTH,
	                                                                                Availability::ANY);
	if (result) {
		std::string newPresetName = result.value()->getDisplayNameWithoutExtension();
		error =
		    StorageManager::loadInstrumentFromFile(this, firstClip, OutputType::SYNTH, false, &newInstrument,
		                                           &result.value()->filePointer, &newPresetName, &Browser::currentDir);

		Browser::emptyFileItems();
		if (error != Error::NONE) {
			goto couldntLoad;
		}
	}
	else {
couldntLoad:
		newInstrument = StorageManager::createNewInstrument(OutputType::SYNTH, &newParamManager);

		// TODO: Clean this up and get rid of infinite loop traps (proper error recovery)

		// If that failed (really unlikely) we're really screwed
		if (!newInstrument) {
			display->displayError(Error::INSUFFICIENT_RAM);
			while (1) {}
		}

		newInstrument->dirPath = "SYNTHS";

		newInstrument->name = "0";

		((SoundInstrument*)newInstrument)->setupAsDefaultSynth(&newParamManager);
		display->displayError(error); // E.g. show the CARD error.
	}

	newInstrument->loadAllAudioFiles(true);

	firstClip->setAudioInstrument(newInstrument, this, true, &newParamManager);
	// TODO: error checking?
	addOutput(newInstrument);

	runtimeView.currentClip = firstClip;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(firstClip);

	if (playbackHandler.isEitherClockActive() && currentPlaybackMode == &session) {
		session.reSyncClip(modelStackWithTimelineCounter, true);
	}

	newInstrument->setActiveClip(modelStackWithTimelineCounter);

	return true;
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
		MusicalKey oldKey = project.scale.key;
		uint8_t noteCount = project.scale.key.modeNotes.count();

		if (offset < 0) {
			octaves = ((offset + 1) / noteCount) - 1;
			rootIndex = ((offset + 1) % noteCount) + noteCount - 1;
		}
		else {
			octaves = (offset / noteCount);
			rootIndex = offset % noteCount;
		}
		int8_t newModeRoot = project.scale.key.modeNotes[rootIndex];

		semitones = 12 * octaves + newModeRoot;
		rotateMusicalMode(offset);

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

		MusicalKey newKey = project.scale.key;
		newKey.rootNote += semitones;

		ClipArray* clipArray = &project.clips.session_clips;
		for (uint8_t iClipTypes = 0; iClipTypes < 2; iClipTypes++) {
			if (iClipTypes == 1) {
				clipArray = &project.clips.arrangement_only_clips;
			}
			for (int32_t c = 0; c < std::ssize(*clipArray); c++) {
				Clip* clip = (*clipArray)[c];
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
					    && ((NonAudioInstrument*)clip->output)->getChannel() == MIDI_CHANNEL_TRANSPOSE) {
						// Must not transpose MIDI clips that are routed to transpose, ie note rows
						// stay exactly the same.
						// Just have to scroll the clip so that the change in song root note
						// does not visually move the notes on the grid.
						yNoteOnBottomRow = oldKey.getYNoteFromYVisual(instrumentClip->yScroll, true);
						instrumentClip->yScroll = newKey.getYVisualFromYNote(yNoteOnBottomRow, true);
					}
					else {
						instrumentClip->transpose(semitones, modelStackWithTimelineCounter);
					}
				}
			}
		}
		project.scale.key = newKey;
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

	project.scale.key.rootNote += interval;
}

bool Song::anyScaleModeClips() {
	for (InstrumentClip* instrumentClip : InstrumentClips::everywhere(this)) {
		if (instrumentClip->isScaleModeClip()) {
			return true;
		}
	}
	return false;
}

NoteSet Song::notesInScaleModeClips() {
	NoteSet notes;
	for (InstrumentClip* instrumentClip : InstrumentClips::everywhere(this)) {
		if (instrumentClip->isScaleModeClip()) {
			instrumentClip->seeWhatNotesWithinOctaveArePresent(notes, project.scale.key);
		}
	}
	return notes;
}

void Song::setRootNote(int32_t newRootNote, InstrumentClip* clipToAvoidAdjustingScrollFor) {
	// Setting the new root affects interpretation of existing notes done by notesInScaleModeClips(),
	// so we need to do it before anything else.
	int32_t oldRootNote = project.scale.key.rootNote;
	project.scale.key.rootNote = newRootNote;

	int32_t oldNumModeNotes = project.scale.key.modeNotes.count();
	NoteSet notesWithinOctavePresent = notesInScaleModeClips();

	// We don't want to reuse "OTHER SCALE", we want the Deluge to guess a new scale.
	// Check if the previously set scale could fit the notes present in the clips
	// If so, we need no check at all, we can directly go back to previous scale safely
	bool previousScaleFits = project.scale.key.getCurrentScale() < NUM_PRESET_SCALES
	                         && notesWithinOctavePresent.isSubsetOf(project.scale.key.modeNotes);
	if (!previousScaleFits) {
		project.scale.key.modeNotes = notesWithinOctavePresent.toImpliedScale();
		// If this is not a preset, save as the current user scale.
		if (isUserScale(project.scale.key.modeNotes)) {
			project.scale.user_scale_notes = project.scale.key.modeNotes;
		}
	}

	// Adjust scroll for Clips with the scale. Crudely - not as high quality as happens for the Clip being processed
	// in enterScaleMode();
	int32_t numMoreNotes = (int32_t)project.scale.key.modeNotes.count() - oldNumModeNotes;

	// Compensation for the change in root note itself
	int32_t rootNoteChange = project.scale.key.rootNote - oldRootNote;
	int32_t rootNoteChangeEffect =
	    rootNoteChange * (12 - project.scale.key.modeNotes.count())
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
	uint8_t noteCount = project.scale.key.modeNotes.count();
	ScaleChange changes;
	changes.source = project.scale.key.modeNotes;

	int8_t steps = (change % noteCount + noteCount) % noteCount;
	newRoot = project.scale.key.modeNotes[steps];
	for (int8_t i = 0; i < noteCount; i++) {
		changes[i] = (project.scale.key.modeNotes[(i + steps) % noteCount] - newRoot) - project.scale.key.modeNotes[i];
		if (i >= noteCount - steps) {
			changes[i] += 12;
		}
	}

	changes.target = changes.applyTo(changes.source);
	replaceMusicalMode(changes, false);
}

/* Changes the musical mode of all scale mode clips, by
   supplying an array of deltas in semitones */
void Song::replaceMusicalMode(const ScaleChange& changes, bool affectMIDITranspose) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	for (InstrumentClip* instrumentClip : InstrumentClips::everywhere(this)) {
		if (!affectMIDITranspose && instrumentClip->output->type == OutputType::MIDI_OUT
		    && ((NonAudioInstrument*)instrumentClip->output)->getChannel() == MIDI_CHANNEL_TRANSPOSE) {
			// Must not transpose MIDI clips that are routed to transpose.
			continue;
		}
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(instrumentClip);

		instrumentClip->replaceMusicalMode(changes, modelStackWithTimelineCounter);
	}

	project.scale.key.modeNotes = changes.applyTo(project.scale.key.modeNotes);
}

// Flattens or sharpens a given note-within-octave in the current scale
void Song::changeMusicalMode(uint8_t yVisualWithinOctave, int8_t change) {

	ScaleChange changes;
	changes.source = project.scale.key.modeNotes;
	changes[yVisualWithinOctave] += change;
	changes.target = changes.applyTo(changes.source);

	replaceMusicalMode(changes, true);
}

bool Song::isYNoteAllowed(int32_t yNote, bool inKeyMode) {
	if (!inKeyMode) {
		return true;
	}
	return project.scale.key.modeNotes.has(project.scale.key.intervalOf(yNote));
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

	for (int32_t l = 0; l < std::ssize(project.clips.session_clips); l++) {
		Clip* clip = project.clips.session_clips[l];
		if (clip->section == section && !isClipActive(clip)) {
			return false;
		}
	}

	return true;
}

uint32_t Song::getInputTickScale() {
	if (!project.transport.sync_scaling_clip) {
		return 3;
	}
	uint32_t inputTickScale = project.transport.sync_scaling_clip->loopLength;
	while (!(inputTickScale & 1)) {
		inputTickScale >>= 1;
	}
	return inputTickScale;
}

Clip* Song::getSyncScalingClip() {
	return project.transport.sync_scaling_clip;
}

void Song::setInputTickScaleClip(Clip* clip) {
	uint32_t oldScale = getInputTickScale();
	project.transport.sync_scaling_clip = clip;
	inputTickScalePotentiallyJustChanged(oldScale);
}

void Song::inputTickScalePotentiallyJustChanged(uint32_t oldScale) {
	uint32_t newScale = getInputTickScale();

	// Chances are we'll have to change the input tick magnitude to account for the magnitudinal difference between
	// say a 1-based and 9-based time
	if ((float)newScale * 1.41 < oldScale) {
		do {
			newScale *= 2;
			project.transport.inside_world_tick_magnitude++;
			// insideWorldTickMagnitudeOffsetFromBPM--;
		} while ((float)newScale * 1.41 < oldScale);
	}

	else {
		while ((float)oldScale * 1.41 < newScale) {
			oldScale *= 2;
			project.transport.inside_world_tick_magnitude--;
			// insideWorldTickMagnitudeOffsetFromBPM++;
		}
	}

	// We then do a very similar process again, to calculate insideWorldTickMagnitudeOffsetFromBPM in such a way
	// that, say, 8th-notes will always appear about the same length to the user

	oldScale = 3;
	newScale = getInputTickScale();
	project.transport.inside_world_tick_magnitude_offset_from_bpm = 0;

	if ((float)newScale * 1.41 < oldScale) {
		do {
			newScale *= 2;
			// insideWorldTickMagnitude++;
			project.transport.inside_world_tick_magnitude_offset_from_bpm--;
		} while ((float)newScale * 1.41 < oldScale);
	}

	else {
		while ((float)oldScale * 1.41 < newScale) {
			oldScale *= 2;
			// insideWorldTickMagnitude--;
			project.transport.inside_world_tick_magnitude_offset_from_bpm++;
		}
	}
}

// This is a little bit of an ugly hack - normally this is true, but we'll briefly set it to false while doing that
// "revert" that we do when (re)lengthening a Clip
bool allowResyncingDuringClipLengthChange = true;

void Song::changeFillMode(bool on) {
	runtimeView.fillModeActive = on;

	UI* root_ui = getRootUI();
	if (root_ui == &instrumentClipView) {
		// we peek fill notes when fill is held so need to re render rows
		uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
	}

	if ((runtimeFeatureSettings.get(RuntimeFeatureSettingType::SyncScalingAction)
	     == RuntimeFeatureStateSyncScalingAction::Fill)) {
		indicator_leds::setLedState(IndicatorLED::SYNC_SCALING, on);
	}
}

void Song::loadNextSong() {
	loadSongUI.queueLoadNextSongIfAvailable(1);
}

// If action is NULL, that means this is being called as part of an undo, so don't do any extra stuff.
// Currently mayReSyncClip is only set to false in a call that happens when we've just finished recording that Clip.
void Song::setClipLength(Clip* clip, uint32_t newLength, Action* action, bool mayReSyncClip) {
	uint32_t oldLength = clip->loopLength;

	if (clip == project.transport.sync_scaling_clip) {
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

	if (clip == project.transport.sync_scaling_clip) {
		inputTickScalePotentiallyJustChanged(oldScale);
	}

	clip->output->clipLengthChanged(clip, oldLength);

	if (playbackHandler.isEitherClockActive() && isClipActive(clip)) {
		currentPlaybackMode->reSyncClip(modelStack);
	}
}

Clip* Song::getClipWithOutput(Output* output, bool mustBeActive, Clip* excludeClip) {

	// For each clip in session and arranger for specific Output
	int32_t numElements = std::ssize(project.clips.session_clips);
	bool doingArrangementClips = false;
	// TODO: This should be rewritten with AllClips as well, but it's less obvious
	// so leaving or later.
traverseClips:
	for (int32_t c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = project.clips.session_clips[c];
			if (clip->output != output) {
				continue;
			}
		}
		else {
			ClipInstance* clipInstance = &output->clipInstances[c];
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
		numElements = std::ssize(output->clipInstances);
		goto traverseClips;
	}

	return nullptr;
}

Clip* Song::getSessionClipWithOutput(Output* output, int32_t requireSection, Clip* excludeClip, int32_t* clipIndex,
                                     bool excludePendingOverdubs) {

	// For each clip in session
	for (int32_t c = 0; c < std::ssize(project.clips.session_clips); c++) {
		Clip* clip = project.clips.session_clips[c];

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

	return nullptr;
}

Clip* Song::getNextSessionClipWithOutput(int32_t offset, Output* output, Clip* prevClip) {

	int32_t oldIndex = -1;
	if (prevClip) {
		// retrieve clip index
		oldIndex = project.clips.session_clips.getIndexForClip(prevClip);
	}
	// if we didn't retrieve a clip index, this is an arranger-only clip
	if (oldIndex == -1) {
		// turning select knob left
		if (offset < 0) {
			// use the highest available index
			// NOTE: resulting index is one-based
			oldIndex = std::ssize(project.clips.session_clips);
		}
	}

	int32_t newIndex = oldIndex;
	while (true) {
		// iterate index according to select knob direction
		newIndex += offset;
		// index out of bounds on either side returns NULL
		if (newIndex == -1 || newIndex == std::ssize(project.clips.session_clips)) {
			return nullptr;
		}
		// retrieve clip and return
		Clip* clip = project.clips.session_clips[newIndex];
		if (clip->output == output) {
			return clip;
		}
	}
}

void Song::writeTemplateSong(const char* templatePath) {
	project.metadata.name = "DEFAULT";
	Error error = StorageManager::createXMLFile(templatePath, smSerializer, false, false);
	if (error != Error::NONE) {
		return;
	}
	deluge::project::saveSong(*this, GetSerializer());
	GetSerializer().closeFileAfterWriting(templatePath, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<song\n",
	                                      "\n</song>\n");
}

void Song::writeToFile(Serializer& writer) {

	setupClipIndexesForSaving();
	writer.reset();
	if (!writeJsonFlag) {
		writer.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	}
	writer.writeOpeningTagBeginning("song", true, false);

	writer.writeFirmwareVersion();
	writer.writeEarliestCompatibleFirmwareVersion("4.1.0-alpha");

	writer.writeAttribute("previewNumPads", 144);
	writer.insertCommaIfNeeded();
	writer.write("\n");
	writer.printIndents();

	writer.writeTagNameAndSeperator("preview");
	writer.write("\"");

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

	if (persistentView.lastClipInstanceEnteredStartPos != -1) {
		writer.writeAttribute("currentTrackInstanceArrangementPos", persistentView.lastClipInstanceEnteredStartPos);

weAreInArrangementEditorOrInClipInstance:
		writer.writeAttribute("xScrollSongView", persistentView.xScrollForReturnToSongView);
		writer.writeAttribute("xZoomSongView", persistentView.xZoomForReturnToSongView);
	}

	writer.writeAttribute("arrangementAutoScrollOn", persistentView.arrangerAutoScrollModeActive);

	writer.writeAttribute("xScroll", persistentView.xScroll[NAVIGATION_CLIP]);
	writer.writeAttribute("xZoom", persistentView.xZoom[NAVIGATION_CLIP]);
	writer.writeAttribute("yScrollSongView", persistentView.songViewYScroll);
	writer.writeAttribute("yScrollArrangementView", persistentView.arrangementYScroll);
	writer.writeAttribute("xScrollArrangementView", persistentView.xScroll[NAVIGATION_ARRANGEMENT]);
	writer.writeAttribute("xZoomArrangementView", persistentView.xZoom[NAVIGATION_ARRANGEMENT]);
	deluge::project::serialize(project.transport, writer);
	deluge::project::serializeScaleKey(project.scale, writer);
	deluge::project::serialize(project.groove, writer, this);

	if (persistentView.tripletsOn) {
		writer.writeAttribute("tripletsLevel", persistentView.tripletsLevel);
	}

	writer.writeAttribute("affectEntire", persistentView.affectEntire);
	writer.writeAttribute("activeModFunction", project.global_fx.global_effectable.modKnobMode);

	if (lastSelectedParamID != params::kNoParamID) {
		writer.writeAttribute("lastSelectedParamID", lastSelectedParamID);
		writer.writeAttribute("lastSelectedParamKind", util::to_underlying(lastSelectedParamKind));
		writer.writeAttribute("lastSelectedParamShortcutX", lastSelectedParamShortcutX);
		writer.writeAttribute("lastSelectedParamShortcutY", lastSelectedParamShortcutY);
		writer.writeAttribute("lastSelectedParamArrayPosition", lastSelectedParamArrayPosition);
	}

	project.global_fx.global_effectable.writeAttributesToFile(writer, false);

	// Community Firmware parameters (always write them after the official ones, just before closing the parent tag)
	writer.writeAttribute("songGridScrollX", persistentView.songGridScrollX);
	writer.writeAttribute("songGridScrollY", persistentView.songGridScrollY);
	writer.writeAttribute("sessionLayout", persistentView.sessionLayout);

	writer.writeOpeningTagEnd();
	// Attributes end

	deluge::project::serializeScaleNotes(project.scale, writer);

	writer.writeOpeningTagBeginning("reverb");
	// The whole reverb block is DOM-authoritative: project.reverb (and the algorithm Song::model) is
	// the source of truth and AudioEngine::reverb / reverbSidechain* is a cache projected from it
	// (getReverbParamsFromSong). Save reads the document, not the engine.
	uint32_t roomSize = project.reverb.room_size * (uint32_t)2147483648u;
	uint32_t damping = project.reverb.damping * (uint32_t)2147483648u;
	uint32_t width = project.reverb.width * (uint32_t)2147483648u;
	uint32_t hpf = project.reverb.hpf * (uint32_t)2147483648u;
	uint32_t lpf = project.reverb.lpf * (uint32_t)2147483648u;

	roomSize = std::min(roomSize, (uint32_t)2147483647);
	damping = std::min(damping, (uint32_t)2147483647);
	width = std::min(width, (uint32_t)2147483647);
	hpf = std::min(hpf, (uint32_t)2147483647);
	lpf = std::min(lpf, (uint32_t)2147483647);

	writer.writeAttribute("roomSize", roomSize);
	writer.writeAttribute("dampening", damping);
	writer.writeAttribute("width", width);
	writer.writeAttribute("hpf", hpf);
	writer.writeAttribute("lpf", lpf);
	writer.writeAttribute("pan", project.reverb.pan);
	writer.writeAttribute("model", util::to_underlying(model));
	writer.writeOpeningTagEnd();

	// The reverb sidechain/compressor reads from the DOM too. AudioEngine::reverbSidechain is a shared
	// SideChain object (the same gui/menu_item/sidechain/* menus also drive per-sound sidechains); for
	// the reverb instance those menus mirror edits into project.reverb.sidechain_*. syncLevel is stored
	// as the file value (convertSyncLevelFromInternalValueToFileValue), mirroring the load conversion —
	// the legacy save wrote the raw internal value, which only round-tripped at tick magnitude 1.
	writer.writeOpeningTagBeginning("compressor");
	writer.writeAttribute("attack", project.reverb.sidechain_attack);
	writer.writeAttribute("release", project.reverb.sidechain_release);
	writer.writeAttribute("volume", project.reverb.sidechain_volume);
	writer.writeAttribute("shape", project.reverb.sidechain_shape);
	writer.writeAttribute("syncLevel",
	                      convertSyncLevelFromInternalValueToFileValue((int32_t)project.reverb.sidechain_sync));
	writer.closeTag();

	writer.writeClosingTag("reverb");

	project.global_fx.global_effectable.writeTagsToFile(writer, NULL, false);

	int32_t* valuesForOverride =
	    runtimeView.paramsInAutomationMode ? project.global_fx.unautomated_param_values.data() : nullptr;

	// Note (Phase B / 5c-3): these read the engine unpatched params, whose automation nodes are BORROWED from
	// project.global_fx.params.unpatched (the DOM store) — so save already serialises the DOM-owned node data.
	// A future Phase D step can read the DocParamSet explicitly to fully decouple save from the engine.
	writer.writeOpeningTagBeginning("songParams");
	GlobalEffectableForClip::writeParamAttributesToFile(writer, &paramManager, true, valuesForOverride);
	writer.writeOpeningTagEnd();
	GlobalEffectableForClip::writeParamTagsToFile(writer, &paramManager, true, valuesForOverride);
	writer.writeClosingTag("songParams");

	writer.writeArrayStart("instruments");
	for (Output* thisOutput = project.media.first_output; thisOutput; thisOutput = thisOutput->next) {
		thisOutput->writeToFile(nullptr, this);
	}
	writer.writeArrayEnding("instruments");
	deluge::project::serializeSections(project.arrangement, writer);

	writer.writeArrayStart("sessionClips");
	for (int32_t c = 0; c < std::ssize(project.clips.session_clips); c++) {
		Clip* clip = project.clips.session_clips[c];
		clip->writeToFile(writer, this);
	}
	writer.writeArrayEnding("sessionClips");

	if (!project.clips.arrangement_only_clips.empty()) {

		writer.writeArrayStart("arrangementOnlyTracks");
		for (int32_t c = 0; c < std::ssize(project.clips.arrangement_only_clips); c++) {
			Clip* clip = project.clips.arrangement_only_clips[c];
			if (!clip->output->clipHasInstance(clip)) {
				continue; // Get rid of any redundant Clips. There shouldn't be any, but occasionally they somehow
				          // get left over.
			}
			clip->writeToFile(writer, this);
		}
		writer.writeArrayEnding("arrangementOnlyTracks");
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
		writer.writeArrayStart("chordMem");
		for (int32_t y = 0; y < maxChordPosToSave; y++) {
			writer.writeArrayStart("chord", true, true);
			for (int i = 0; i < chordMemNoteCount[y]; i++) {
				writer.writeOpeningTagBeginning("note", true);
				writer.writeAttribute("code", chordMem[y][i]);
				writer.closeTag(true);
			}
			writer.writeArrayEnding("chord", true, true);
		}
		writer.writeArrayEnding("chordMem");
	}

	deluge::project::serializeMacros(project.arrangement, writer);

	writer.writeClosingTag("song", true, true);
}

Error Song::readFromFile(Deserializer& reader) {
	D_PRINTLN("DEBUG: readFromFile");

	outputClipInstanceListIsCurrentlyInvalid = true;

	D_PRINTLN("");
	D_PRINTLN("loading song!!!!!!!!!!!!!!");

	char const* tagName;

	for (int32_t s = 0; s < kMaxNumSections; s++) {
		project.arrangement.sections[s].num_repetitions = 0;
	}
	for (int32_t y = 0; y < 8; y++) {
		project.arrangement.session_macros[y].kind = SessionMacroKind::NO_MACRO;
	}

	uint64_t newTimePerTimerTick = (uint64_t)1 << 32; // TODO: make better!

	// reverb mode is freeverb for songs that predate having multiple options. New songs will set it to mutable anyway
	// so this is only used as a fallback
	model = deluge::dsp::Reverb::Model::FREEVERB;

	if (!reader.match('{'))
		return Error::FILE_CORRUPTED;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		// D_PRINTLN(tagName); delayMS(30);
		switch (*(uint32_t*)tagName) {

		// "reve" -> reverb
		case 0x65766572:
			if (!strcmp(tagName, "reverb")) {
				reader.match('{');
				while (*(tagName = reader.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "model")) {
						model = static_cast<deluge::dsp::Reverb::Model>(reader.readTagOrAttributeValueInt());
						reader.exitTag("model");
					}
					else if (!strcmp(tagName, "roomSize")) {
						project.reverb.room_size = (float)reader.readTagOrAttributeValueInt() / 2147483648u;
						reader.exitTag("roomSize");
					}
					else if (!strcmp(tagName, "dampening")) {
						project.reverb.damping = (float)reader.readTagOrAttributeValueInt() / 2147483648u;
						reader.exitTag("dampening");
					}
					else if (!strcmp(tagName, "width")) {
						int32_t widthInt = reader.readTagOrAttributeValueInt();
						if (widthInt == -2147483648) {
							widthInt = 2147483647; // Was being saved incorrectly in V2.1.0-beta1 and alphas, so we fix
							                       // it on read here!
						}
						project.reverb.width = (float)widthInt / 2147483648u;
						reader.exitTag("width");
					}
					else if (!strcmp(tagName, "hpf")) {
						project.reverb.hpf = (float)reader.readTagOrAttributeValueInt() / 2147483648u;
						reader.exitTag("hpf");
					}
					else if (!strcmp(tagName, "lpf")) {
						project.reverb.lpf = (float)reader.readTagOrAttributeValueInt() / 2147483648u;
						reader.exitTag("lpf");
					}
					else if (!strcmp(tagName, "pan")) {
						project.reverb.pan = reader.readTagOrAttributeValueInt();
						reader.exitTag("pan");
					}
					else if (!strcmp(tagName, "compressor")) {
						reader.match('{');
						while (*(tagName = reader.readNextTagOrAttributeName())) {
							if (!strcmp(tagName, "attack")) {
								project.reverb.sidechain_attack = reader.readTagOrAttributeValueInt();
								reader.exitTag("attack");
							}
							else if (!strcmp(tagName, "release")) {
								project.reverb.sidechain_release = reader.readTagOrAttributeValueInt();
								reader.exitTag("release");
							}
							else if (!strcmp(tagName, "volume")) {
								project.reverb.sidechain_volume = reader.readTagOrAttributeValueInt();
								reader.exitTag("volume");
							}
							else if (!strcmp(tagName, "shape")) {
								project.reverb.sidechain_shape = reader.readTagOrAttributeValueInt();
								reader.exitTag("shape");
							}
							else if (!strcmp(tagName, "syncLevel")) {
								project.reverb.sidechain_sync = (SyncLevel)convertSyncLevelFromFileValueToInternalValue(
								    reader.readTagOrAttributeValueInt());
								project.reverb.sidechain_sync =
								    (SyncLevel)std::min((uint8_t)project.reverb.sidechain_sync, (uint8_t)9);
								reader.exitTag("syncLevel");
							}
							else {
								reader.exitTag(tagName);
							}
						}
						reader.exitTag("compressor", true);
					}
					else {
						reader.exitTag(tagName);
					}
				}
			}
			else {
				goto unknownTag;
			}
			reader.exitTag(nullptr, true); // exit value object
			break;

		case charsToIntegerConstant('x', 'S', 'c', 'r'):

			switch (*(((uint32_t*)tagName) + 1)) {

			// "oll\x00" ->xScroll
			case 0x006c6c6f:
				persistentView.xScroll[NAVIGATION_CLIP] = reader.readTagOrAttributeValueInt();
				persistentView.xScroll[NAVIGATION_CLIP] = std::max((int32_t)0, persistentView.xScroll[NAVIGATION_CLIP]);
				break;

			// "ollS" -> xScrollSongView
			case 0x536c6c6f:
				if (!strcmp(tagName, "xScrollSongView")) {
					persistentView.xScrollForReturnToSongView = reader.readTagOrAttributeValueInt();
					persistentView.xScrollForReturnToSongView =
					    std::max((int32_t)0, persistentView.xScrollForReturnToSongView);
					break;
				}
				else {
					goto unknownTag;
				}

			// "ollA" -> xScrollArrangementView"
			case 0x416c6c6f:
				if (!strcmp(tagName, "xScrollArrangementView")) {
					persistentView.xScroll[NAVIGATION_ARRANGEMENT] = reader.readTagOrAttributeValueInt();
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
				persistentView.xZoomForReturnToSongView = reader.readTagOrAttributeValueInt();
				persistentView.xZoomForReturnToSongView = std::max((int32_t)1, persistentView.xZoomForReturnToSongView);
			}

			// "xZoom"
			else if (!strcmp(tagName, "xZoom")) {
				persistentView.xZoom[NAVIGATION_CLIP] = reader.readTagOrAttributeValueInt();
				persistentView.xZoom[NAVIGATION_CLIP] = std::max((uint32_t)1, persistentView.xZoom[NAVIGATION_CLIP]);
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
					persistentView.songViewYScroll = reader.readTagOrAttributeValueInt();
					persistentView.songViewYScroll = std::max(1 - kDisplayHeight, persistentView.songViewYScroll);
					break;
				}
				else {
					goto unknownTag;
				}

			// "yScrollArrangementView"
			case 0x416c6c6f:
				if (!strcmp(tagName, "yScrollArrangementView")) {
					persistentView.arrangementYScroll = reader.readTagOrAttributeValueInt();
					persistentView.arrangementYScroll = std::max(1 - kDisplayHeight, persistentView.arrangementYScroll);
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
				reader.tryReadingFirmwareTagFromFile(tagName, false);
				reader.exitTag(tagName);
			}
			else if (!strcmp(tagName, "preview") || !strcmp(tagName, "previewNumPads")) {
				reader.tryReadingFirmwareTagFromFile(tagName, false);
				reader.exitTag(tagName);
			}
			else if (!strcmp(tagName, "sessionLayout")) {
				persistentView.sessionLayout = (SessionLayoutType)reader.readTagOrAttributeValueInt();
				reader.exitTag("sessionLayout");
			}

			else if (!strcmp(tagName, "songGridScrollX")) {
				persistentView.songGridScrollX = reader.readTagOrAttributeValueInt();
				reader.exitTag("songGridScrollX");
			}

			else if (!strcmp(tagName, "songGridScrollY")) {
				persistentView.songGridScrollY = reader.readTagOrAttributeValueInt();
				reader.exitTag("songGridScrollY");
			}

			else if (!strcmp(tagName, "xZoomArrangementView")) {
				persistentView.xZoom[NAVIGATION_ARRANGEMENT] = reader.readTagOrAttributeValueInt();
				reader.exitTag("xZoomArrangementView");
			}

			else if (!strcmp(tagName,
			                 "inArrangementView")) { // For V2.0 pre-beta songs. There'd be another way to detect
				                                     // this...
				persistentView.lastClipInstanceEnteredStartPos = 0;
				reader.exitTag("inArrangementView");
			}

			else if (!strcmp(tagName, "currentTrackInstanceArrangementPos")) {
				persistentView.lastClipInstanceEnteredStartPos = reader.readTagOrAttributeValueInt();
				reader.exitTag("currentTrackInstanceArrangementPos");
			}

			else if (!strcmp(tagName, "arrangementAutoScrollOn")) {
				persistentView.arrangerAutoScrollModeActive = reader.readTagOrAttributeValueInt();
				reader.exitTag("arrangementAutoScrollOn");
			}

			else if (deluge::project::deserialize(project.transport, reader, newTimePerTimerTick, tagName)) {}

			else if (deluge::project::deserializeScale(project.scale, reader, tagName)) {}

			else if (deluge::project::deserialize(project.groove, reader, tagName)) {}

			else if (!strcmp(tagName, "tripletsLevel")) {
				persistentView.tripletsLevel = reader.readTagOrAttributeValueInt();
				reader.exitTag("tripletsLevel");
				persistentView.tripletsOn = true;
			}

			else if (!strcmp(tagName, "activeModFunction")) {
				project.global_fx.global_effectable.modKnobMode = reader.readTagOrAttributeValueInt();
				project.global_fx.global_effectable.modKnobMode =
				    std::min(project.global_fx.global_effectable.modKnobMode, (uint8_t)(kNumModButtons - 1));
				reader.exitTag("activeModFunction");
			}

			else if (!strcmp(tagName, "affectEntire")) {
				persistentView.affectEntire = reader.readTagOrAttributeValueInt();
				reader.exitTag("affectEntire");
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
						project.global_fx.global_effectable.compressor.setAttack(masterCompressorAttack);
						reader.exitTag("attack");
					}
					else if (!strcmp(tagName, "release")) {
						q31_t masterCompressorRelease = reader.readTagOrAttributeValueInt();
						project.global_fx.global_effectable.compressor.setRelease(masterCompressorRelease);
						reader.exitTag("release");
					}
					else if (!strcmp(tagName, "thresh")) {
						q31_t masterCompressorThresh = reader.readTagOrAttributeValueInt();
						project.global_fx.global_effectable.compressor.setThreshold(masterCompressorThresh);
						reader.exitTag("thresh");
					}
					else if (!strcmp(tagName, "ratio")) {
						q31_t masterCompressorRatio = reader.readTagOrAttributeValueInt();
						project.global_fx.global_effectable.compressor.setRatio(masterCompressorRatio);
						reader.exitTag("ratio");
					}
					else if (!strcmp(tagName, "compHPF")) {
						q31_t masterCompressorSidechain = reader.readTagOrAttributeValueInt();
						project.global_fx.global_effectable.compressor.setSidechain(masterCompressorSidechain);
						reader.exitTag("compHPF");
					}
					else {
						reader.exitTag(tagName);
					}
				}
				reader.exitTag("songCompressor");
			}

			else if (!strcmp(tagName, "chordMem")) {
				reader.match('[');
				int slot_index = 0;
				while (reader.match('{') && *(tagName = reader.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "chord")) {
						int y = slot_index++;
						if (y >= kDisplayHeight) {
							reader.exitTag("chord");
							continue;
						}
						int i = 0;
						reader.match('[');
						while (reader.match('{') && *(tagName = reader.readNextTagOrAttributeName())) {
							if (!strcmp(tagName, "note")) {
								reader.match('{');
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
								reader.match('}'); // note value object
								reader.match('}'); // note box
							}
							else {
								reader.exitTag();
							}
						}
						chordMemNoteCount[y] = std::min(MAX_NOTES_CHORD_MEM, i);
						reader.match(']'); // close note array.
						reader.match('}'); // close chord box.
					}
					else {
						reader.exitTag();
					}
				}
				reader.match(']');
				reader.exitTag("chordMem");
			}

			else if (deluge::project::deserializeMacros(project.arrangement, reader, *this, tagName)) {}

			else if (deluge::project::deserializeSections(project.arrangement, reader, tagName)) {}

			else if (!strcmp(tagName, "instruments")) {
				reader.match('[');
				Output** lastPointer = &project.media.first_output;
				while (reader.match('{') && *(tagName = reader.readNextTagOrAttributeName())) {

					void* memory;
					Output* newOutput;
					char const* defaultDirPath;
					Error error;

					if (!strcmp(tagName, "audioTrack")) {
						memory = deluge::memory::alloc_fast(sizeof(AudioOutput));
						if (!memory) {
							return Error::INSUFFICIENT_RAM;
						}
						newOutput = new (memory) AudioOutput();
						reader.match('{');
						goto loadOutput;
					}

					else if (!strcmp(tagName, "sound")) {
						memory = deluge::memory::alloc_fast(sizeof(SoundInstrument));
						if (!memory) {
							return Error::INSUFFICIENT_RAM;
						}
						reader.match('{');
						newOutput = new (memory) SoundInstrument();
						defaultDirPath = "SYNTHS";

setDirPathFirst:
						((Instrument*)newOutput)->dirPath = defaultDirPath;

loadOutput:
						error = newOutput->readFromFile(
						    reader, this, nullptr,
						    0); // If it finds any default params, it'll make a ParamManager and "back it up"

						if (error != Error::NONE) {
							newOutput->~Output();
							delugeDealloc(memory);
							return error;
						}
						((Instrument*)newOutput)->mightExistOnCard = true;
						*lastPointer = newOutput;
						lastPointer = &newOutput->next;
						reader.match('}');
					}

					else if (!strcmp(tagName, "kit")) {
						memory = deluge::memory::alloc_fast(sizeof(Kit));
						if (!memory) {
							return Error::INSUFFICIENT_RAM;
						}
						reader.match('{');
						newOutput = new (memory) Kit();
						defaultDirPath = "KITS";
						goto setDirPathFirst;
					}
					// old firmwares used midiChannel or mpeZone depending on the type, but this distinction is never
					// used. The distinction is later in a different tag which can be "midiChannel" or "zone"
					else if (!strcmp(tagName, "midi") || !strcmp(tagName, "midiChannel")
					         || !strcmp(tagName, "mpeZone")) {
						memory = deluge::memory::alloc_fast(sizeof(MIDIInstrument));
						if (!memory) {
							return Error::INSUFFICIENT_RAM;
						}
						newOutput = new (memory) MIDIInstrument();
						goto loadOutput;
					}

					else if (!strcmp(tagName, "cvChannel")) {
						memory = deluge::memory::alloc_fast(sizeof(CVInstrument));
						if (!memory) {
							return Error::INSUFFICIENT_RAM;
						}
						newOutput = new (memory) CVInstrument();
						goto loadOutput;
					}

					reader.exitTag(tagName, true);
				}
				reader.exitTag("instruments");
				reader.match(']');
			}

			else if (!strcmp(tagName, "songParams")) {
				reader.match('{');
				// Phase 2 (Option B): read the song-global params INTO the DOM store, then project the store to
				// the engine paramManager (fromDocParamManager). The store is the authority; the engine unpatched
				// set is its projection. (Clips still read straight into the engine — a later phase.)
				if (!paramManager.containsAnyMainParamCollections()) {
					Error error = paramManager.setupUnpatched();
					if (error != Error::NONE) {
						return error;
					}
					GlobalEffectable::initParams(&paramManager);
				}
				// M2-F: seed the store from the freshly-initialised engine set (so params the file OMITS keep their
				// init current_value), then BIND the engine set to it BEFORE reading — so the reader writes the
				// present params' nodes straight into the DOM store the engine will resolve, with no ownDoc_ round
				// trip. Nodes are never copied out of the store (avoids a projection self-copy).
				UnpatchedParamSet* songUnpatched = paramManager.getUnpatchedParamSet();
				project.global_fx.params.unpatched = deluge::project::toDocParamSet(*songUnpatched);
				deluge::project::DocParamSet& songStore = *project.global_fx.params.unpatched;
				songUnpatched->bindDomStoreExternal(&songStore);
				char const* paramTag;
				while (*(paramTag = reader.readNextTagOrAttributeName())) {
					if (deluge::project::readGlobalEffectableParamTagToDoc(reader, paramTag, songStore, 2147483647)) {}
					else {
						reader.exitTag(paramTag);
					}
				}
				// Sync each engine RT cursor from the store's (read or seeded-default) current_value. Nodes already
				// live in the store, resolved via owner-resolution.
				for (int32_t p = 0; p < songUnpatched->getNumParams() && p < (int32_t)songStore.params.size(); p++) {
					songUnpatched->params[p].currentValue = songStore.params[p].current_value;
				}
				reader.exitTag("songParams", true);
			}

			else if (!strcmp(tagName, "tracks") || !strcmp(tagName, "sessionClips")) {
				Error error = readClipsFromFile(reader, &project.clips.session_clips);
				if (error != Error::NONE) {
					return error;
				}
				reader.exitTag();
			}

			else if (!strcmp(tagName, "arrangementOnlyTracks") || !strcmp(tagName, "arrangementOnlyClips")) {
				Error error = readClipsFromFile(reader, &project.clips.arrangement_only_clips);
				if (error != Error::NONE) {
					return error;
				}
				reader.exitTag();
			}

			else {
				Error result = project.global_fx.global_effectable.readTagFromFile(reader, tagName, &paramManager,
				                                                                   2147483647, nullptr, this);
				if (result == Error::NONE) {}
				else if (result != Error::RESULT_TAG_UNUSED) {
					return result;
				}
				else {
					Error result = reader.tryReadingFirmwareTagFromFile(tagName, false);
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

	if (song_firmware_version >= FirmwareVersion::official({3, 1, 0, "alpha2"})) {
		// Basically, like all other "sync" type parameters, the file value and internal value are different for
		// swingInterval. But unlike other ones, which get converted as we go, we do this one at the end once we
		// know we have enough info to do the conversion
		project.groove.swing_interval = convertSyncLevelFromFileValueToInternalValue(project.groove.swing_interval);
	}
	project.groove.swing_interval = std::min(project.groove.swing_interval, (uint8_t)9);

	setTimePerTimerTick(newTimePerTimerTick);

	// make sure the bpm param matches the ticks in case something went wrong? ref issue 2455, possible cause is invalid
	// tempo param
	setBPM(calculateBPM(), false);

	// Ensure all arranger-only Clips have their section as 255
	for (int32_t t = 0; t < std::ssize(project.clips.arrangement_only_clips); t++) {
		Clip* clip = project.clips.arrangement_only_clips[t];

		clip->section = 255;
		clip->gotInstanceYet = false;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	int32_t count = 0;

	// Match all Clips up with their Output
	// For each Clip in session and arranger
	std::array<ClipArray*, 2> const arrays{
	    &project.clips.session_clips,
	    &project.clips.arrangement_only_clips,
	};
	for (ClipArray* clipArray : arrays) {
		for (int32_t c = 0; c < std::ssize(*clipArray); c++) {
			Clip* thisClip = (*clipArray)[c]; // TODO: deal with other Clips too!

			if (!(count & 31)) {
				AudioEngine::routineWithClusterLoading();
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
			if (song_firmware_version < FirmwareVersion::official({1, 5, 0, "pretest"})
			    && thisClip->output->type == OutputType::SYNTH) {
				if (((InstrumentClip*)thisClip)->arpSettings.mode != ArpMode::OFF
				    && !((InstrumentClip*)thisClip)->arpSettings.syncLevel) {
					ParamManagerForTimeline* thisParamManager = &thisClip->paramManager;
					thisParamManager->getPatchedParamSet()->params[params::GLOBAL_ARP_RATE].shiftValues(
					    *thisParamManager->getPatchedParamSet()->docForParam(params::GLOBAL_ARP_RATE),
					    (1 << 30) + (1 << 28));
				}
			}
		}
	}

	// Phase C (DOM single-store): borrow-point each AudioClip's unpatched automation node storage at a
	// DOM-owned DocParamSet anchored on the ParamSet (Model A), mirroring the song-global borrow below. Every
	// edit + undo then operates on DOM-owned data and save reads it directly. The store lives on the heap-stable,
	// never-relocating ParamSet, so it survives the clip through ParamBackup steal/restore. AudioClips carry a
	// single unpatched set (GlobalEffectableForClip); InstrumentClips/kits are later Movement-1 steps.
	for (ClipArray* clipArray : arrays) {
		for (int32_t c = 0; c < std::ssize(*clipArray); c++) {
			Clip* thisClip = (*clipArray)[c];
			if (thisClip->type == ClipType::AUDIO && thisClip->paramManager.containsAnyMainParamCollections()) {
				UnpatchedParamSet* unpatched = thisClip->paramManager.getUnpatchedParamSet();
				// M2-G S2b: relocate the audio clip's unpatched automation into the persistent project tree, keyed
				// by the clip's stable serialId, and bind the engine set to that tree store (so doc() id-resolves
				// it and docForParam/save read the same nodes). A clip with no serialId (never saved) keeps its
				// engine-owned store. Seed the tree store from what the file read into the engine set first.
				if (std::optional<uint32_t> id = thisClip->serialId()) {
					deluge::project::DocParamManager& mgr = project.clip_params.getOrCreate(*id);
					mgr.unpatched = deluge::project::toDocParamSet(*unpatched);
					unpatched->bindDomStoreExternal(&*mgr.unpatched);
				}
				else {
					unpatched->bindDomStore();
				}
			}
		}
	}

	AudioEngine::logAction("matched up");

	AudioEngine::routineWithClusterLoading();

	anyOutputsSoloingInArrangement = false;

	D_PRINTLN("aaa1");

	// Match all ClipInstances up with their Clip. And while we're at it, check if any Outputs are soloing in
	// arranger
	for (Output* thisOutput = project.media.first_output; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput->soloingInArrangementMode) {
			anyOutputsSoloingInArrangement = true;
		}

		if (thisOutput->clipInstances.empty()
		    && project.param_backup.getPreferablyWithClip((ModControllableAudio*)thisOutput, nullptr) == nullptr
		    && thisOutput->type == OutputType::AUDIO) {
			// This clip has no way to get a param manager, and no clips to help it out. Need to create a backup or
			// things will go wrong later.
			ParamManagerForTimeline paramManager{};

			paramManager.setupUnpatched();
			GlobalEffectable::initParams(&paramManager);

			this->project.param_backup.backUp((ModControllableAudio*)thisOutput->toModControllable(), nullptr,
			                                  &paramManager);
		}

		for (int32_t i = 0; i < std::ssize(thisOutput->clipInstances); i++) {
			ClipInstance* thisInstance = &thisOutput->clipInstances[i];

			// Grab out the encoded Clip reference and turn it into an actual Clip*
			uint32_t clipCode = (uint32_t)(uintptr_t)thisInstance->clip;

			// Special case for NULL Clip
			if (clipCode == 0xFFFFFFFF) {
				thisInstance->clip = nullptr;
			}

			else {
				uint32_t lookingForIndex = clipCode & ~((uint32_t)1 << 31);
				bool isArrangementClip = clipCode >> 31;

				ClipArray* clips =
				    isArrangementClip ? &project.clips.arrangement_only_clips : &project.clips.session_clips;

				if (lookingForIndex >= static_cast<int32_t>(clips->size())) {
#if ALPHA_OR_BETA_VERSION
					display->displayPopup("E248");
#endif
skipInstance:
					thisOutput->clipInstances.erase(thisOutput->clipInstances.begin() + i);
					i--;
					continue;
				}

				thisInstance->clip = (*clips)[lookingForIndex];

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
		if (thisOutput->type == OutputType::AUDIO) {
			auto ao = (AudioOutput*)thisOutput;
			if (ao->inputChannel == AudioInputChannel::SPECIFIC_OUTPUT) {
				ao->setOutputRecordingFrom(getOutputFromIndex(ao->outputRecordingFromIndex));
			}
		}
		// If saved before V2.1, set sample-based synth instruments to linear interpolation, cos that's how it was
		if (song_firmware_version < FirmwareVersion::official({2, 1, 0, "beta"})) {
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
	for (int32_t c = 0; c < std::ssize(project.clips.arrangement_only_clips); c++) {
		Clip* clip = project.clips.arrangement_only_clips[c];

		if (!clip->gotInstanceYet) {
#if ALPHA_OR_BETA_VERSION
			display->displayPopup("E043");
#endif
			if (runtimeView.currentClip == clip) {
				runtimeView.currentClip = nullptr;
			}
			if (project.transport.sync_scaling_clip == clip) {
				project.transport.sync_scaling_clip = nullptr;
			}

			project.clips.arrangement_only_clips.erase(project.clips.arrangement_only_clips.begin() + c);
			deleteClipObject(clip, false, InstrumentRemoval::NONE);
			c--;
		}
	}

	// Pre V1.2...
	if (song_firmware_version < FirmwareVersion::official({1, 2, 0})) {

		deleteAllBackedUpParamManagers(); // Before V1.2, lots of extras of these could be created during loading
		project.global_fx.global_effectable.compensateVolumeForResonance(&paramManager);
	}

	if (project.transport.sync_scaling_clip) {
		Clip* newInputTickScaleClip = project.transport.sync_scaling_clip;
		project.transport.sync_scaling_clip =
		    nullptr; // We shouldn't have set this manually earlier, anyway - we just saved hassle
		setInputTickScaleClip(newInputTickScaleClip);
	}

	D_PRINTLN("aaa3");
	AudioEngine::logAction("aaa3.1");

	AudioEngine::routineWithClusterLoading();

	reassessWhetherAnyClipsSoloing();

	AudioEngine::logAction("aaa4.2");

	setupPatchingForAllParamManagers();
	AudioEngine::logAction("aaa4.3");

	// Phase B (DOM single-store): the DOM is the sole owner of the song-global unpatched automation node storage
	// (project.global_fx.params.unpatched); the engine set owner-resolves to it, so every edit + undo operates on
	// DOM-owned data and save reads it directly. The <songParams> read path above already created + bound the
	// store; this handles the file-had-no-<songParams> case (seed from the engine defaults, then bind). M2-F: no
	// moveNodesInto — the engine holds no ownDoc_ node data to move; reads/edits land in the store directly.
	{
		UnpatchedParamSet* unpatchedEngine = paramManager.getUnpatchedParamSet();
		if (!project.global_fx.params.unpatched) {
			project.global_fx.params.unpatched = deluge::project::toDocParamSet(*unpatchedEngine);
		}
		deluge::project::DocParamSet& store = *project.global_fx.params.unpatched;
#if ALPHA_OR_BETA_VERSION
		if (static_cast<int32_t>(store.params.size()) != unpatchedEngine->getNumParams()) {
			FREEZE_WITH_ERROR("E4B0"); // DOM/engine song-global param-count mismatch
		}
#endif
		unpatchedEngine->bindDomStoreExternal(&store); // idempotent if the <songParams> path already bound it
	}

	// Phase C (InstrumentClip): borrow-point each instrument clip's automation node storage at DOM-owned
	// DocParamSets (Model A), like the AudioClip / song-global borrows above. setupPatchingForAllParamManagers
	// has finalized every instrument clip's param manager by now. bindMainParamSetDomStores binds unpatched
	// (always) + patched (synth/drum only) and no-ops for MIDI/CV. For KIT clips the drum params live
	// per-NoteRow, so bind each row's manager too (the kit clip-level manager is its unpatched-only global FX).
	// ParamSets are heap-allocated, so their stores survive NoteRow relocation / ParamBackup steal (Model A).
	{
		std::array<ClipArray*, 2> const instClipArrays{&project.clips.session_clips,
		                                               &project.clips.arrangement_only_clips};
		for (ClipArray* clipArray : instClipArrays) {
			for (int32_t c = 0; c < std::ssize(*clipArray); c++) {
				Clip* thisClip = (*clipArray)[c];
				if (thisClip->type != ClipType::INSTRUMENT || thisClip->output == nullptr) {
					continue;
				}
				OutputType ot = thisClip->output->type;
				// Only bind SOUND managers (unpatched ParamSet at summaries[0]). MIDI/CV clip managers hold a
				// MIDIParamCollection there — NOT a ParamSet — so bindMainParamSetDomStores would corrupt them.
				if (ot == OutputType::SYNTH) {
					thisClip->paramManager.bindMainParamSetDomStores(); // synth: unpatched + patched
				}
				else if (ot == OutputType::KIT) {
					thisClip->paramManager.bindMainParamSetDomStores(); // kit clip-level global-FX (unpatched)
					InstrumentClip* instrumentClip = static_cast<InstrumentClip*>(thisClip);
					for (int32_t n = 0; n < std::ssize(instrumentClip->noteRows); n++) {
						NoteRow& noteRow = instrumentClip->noteRows[n];
						if (noteRow.drum != nullptr && noteRow.drum->type == DrumType::SOUND) {
							noteRow.paramManager.bindMainParamSetDomStores(); // per-SoundDrum: unpatched + patched
						}
					}
				}
				else if (ot == OutputType::MIDI_OUT || ot == OutputType::CV) {
					// MIDI/CV clip managers hold a MIDIParamCollection (variable per-CC AutoParams), NOT a
					// ParamSet — bind via its own store anchored on the heap-stable collection.
					if (thisClip->paramManager.containsAnyParamCollectionsIncludingExpression()) {
						thisClip->paramManager.getMIDIParamCollection()->bindDomStore();
					}
				}
			}
		}
	}

	AudioEngine::routineWithClusterLoading();

	int32_t playbackWillStartInArrangerAtPos =
	    playbackHandler.playbackState ? persistentView.lastClipInstanceEnteredStartPos : -1;

	AudioEngine::logAction("aaa5.1");
	sortOutWhichClipsAreActiveWithoutSendingPGMs(modelStack, playbackWillStartInArrangerAtPos);
	AudioEngine::logAction("aaa5.2");

	AudioEngine::routineWithClusterLoading();

	// Advance the serial-id counters past every id restored from the file, so any object created
	// after load gets a fresh, non-colliding id.
	syncSerialIdCountersFromLoaded();

	return Error::NONE;
}

Error Song::readClipsFromFile(Deserializer& reader, ClipArray* clipArray) {
	char const* tagName;
	reader.match('[');
	while (reader.match('{') && *(tagName = reader.readNextTagOrAttributeName())) {

		int32_t allocationSize;
		ClipType clipType;

		if (!strcmp(tagName, "track") || !strcmp(tagName, "instrumentClip")) {
			allocationSize = sizeof(InstrumentClip);
			clipType = ClipType::INSTRUMENT;

readClip:
			if (!clipArray->reserveExtra(1)) {
				return Error::INSUFFICIENT_RAM;
			}

			void* memory = deluge::memory::alloc_fast(allocationSize);
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

			(void)clipArray->insertClipAt(newClip, std::ssize(*clipArray)); // We made sure enough space, above

			reader.exitTag(nullptr, true); // exit value object
			reader.match('}');             // exit box.
		}
		else if (!strcmp(tagName, "audioClip")) {
			allocationSize = sizeof(AudioClip);
			clipType = ClipType::AUDIO;

			goto readClip;
		}
		else if (*tagName) {
			reader.exitTag(tagName);
		}
	}
	reader.match(']'); // leave array.
	return Error::NONE;
}

// Needs to be in a separate function than the above because the main song XML file needs to be closed first before
// this is called, because this will open other (sample) files
void Song::loadAllSamples(bool mayActuallyReadFiles) {

	for (Output* thisOutput = project.media.first_output; thisOutput; thisOutput = thisOutput->next) {
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
			AudioEngine::routineWithClusterLoading();
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
	for (Output* thisOutput = project.media.first_output; thisOutput; thisOutput = thisOutput->next) {
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

		AudioEngine::routineWithClusterLoading();
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

		AudioEngine::routineWithClusterLoading();
		if (clip->deleteSoundsWhichWontSound(this)) {
			it.deleteClip(InstrumentRemoval::DELETE);
		}
		else {
			++it;
		}
	}

	for (Output* thisOutput = project.media.first_output; thisOutput; thisOutput = thisOutput->next) {
		thisOutput->clipInstances.clear();
	}

	deleteAllOutputs((Output**)&project.media.first_hibernating_instrument);
	deleteHibernatingMIDIInstrument();

	// Can't delete the ones with no Clips, cos these might be needed by their ModControllables
	deleteAllBackedUpParamManagersWithClips();
}

void Song::renderAudio(std::span<StereoSample> outputBuffer, int32_t* reverbBuffer, int32_t sideChainHitPending) {

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
	for (Output* output = project.media.first_output; output; output = output->next) {
		if (!output->inValidState) {
			continue;
		}

		bool isClipActiveNow =
		    (output->getActiveClip() && isClipActive(output->getActiveClip()->getClipBeingRecordedFrom()));
		ENTER_CRITICAL_SECTION();
		if (output->shouldRenderInSong()) {
			output->renderOutput(modelStack, outputBuffer, reverbBuffer, volumePostFX >> 1, sideChainHitPending,
			                     !isClipActiveNow, isClipActiveNow);
		}
		EXIT_CRITICAL_SECTION();
#if DO_AUDIO_LOG
		char buf[64];
		snprintf(buf, sizeof(buf), "complete: %s", output->name.get());
		AudioEngine::logAction(buf);
#endif
	}
	AudioEngine::logAction("done rendering outputs");
	// If recording the "MIX", this is the place where we want to grab it - before any master FX or volume applied
	// Go through each SampleRecorder, feeding them audio
	for (SampleRecorder* recorder = AudioEngine::firstRecorder; recorder; recorder = recorder->next) {

		if (recorder->status >= RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
			continue;
		}

		if (recorder->mode == AudioInputChannel::MIX) {
			recorder->feedAudio(outputBuffer, true);
		}
	}
	AudioEngine::logAction("done recorders");

	Delay::State delayWorkingState = project.global_fx.global_effectable.createDelayWorkingState(paramManager);

	int32_t postReverbVolume = paramNeutralValues[params::GLOBAL_VOLUME_POST_REVERB_SEND];
	int32_t reverbSendAmount =
	    getFinalParameterValueVolume(paramNeutralValues[params::GLOBAL_REVERB_AMOUNT],
	                                 cableToLinearParamShortcut(paramManager.getUnpatchedParamSet()->getValue(
	                                     params::UNPATCHED_REVERB_SEND_AMOUNT)));

	// don't bother checking if sound is coming in - its just to save resources and if nothing is being rendered we
	// don't need to
	project.global_fx.global_effectable.processFXForGlobalEffectable(outputBuffer, &volumePostFX, &paramManager,
	                                                                 delayWorkingState, true, reverbSendAmount >> 3);

	project.global_fx.global_effectable.processReverbSendAndVolume(outputBuffer, reverbBuffer, volumePostFX,
	                                                               postReverbVolume, reverbSendAmount >> 1);
	AudioEngine::logAction("done global effectables");

	if (playbackHandler.isEitherClockActive() && !playbackHandler.ticksLeftInCountIn
	    && currentPlaybackMode == &arrangement) {
		const bool result = params::kMaxNumUnpatchedParams > 32
		                        ? paramManager.getUnpatchedParamSetSummary()->whichParamsAreInterpolating[0]
		                              || paramManager.getUnpatchedParamSetSummary()->whichParamsAreInterpolating[1]
		                        : paramManager.getUnpatchedParamSetSummary()->whichParamsAreInterpolating[0];
		if (result) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings = addToModelStack(modelStack);
			paramManager.tickSamples(outputBuffer.size(), modelStackWithThreeMainThings);
		}
	}
	AudioEngine::logAction("done render");
}

void Song::setTimePerTimerTick(uint64_t newTimeBig, bool shouldLogAction) {

	if (shouldLogAction) {
		actionLogger.recordTempoChange(project.transport.samples_per_timer_tick_big, newTimeBig);
	}

	// Alter timing of next and last timer ticks
	if (currentSong == this && playbackHandler.isInternalClockActive()) {
		uint32_t timeSinceLastTimerTick =
		    AudioEngine::audioSampleTimer - (uint32_t)(playbackHandler.timeLastTimerTickBig >> 32);

		// Using intermediary float here is best: newTimeBig might be a huge number, and below about 2bpm (so not
		// that low), multiplying it by timeSinceLastTimerTick can overflow a uint64_t
		timeSinceLastTimerTick =
		    (float)timeSinceLastTimerTick * newTimeBig / project.transport.samples_per_timer_tick_big;

		playbackHandler.timeLastTimerTickBig =
		    (uint64_t)(uint32_t)(AudioEngine::audioSampleTimer - timeSinceLastTimerTick) << 32;

		uint32_t timeTilNextTimerTick =
		    (uint32_t)(playbackHandler.timeNextTimerTickBig >> 32) - AudioEngine::audioSampleTimer;

		// Using intermediary float here is best: newTimeBig might be a huge number, and below about 2bpm (so not
		// that low), multiplying it by timeSinceLastTimerTick can overflow a uint64_t
		timeTilNextTimerTick = (float)timeTilNextTimerTick * newTimeBig / project.transport.samples_per_timer_tick_big;

		playbackHandler.timeNextTimerTickBig = (uint64_t)(AudioEngine::audioSampleTimer + timeTilNextTimerTick) << 32;
	}

	project.transport.samples_per_timer_tick_big = newTimeBig;

	project.transport.inverse_samples_per_timer_tick = ((uint64_t)1 << 63) / ((newTimeBig * 3) >> 1);

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
	return project.groove.hasSwing();
}

void Song::resyncLFOs() {

	for (Output* thisOutput = project.media.first_output; thisOutput; thisOutput = thisOutput->next) {
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
	ClipArray* clipArray = &project.clips.session_clips;
	bool doingClipsProvidedByOutput = false;
decideNumElements:
	int32_t numElements = std::ssize(*clipArray);
	// TODO: Should use AllClips(), but less obvious so later.
traverseClips:
	for (int32_t c = 0; c < numElements; c++) {
		InstrumentClip* instrumentClip;
		if (!doingClipsProvidedByOutput) {
			Clip* clip = (*clipArray)[c];
			if (clip == stopTraversalAtClip) {
				return nullptr;
			}
			if (clip->output != kit) {
				continue;
			}
			instrumentClip = (InstrumentClip*)clip;
		}
		else {
			ClipInstance* clipInstance = &kit->clipInstances[c];
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
	if (!doingClipsProvidedByOutput && clipArray == &project.clips.session_clips) {
		if (outputClipInstanceListIsCurrentlyInvalid) {
			clipArray = &project.clips.arrangement_only_clips;
			goto decideNumElements;
		}
		else {
			doingClipsProvidedByOutput = true;
			numElements = std::ssize(kit->clipInstances);
			goto traverseClips;
		}
	}

	return nullptr;
}

ParamManagerForTimeline* Song::findParamManagerForDrum(Kit* kit, Drum* drum, Clip* stopTraversalAtClip) {
	NoteRow* noteRow = findNoteRowForDrum(kit, drum, stopTraversalAtClip);
	if (!noteRow) {
		return nullptr;
	}
	return &noteRow->paramManager;
}

void Song::setupPatchingForAllParamManagersForDrum(SoundDrum* drum) {

	// If currently swapping an Instrument, it can't be assumed that all arranger-only Clips for this Instrument are
	// in its clipInstances, which otherwise is a nice time-saver

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	// We don't know the Kit / Instrument. That's unfortunate. We'll work it out if we can
	Output* output = nullptr;

	// For each Clip in session and arranger for specific Output - but if currentlySwappingInstrument, use master
	// list for arranger Clips
	ClipArray* clipArray = &project.clips.session_clips;
	bool doingClipsProvidedByOutput = false;
decideNumElements:
	int32_t numElements = std::ssize(*clipArray);
	// TODO: Should use AllClips, but less obvious so later.
traverseClips:
	for (int32_t c = 0; c < numElements; c++) {
		InstrumentClip* instrumentClip;
		if (!doingClipsProvidedByOutput) {
			Clip* clip = (*clipArray)[c];

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
			ClipInstance* clipInstance = &output->clipInstances[c];
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
	if (!doingClipsProvidedByOutput && clipArray == &project.clips.session_clips) {
		// If Instrument is currently being "swapped", or if we still don't actually know the Output, then we have
		// to do things the slow way and search every arrangementOnlyClip
		if (outputClipInstanceListIsCurrentlyInvalid || !output) {
			clipArray = &project.clips.arrangement_only_clips;
			goto decideNumElements;
		}

		// Or more ideally, we would have found the Output along the way, so we can just grab its arrangement-only
		// Clips directly from it
		else {
			doingClipsProvidedByOutput = true;
			numElements = std::ssize(output->clipInstances);
			goto traverseClips;
		}
	}
}

void Song::setupPatchingForAllParamManagersForInstrument(SoundInstrument* sound) {

	// If currently swapping an Instrument, it can't be assumed that all arranger-only Clips for this Instrument are
	// in its clipInstances, which otherwise is a nice time-saver

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithModControllable* modelStack = setupModelStackWithSong(modelStackMemory, this)
	                                                ->addTimelineCounter(nullptr)
	                                                ->addModControllableButNoNoteRow(sound);

	// For each Clip in session and arranger for specific Output
	int32_t numElements = std::ssize(project.clips.session_clips);
	bool doingArrangementClips = false;
	// TODO: Should use AllClips, but less obvious so later.
traverseClips:
	for (int32_t c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = project.clips.session_clips[c];
			if (clip->output != sound) {
				continue;
			}
		}
		else {
			ClipInstance* clipInstance = &sound->clipInstances[c];
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
		numElements = std::ssize(sound->clipInstances);
		goto traverseClips;
	}
}

void Song::grabVelocityToLevelFromMIDICableAndSetupPatchingForAllParamManagersForInstrument(
    MIDICable& cable, SoundInstrument* instrument) {

	if (!cable.hasDefaultVelocityToLevelSet()) {
		return;
	}

	// TODO: backed up ParamManagers?

	// If currently swapping an Instrument, it can't be assumed that all arranger-only Clips for this Instrument are
	// in its clipInstances, which otherwise is a nice time-saver

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithModControllable* modelStack = setupModelStackWithSong(modelStackMemory, this)
	                                                ->addTimelineCounter(nullptr)
	                                                ->addModControllableButNoNoteRow(instrument);

	// For each Clip in session and arranger for specific Output
	int32_t numElements = std::ssize(project.clips.session_clips);
	bool doingArrangementClips = false;
	// TODO: Should use AllClips, but less obvious so later.
traverseClips:
	for (int32_t c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = project.clips.session_clips[c];
			if (clip->output != instrument) {
				continue;
			}
		}
		else {
			ClipInstance* clipInstance = &instrument->clipInstances[c];
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

		patchCableSet->grabVelocityToLevelFromMIDICable(cable);
		patchCableSet->setupPatching(modelStackWithParamCollection);
	}
	if (!doingArrangementClips) {
		doingArrangementClips = true;
		numElements = std::ssize(instrument->clipInstances);
		goto traverseClips;
	}
}

// kit is required, fortunately. Unlike some of the other "for drum" functions here.
void Song::grabVelocityToLevelFromMIDICableAndSetupPatchingForAllParamManagersForDrum(MIDICable& cable, SoundDrum* drum,
                                                                                      Kit* kit) {

	if (!cable.hasDefaultVelocityToLevelSet()) {
		return;
	}

	// TODO: backed up ParamManagers?

	// If currently swapping an Instrument, it can't be assumed that all arranger-only Clips for this Instrument are
	// in its clipInstances, which otherwise is a nice time-saver

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

	// For each Clip in session and arranger for specific Output
	int32_t numElements = std::ssize(project.clips.session_clips);
	bool doingArrangementClips = false;
	// TODO: Should use AllClips but less obvious, so later.
traverseClips:
	for (int32_t c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = project.clips.session_clips[c];
			if (clip->output != kit) {
				continue;
			}
		}
		else {
			ClipInstance* clipInstance = &kit->clipInstances[c];
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

		patchCableSet->grabVelocityToLevelFromMIDICable(cable);
		patchCableSet->setupPatching(modelStackWithParamCollection);
	}
	if (!doingArrangementClips) {
		doingArrangementClips = true;
		numElements = std::ssize(kit->clipInstances);
		goto traverseClips;
	}
}

void Song::grabVelocityToLevelFromMIDICableAndSetupPatchingForEverything(MIDICable& cable) {
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
			if (synth->midiInput.containsSomething() && synth->midiInput.cable == &cable) {

				ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
				    modelStackWithTimelineCounter->addModControllableButNoNoteRow(synth)->addParamManager(
				        &clip->paramManager);

				ModelStackWithParamCollection* modelStackWithParamCollection =
				    clip->paramManager.getPatchCableSet(modelStackWithThreeMainThings);

				PatchCableSet* patchCableSet = (PatchCableSet*)modelStackWithParamCollection->paramCollection;
				patchCableSet->grabVelocityToLevelFromMIDICable(cable);

				patchCableSet->setupPatching(modelStackWithParamCollection);
			}
		}

		else if (output->type == OutputType::KIT) {
			Kit* kit = (Kit*)output;

			for (Drum* drum = kit->firstDrum; drum; drum = drum->next) {
				if (drum->type == DrumType::SOUND && drum->midiInput.containsSomething()
				    && drum->midiInput.cable == &cable) {

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

					patchCableSet->grabVelocityToLevelFromMIDICable(cable);
					patchCableSet->setupPatching(modelStackWithParamCollection);
				}
			}
		}
	}
}

Scale Song::cycleThroughScales() {
	auto startScale = project.scale.key.getCurrentScale();
	auto currentScale = startScale;
	auto newScale = currentScale;
	// Try next scale until one works, or we've tried all.
	// NUM_PRESET_SCALES stands for the user scale.
	do {
		newScale = static_cast<Scale>(mod(newScale + 1, NUM_PRESET_SCALES + 1));
		if (newScale == USER_SCALE || !project.scale.disabled_preset_scales[newScale]) {
			currentScale = setScale(newScale);
		}
	} while (newScale != currentScale && newScale != startScale);
	return newScale;
}

/// Returns CUSTOM_SCALE_WITH_MORE_THAN_7_NOTES we can't use the newScale
Scale Song::setScale(Scale newScale) {
	// Make sure newScale is a legal one
	if (0 <= newScale && newScale < NUM_PRESET_SCALES && setScaleNotes(presetScaleNotes[newScale])) {
		return newScale;
	}
	else if (newScale == NUM_PRESET_SCALES && !project.scale.user_scale_notes.isEmpty()
	         && setScaleNotes(project.scale.user_scale_notes)) {
		return newScale;
	}
	else {
		return NO_SCALE;
	}
}

bool Song::setScaleNotes(NoteSet newScaleNotes) {
	newScaleNotes.add(0); // ensure root

	// Always count the root note as present, to avoid changing the root note when cycling scales.
	//
	// Note: it is important we always use the actual notes, instead of eg. picking all scale notes
	// when transforming between scales of same size - otherwise scale transformations don't necessarily
	// remain reversible across sizes, even if the user didn't change the notes used.
	NoteSet notesWithinOctavePresent = notesInScaleModeClips();
	notesWithinOctavePresent.add(0);

	// If the new scale cannot fit the notes from the old one, we can't change scale
	if (notesWithinOctavePresent.scaleSize() > newScaleNotes.scaleSize()) {
		return false;
	}

	ScaleChange changes;
	project.scale.scale_mapper.computeChangeFrom(notesWithinOctavePresent, project.scale.key.modeNotes, newScaleNotes,
	                                             changes);

	replaceMusicalMode(changes, true);

	project.scale.key.modeNotes = newScaleNotes;

	// Save possible user scale
	if (isUserScale(newScaleNotes)) {
		project.scale.user_scale_notes = newScaleNotes;
	}

	return true;
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
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    setupModelStackWithSongAsTimelineCounter(modelStackMemory);
	auto tempoParam = getModelStackWithParam(modelStackWithThreeMainThings, params::UnpatchedGlobal::UNPATCHED_TEMPO);
	// record it with accuracy of .01. Max tempo is about 20 000bpm so this should fit fine
	auto intTempo = (int32_t)(tempoBPM * 100);
	int32_t pos = -1; // means use the live position
	tempoParam->autoParam->setCurrentValueInResponseToUserInput(intTempo, tempoParam, shouldLogAction, pos);
	setBPMInner(tempoBPM, shouldLogAction);
}

void Song::clearTempoAutomation() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    setupModelStackWithSongAsTimelineCounter(modelStackMemory);
	auto tempoParam = getModelStackWithParam(modelStackWithThreeMainThings, params::UnpatchedGlobal::UNPATCHED_TEMPO);
	// record it with accuracy of .01. Max tempo is about 20 000bpm so this should fit fine
	int32_t pos = -1; // means use the live position
	Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, ActionAddition::ALLOWED);
	tempoParam->autoParam->deleteAutomation(action, tempoParam);
	display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_CLEARED));
}

void Song::setBPMInner(float tempoBPM, bool shouldLogAction) {
	if (project.transport.inside_world_tick_magnitude > 0) {
		tempoBPM *= ((uint32_t)1 << (project.transport.inside_world_tick_magnitude));
	}
	double timePerTimerTick = (double)110250 / tempoBPM;
	if (project.transport.inside_world_tick_magnitude < 0) {
		timePerTimerTick *= ((uint32_t)1 << (-project.transport.inside_world_tick_magnitude));
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

	// M2-G S2b: the clip's tree-resident param store (if any) is retired with the clip. Capture the key now while
	// the clip is valid; erase the entry after the clip (and its bound ParamSets) are destructed below.
	std::optional<uint32_t> treeParamId = clip->serialId();

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

	if (treeParamId.has_value()) {
		project.clip_params.remove(*treeParamId);
	}
}

int32_t Song::getMaxMIDIChannelSuffix(int32_t channel) {
	if (channel >= 16) {
		return -1; // MPE zones - just don't do freakin suffixes?
	}

	bool* inUse = (bool*)shortStringBuffer; // Only actually needs to be 27 long
	memset(inUse, 0, 27);

	int32_t maxSuffix = -2;

	for (Output* output = project.media.first_output; output; output = output->next) {
		if (output->type == OutputType::MIDI_OUT) {
			MIDIInstrument* instrument = (MIDIInstrument*)output;
			if (instrument->getChannel() == channel) {
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
	for (int32_t c = 0; c < std::ssize(project.clips.session_clips); c++) {
		Clip* clip = project.clips.session_clips[c];

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
			for (int32_t l = 0; l < std::ssize(project.clips.session_clips); l++) {
				Clip* loopable = project.clips.session_clips[l];
				if (loopable->launchStyle == LaunchStyle::DEFAULT
				    && currentSong->project.arrangement.sections[loopable->section].num_repetitions != -2) {
					loopable->armState = ArmState::OFF;
				}
			}
		}
		return;
	}

	// Stop all other playing-but-not-soloing Clips, and turn all soloing Clip into playing Clip!
	// For each Clip in session
	for (int32_t c = 0; c < std::ssize(project.clips.session_clips); c++) {
		Clip* clip = project.clips.session_clips[c];

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
	return (float)project.transport.samples_per_timer_tick_big / 4294967296;
}

uint32_t Song::getTimePerTimerTickRounded() {
	return (uint64_t)((uint64_t)project.transport.samples_per_timer_tick_big + (uint64_t)(uint32_t)2147483648uL)
	       >> 32; // Rounds
}

void Song::addOutput(Output* output, bool atStart) {

	if (atStart) {
		output->next = project.media.first_output;
		project.media.first_output = output;

		persistentView.arrangementYScroll++;
	}
	else {
		Output** prevPointer = &project.media.first_output;
		while (*prevPointer) {
			prevPointer = &(*prevPointer)->next;
		}
		*prevPointer = output;
		output->next = nullptr;
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
	for (prevPointer = &project.media.first_output; *prevPointer != output; prevPointer = &(*prevPointer)->next) {
		if (*prevPointer == nullptr) {
			return -1; // Shouldn't be necessary, but better to safeguard
		}

		if ((*prevPointer)->soloingInArrangementMode) {
			seenAnyOtherSoloing = true;
		}
		outputIndex++;
	}

	*prevPointer = output->next;

	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;

	int32_t bottomYDisplay = -persistentView.arrangementYScroll;
	int32_t topYDisplay = bottomYDisplay + getNumOutputs();

	bottomYDisplay = std::max(0_i32, bottomYDisplay);
	topYDisplay = std::min(kDisplayHeight - 1, topYDisplay);

	int32_t yDisplay = outputIndex - persistentView.arrangementYScroll;

	if (yDisplay - bottomYDisplay < topYDisplay - yDisplay) {
		persistentView.arrangementYScroll--;
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
	for (Output* thisOutput = project.media.first_output; thisOutput; thisOutput = thisOutput->next) {
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
	for (prevPointer = &project.media.first_output; *prevPointer != oldOutput; prevPointer = &(*prevPointer)->next) {}
	newOutput->next = oldOutput->next;
	*prevPointer = oldOutput->next;

	Clip* favourClipForCloningParamManager = nullptr;

	// Migrate input MIDI channel / device. Putting this up here before any calls to changeInstrument() is good,
	// because then if a default velocity is set, for the MIDIDevice, that gets grabbed by the Clip's ParamManager
	// during that call.
	((Instrument*)newOutput)->midiInput = ((Instrument*)oldOutput)->midiInput;
	((Instrument*)oldOutput)->midiInput.clear();

	// if we're replacing the same output type
	if (newOutput->type == oldOutput->type) {
		// exclude MIDI and CV instruments from this
		// only audio instruments have a midi knob array
		if (newOutput->type == OutputType::SYNTH || newOutput->type == OutputType::KIT) {
			// Migrate any midi learned params for synth and kit clip preset changes
			// For kit clips it will migrate only kit affect entire midi learned params
			// Scenario:
			// - you create a clip
			// - you midi learn a controller to that clip's params
			// - you then go to change the preset for that clip
			// - you expect that you can continue controlling the same params for the new preset
			auto* old_mca = static_cast<ModControllableAudio*>(oldOutput->toModControllable());
			if (old_mca != nullptr) {
				size_t num_knobs = old_mca->midi_knobs.size();
				if (num_knobs > 0) {
					auto& new_mca = static_cast<ModControllableAudio&>(*newOutput->toModControllable());
					new_mca.midi_knobs.clear();
					std::swap(new_mca.midi_knobs, old_mca->midi_knobs);
					old_mca->ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(this);
				}
			}
		}
	}

	Output* outputRecordingOldOutput = oldOutput->getOutputRecordingThis();

	outputClipInstanceListIsCurrentlyInvalid = true;

	// Tell all the Clips to change their Instrument.
	// For each Clip in session and arranger for specific Output...
	int32_t numElements = std::ssize(project.clips.session_clips);
	bool doingArrangementClips = false;
	// TODO: Should use AllClips, but less obvious so later.
traverseClips:
	for (int32_t c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = project.clips.session_clips[c];
			if (clip->output != oldOutput) {
				continue;
			}
		}
		else {
			ClipInstance* clipInstance = &oldOutput->clipInstances[c];
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
			    modelStackWithTimelineCounter, newOutput, nullptr, InstrumentRemoval::NONE,
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
		numElements = std::ssize(oldOutput->clipInstances);
		goto traverseClips;
	}

	// Migrate all ClipInstances from oldInstrument to newInstrument
	newOutput->clipInstances.swap(oldOutput->clipInstances);

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
	if (outputRecordingOldOutput) {
		((AudioOutput*)outputRecordingOldOutput)->setOutputRecordingFrom(newOutput);
	}
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
	for (int y = 0; y < 8; y++) {
		auto& m = project.arrangement.session_macros[y];
		if (m.kind == SessionMacroKind::OUTPUT_CYCLE && m.output == output) {
			m.kind = SessionMacroKind::NO_MACRO;
		}
	}

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
	instrument->next = project.media.first_hibernating_instrument;
	project.media.first_hibernating_instrument = instrument;
	instrument->setActiveClip(nullptr); // Just be sure!
	instrument->inValidState = false;
}

void Song::removeInstrumentFromHibernationList(Instrument* instrument) {

	// Remove the Instrument from the list of Instruments of this type
	Instrument** prevPointer;
	for (prevPointer = &project.media.first_hibernating_instrument; *prevPointer != instrument;
	     prevPointer = (Instrument**)&(*prevPointer)->next) {
		if (*prevPointer == nullptr) {
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
	Instrument** prevPointer = &project.media.first_hibernating_instrument;
	while (true) {
		Instrument* instrument = *prevPointer;

		if (instrument == nullptr) {
			return; // Shouldn't be necessary, but better to safeguard
		}

		if (instrument->type == outputType) {

			if (!strcasecmp(name, instrument->name.c_str())) {
				*prevPointer = (Instrument*)instrument->next;

				deleteOutput(instrument);

				return;
			}
		}

		prevPointer = (Instrument**)&instrument->next;
	}
}

void Song::markAllInstrumentsAsEdited() {
	for (Output* output = project.media.first_output; output; output = output->next) {
		if (output->type == OutputType::AUDIO) {
			continue;
		}

		Instrument* instrument = (Instrument*)output;
		bool shouldMoveToEmptySlot = false; // Deprecated. See newer branch.
		instrument->beenEdited(shouldMoveToEmptySlot);
	}
}

// used with the renameOutputUI class to check if you're trying to rename an output to the same
// name as another output
AudioOutput* Song::getAudioOutputFromName(std::string_view name) {
	for (Output* thisOutput = project.media.first_output; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput->type == OutputType::AUDIO) {
			if (deluge::string::caselessEquals(thisOutput->name.c_str(), name)) {
				return (AudioOutput*)thisOutput;
			}
		}
	}
	return nullptr;
}

// You can put name as NULL if it's MIDI or CV
Instrument* Song::getInstrumentFromPresetSlot(OutputType outputType, int32_t channel, int32_t channelSuffix,
                                              char const* name, char const* dirPath, bool searchHibernating,
                                              bool searchNonHibernating) {

	if (searchNonHibernating) {
		for (Output* thisOutput = project.media.first_output; thisOutput; thisOutput = thisOutput->next) {
			bool match = thisOutput->matchesPreset(outputType, channel, channelSuffix, name, dirPath);
			if (match) {
				return (Instrument*)thisOutput;
			}
		}
	}

	if (searchHibernating) {
		for (Output* thisOutput = project.media.first_hibernating_instrument; thisOutput;
		     thisOutput = thisOutput->next) {
			bool match = thisOutput->matchesPreset(outputType, channel, channelSuffix, name, dirPath);
			if (match) {
				return (Instrument*)thisOutput;
			}
		}
	}

	return nullptr;
}

int32_t Song::getOutputIndex(Output* output) {
	int32_t count = 0;
	for (Output* thisOutput = project.media.first_output; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput == output) {
			return count;
		}
		count++;
	}

	return 0; // fail
}

Output* Song::getOutputFromIndex(int32_t index) {
	for (Output* thisOutput = project.media.first_output; thisOutput; thisOutput = thisOutput->next) {
		if (!index) {
			return thisOutput;
		}
		index--;
	}

	return project.media.first_output; // fail
}

void Song::syncSerialIdCountersFromLoaded() {
	const auto advancePast = [](uint32_t& counter, std::optional<uint32_t> id) {
		if (id.has_value() && *id >= counter) {
			counter = *id + 1;
		}
	};
	for (Output* output = project.media.first_output; output != nullptr; output = output->next) {
		advancePast(outputIdCounter_, output->serialId());
	}
	for (Clip* clip : project.clips.session_clips) {
		advancePast(clipIdCounter_, clip->serialId());
	}
	for (Clip* clip : project.clips.arrangement_only_clips) {
		advancePast(clipIdCounter_, clip->serialId());
	}
	for (Output* output = project.media.first_output; output != nullptr; output = output->next) {
		if (output->type == OutputType::KIT) {
			for (Drum* drum = ((Kit*)output)->firstDrum; drum != nullptr; drum = drum->next) {
				advancePast(drumIdCounter_, drum->serialId());
			}
		}
	}
}

Clip* Song::clipForId(uint32_t id) {
	for (Clip* clip : project.clips.session_clips) {
		if (clip->serialId() == id) {
			return clip;
		}
	}
	for (Clip* clip : project.clips.arrangement_only_clips) {
		if (clip->serialId() == id) {
			return clip;
		}
	}
	return nullptr;
}

Output* Song::outputForId(uint32_t id) {
	for (Output* output = project.media.first_output; output != nullptr; output = output->next) {
		if (output->serialId() == id) {
			return output;
		}
	}
	return nullptr;
}

Drum* Song::drumForId(uint32_t id) {
	for (Output* output = project.media.first_output; output != nullptr; output = output->next) {
		if (output->type != OutputType::KIT) {
			continue;
		}
		for (Drum* drum = ((Kit*)output)->firstDrum; drum != nullptr; drum = drum->next) {
			if (drum->serialId() == id) {
				return drum;
			}
		}
	}
	return nullptr;
}

int32_t Song::getNumOutputs() {
	int32_t count = 0;
	for (Output* output = project.media.first_output; output; output = output->next) {
		count++;
	}
	return count;
}

void Song::reassessWhetherAnyOutputsSoloingInArrangement() {
	anyOutputsSoloingInArrangement = false;
	for (Output* output = project.media.first_output; output; output = output->next) {
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
		AudioEngine::routineWithClusterLoading();
		AudioEngine::logAction("aaa4.26");
		((Instrument*)instrumentClip->output)->setupPatching(modelStackWithTimelineCounter);
		AudioEngine::logAction("aaa4.27");
	}
}

void Song::deleteBackedUpParamManagersForClip(Clip* clip) {

	AudioEngine::logAction("Song::deleteBackedUpParamManagersForClip");

	for (auto it = project.param_backup.map.begin(); it != project.param_backup.map.end();) {
		if (it->first.second != clip) {
			++it;
			continue;
		}

		AudioEngine::routineWithClusterLoading();
		ModControllableAudio* modControllable = it->first.first;

		auto itNull = project.param_backup.map.find({modControllable, static_cast<Clip*>(nullptr)});

		// If a no-Clip backup already exists for this ModControllable, replace its contents with this one's
		if (itNull != project.param_backup.map.end()) {
			itNull->second.destructAndForgetParamCollections();
			itNull->second.stealParamCollectionsFrom(&it->second);
			it = project.param_backup.map.erase(it);
		}

		// Otherwise, just re-key this entry to the null Clip. extract() re-links the node without moving the
		// ParamManager, and the re-inserted entry sorts before the current position, so it won't be revisited.
		else {
			auto node = project.param_backup.map.extract(it++);
			node.key().second = nullptr;
			project.param_backup.map.insert(std::move(node));
		}
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

			if (instrument->type == outputType && ((NonAudioInstrument*)instrument)->getChannel() == slot
			    && (outputType == OutputType::CV || ((MIDIInstrument*)instrument)->channelSuffix == subSlot)) {
				return true;
			}
		}
	}

	return false;
}

// This is for non-audio Instruments only, so no name is relevant
bool Song::doesNonAudioSlotHaveClipInSession(OutputType outputType, int32_t slot, int32_t subSlot) {

	// For each Clip in session
	for (Clip* clip : AllClips::inSession(this)) {

		if (clip->type == ClipType::INSTRUMENT) {

			Instrument* instrument = (Instrument*)clip->output;

			if (instrument->type == outputType && ((NonAudioInstrument*)instrument)->getChannel() == slot
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
	for (int32_t i = 0; i < std::ssize(output->clipInstances); i++) {
		ClipInstance* thisInstance = &output->clipInstances[i];
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
	for (int32_t c = 0; c < std::ssize(project.clips.session_clips); c++) {
		Clip* clip = project.clips.session_clips[c];

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

	for (int32_t c = 0; c < std::ssize(project.clips.session_clips); c++) {
		Clip* clip = project.clips.session_clips[c];
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
	int32_t numElements = std::ssize(project.clips.session_clips);
	bool doingArrangementClips = false;
	// TODO: Should use AllClips(), but less obvious so later.
traverseClips:
	for (int32_t c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = project.clips.session_clips[c];
			if (clip->output != output) {
				continue;
			}
		}
		else {
			ClipInstance* clipInstance = &output->clipInstances[c];
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
		numElements = std::ssize(output->clipInstances);
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
	for (Output* thisOutput = project.media.first_output; thisOutput; thisOutput = thisOutput->next) {
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
		for (Output* output = project.media.first_output; output; output = output->next) {

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

		for (int32_t c = 0; c < std::ssize(project.clips.session_clips); c++) {
			Clip* clip = project.clips.session_clips[c];

			if (!(count & 3)) {
				AudioEngine::routineWithClusterLoading();
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
		for (Output* output = project.media.first_output; output; output = output->next) {

			// Don't do any additional searching of session Clips, cos it'd be really inefficient searching all
			// session Clips for each Instrument
			output->pickAnActiveClipIfPossible(modelStack, false, PgmChangeSend::NEVER, false);
		}
	}
	AudioEngine::logAction("aaa5.12");

	int32_t count = 0;

	// And finally, go through session Clips again, giving any more to Instruments that can be given
	for (int32_t c = 0; c < std::ssize(project.clips.session_clips); c++) {
		Clip* clip = project.clips.session_clips[c];
		if (!(count & 7)) {
			AudioEngine::routineWithClusterLoading();
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
	for (Output* output = project.media.first_output; output;) {

		Output* nextOutput = output->next;

		// Also while we're at it, for SoundInstruments (Synths), get them to grab a copy of the arp settings
		if (output->getActiveClip()) {
			if (output->type == OutputType::SYNTH) {
				((SoundInstrument*)output)
				    ->defaultArpSettings.cloneFrom(&((InstrumentClip*)output->getActiveClip())->arpSettings);
			}
			else if (output->type == OutputType::KIT) {
				((Kit*)output)->defaultArpSettings.cloneFrom(&((InstrumentClip*)output->getActiveClip())->arpSettings);
			}
		}

		// Ok, back to the main task - if there's no activeClip...
		else {
			if (output->type == OutputType::SYNTH || output->type == OutputType::KIT) {
				if (!project.param_backup.getPreferablyWithClip((ModControllableAudio*)output->toModControllable(),
				                                                nullptr)) {
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
					if (!project.param_backup.getPreferablyWithClip(soundDrum,
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
	Clip* longestClip = nullptr;

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
	Clip* foundClip = nullptr;
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

	project.media.hibernating_midi_instrument = newInstrument;
}

void Song::deleteHibernatingMIDIInstrument() {
	if (project.media.hibernating_midi_instrument) {
		void* toDealloc = dynamic_cast<void*>(project.media.hibernating_midi_instrument);
		project.media.hibernating_midi_instrument->~MIDIInstrument();
		delugeDealloc(toDealloc);
		project.media.hibernating_midi_instrument = nullptr;
	}
}

MIDIInstrument* Song::grabHibernatingMIDIInstrument(int32_t channel, int32_t channelSuffix) {
	MIDIInstrument* toReturn = project.media.hibernating_midi_instrument;
	project.media.hibernating_midi_instrument = nullptr;
	if (toReturn) {
		toReturn->setActiveClip(nullptr, PgmChangeSend::NEVER); // Not really necessary?
		toReturn->inValidState = false;

		toReturn->setChannel(channel);
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

	for (Output* output = project.media.first_output; output; output = output->next) {
		output->stopAnyAuditioning(modelStack);
	}
}

void Song::ensureAllInstrumentsHaveAClipOrBackedUpParamManager(char const* errorMessageNormal,
                                                               char const* errorMessageHibernating) {

#if ALPHA_OR_BETA_VERSION

	// Non-hibernating Instruments
	for (Output* thisOutput = project.media.first_output; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput->type != OutputType::SYNTH && thisOutput->type != OutputType::KIT) {
			continue;
		}

		AudioEngine::routineWithClusterLoading();

		// If has Clip, that's fine
		if (getClipWithOutput(thisOutput)) {}

		else {
			if (!project.param_backup.getPreferablyWithClip((ModControllableAudio*)thisOutput->toModControllable(),
			                                                nullptr)) {
				FREEZE_WITH_ERROR(errorMessageNormal);
			}
		}
	}

	// And hibernating Instruments
	for (Instrument* thisInstrument = project.media.first_hibernating_instrument; thisInstrument;
	     thisInstrument = (Instrument*)thisInstrument->next) {
		if (thisInstrument->type != OutputType::SYNTH && thisInstrument->type != OutputType::KIT) {
			continue;
		}

		AudioEngine::routineWithClusterLoading();

		// If has Clip, it shouldn't!
		if (getClipWithOutput(thisInstrument)) {
			// gtridr got, V4.0.0-beta2. Before I fixed memory corruption issues, so hopefully could just be that.
			FREEZE_WITH_ERROR("E056");
		}

		else {
			if (!project.param_backup.getPreferablyWithClip((ModControllableAudio*)thisInstrument->toModControllable(),
			                                                nullptr)) {
				FREEZE_WITH_ERROR(errorMessageHibernating);
			}
		}
	}
#endif
}

Error Song::placeFirstInstancesOfActiveClips(int32_t pos) {

	// For each Clip in session
	for (int32_t c = 0; c < std::ssize(project.clips.session_clips); c++) {
		Clip* clip = project.clips.session_clips[c];

		if (isClipActive(clip)) {
			int32_t clipInstanceI = std::ssize(clip->output->clipInstances);
			Error error = clip->output->clipInstances.insertAt(clipInstanceI).error_or(Error::NONE);
			if (error != Error::NONE) {
				return error;
			}

			ClipInstance* clipInstance = &clip->output->clipInstances[clipInstanceI];
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
	for (int32_t c = 0; c < std::ssize(project.clips.session_clips); c++) {
		Clip* clip = project.clips.session_clips[c];

		if (isClipActive(clip)) {

			Clip* clipNow = clip->getClipToRecordTo();

			if (detachClipsToo) {
				clipNow->beingRecordedFromClip = nullptr;
			}

			int32_t clipInstanceI = clip->output->clipInstances.firstAtOrAfter(pos + 1) - 1;
			if (clipInstanceI >= 0) {
				ClipInstance* clipInstance = &clip->output->clipInstances[clipInstanceI];
				if (clipInstance->clip == clipNow) {
					int32_t newLength = pos - clipInstance->pos;
					if (newLength == 0) {
						clip->output->clipInstances.erase(clip->output->clipInstances.begin() + clipInstanceI);
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
	for (int32_t c = 0; c < std::ssize(project.clips.session_clips); c++) {
		Clip* originalClip = project.clips.session_clips[c];

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

	for (Output* thisOutput = project.media.first_output; thisOutput; thisOutput = thisOutput->next) {
		int32_t i = thisOutput->clipInstances.firstAtOrAfter(pos);

		// We go through deleting the ClipInstances one by one. This is actually quite inefficient, but complicated
		// to improve on because the deletion of the Clips themselves, where there are arrangement-only ones, causes
		// the calling of output->pickAnActiveClipIfPossible. So we have to ensure that extra ClipInstances don't
		// exist at any instant in time, or else it'll look at those to pick the new activeClip, which might not
		// exist anymore.
		for (int32_t j = std::ssize(thisOutput->clipInstances) - 1; j >= i; j--) {
			ClipInstance* clipInstance = &thisOutput->clipInstances[j];
			if (action) {
				action->recordClipInstanceExistenceChange(thisOutput, clipInstance, ExistenceChangeType::DELETE);
			}
			Clip* clip = clipInstance->clip;
			thisOutput->clipInstances.erase(thisOutput->clipInstances.begin() + j);

			deletingClipInstanceForClip(thisOutput, clip, action,
			                            true); // Could be bad that this calls the audio routine before we've
			                                   // actually deleted the ClipInstances...
		}

		// Shorten the previous one if need be
		int32_t numElements = std::ssize(thisOutput->clipInstances);
		if (numElements) {
			ClipInstance* clipInstance = &thisOutput->clipInstances[numElements - 1];
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

		void* memory = nullptr;

		bool deletionDone = false;

		if (action) {
			deletionDone = action->recordClipExistenceChange(this, &project.clips.arrangement_only_clips, clip,
			                                                 ExistenceChangeType::DELETE);
			// That call will call pickAnActiveClipIfPossible() whether we like it or not...
		}

		if (!deletionDone) { // If not enough memory to create undo-history...
			actionLogger.deleteAllLogs();
			// Delete the actual Clip.
			// Will not remove Instrument from Song. Will call audio routine! That's why we deleted the ClipInstance
			// first
			int32_t index = project.clips.arrangement_only_clips.getIndexForClip(clip);
			if (index != -1) {
				project.clips.arrangement_only_clips.erase(project.clips.arrangement_only_clips.begin()
				                                           + index); // Shouldn't actually ever not be found
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
	for (Output* thisOutput = project.media.first_output; thisOutput; thisOutput = thisOutput->next) {
		if (!thisOutput->clipInstances.empty()) {
			return true;
		}
	}
	return false;
}

void Song::setParamsInAutomationMode(bool newState) {

	if (runtimeView.paramsInAutomationMode == newState) {
		return;
	}

	runtimeView.paramsInAutomationMode = newState;

	UnpatchedParamSet* unpatchedParams = paramManager.getUnpatchedParamSet();

	// If going automated...
	if (newState) {

		// Back up the unautomated values
		for (int32_t p = 0; p < params::kMaxNumUnpatchedParams; p++) {
			project.global_fx.unautomated_param_values[p] = unpatchedParams->params[p].getCurrentValue();
		}
	}

	// Or if going un-automated
	else {

		// Restore the unautomated values, where automation is present
		for (int32_t p = 0; p < params::kMaxNumUnpatchedParams; p++) {
			if (unpatchedParams->params[p].isAutomated(*unpatchedParams->docForParam(p))) {
				unpatchedParams->params[p].currentValue = project.global_fx.unautomated_param_values[p];
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
		return (getClipWithOutput(clip->output, false, clip) == nullptr);
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

		int32_t oldChannel = oldNonAudioInstrument->getChannel();
		int32_t newChannel = oldNonAudioInstrument->getChannel();

		int32_t oldChannelSuffix, newChannelSuffix;
		if (outputType == OutputType::MIDI_OUT) {
			oldChannelSuffix = ((MIDIInstrument*)oldNonAudioInstrument)->channelSuffix;
			newChannelSuffix = ((MIDIInstrument*)oldNonAudioInstrument)->channelSuffix;
		}

		// CV
		if (outputType == OutputType::CV) {
			int channelToSearch = 0;
			Instrument* instrument = nullptr;
			do {
				newChannel = CVInstrument::navigateChannels(newChannel, offset);

				if (newChannel == oldChannel) {
					display->displayPopup(l10n::get(l10n::String::STRING_FOR_NO_FREE_CHANNEL_SLOTS_AVAILABLE_IN_SONG));
					return output;
				}
				if (newChannel == CVInstrumentMode::both) {
					// in this case we just need to make sure the one were not about to give up is free
					// there probably should be a gatekeeper managing the cv/gate resources but that's a lot to
					// change and this doesn't matter much
					channelToSearch = oldChannel == 0 ? 1 : 0;
				}
				else {
					channelToSearch = newChannel;
				}
				instrument =
				    currentSong->getInstrumentFromPresetSlot(outputType, channelToSearch, -1, nullptr, nullptr, false);
			} while (instrument != nullptr && instrument != oldInstrument);
		}

		// Or MIDI
		else {

			oldNonAudioInstrument->setChannel(-1); // Get it out of the way

			do {
				// Use +/-1 step regardless of encoder so channel-wrap logic stays correct.
				int32_t step = (offset > 0) ? 1 : -1;
				newChannelSuffix += step;

				// Turned left
				if (offset < 0) {
					if (newChannelSuffix < -1) {
						newChannel = (newChannel + step) & 15;
						newChannelSuffix = currentSong->getMaxMIDIChannelSuffix(newChannel);
					}
				}

				// Turned right
				else {
					if (newChannelSuffix >= 26 || newChannelSuffix > currentSong->getMaxMIDIChannelSuffix(newChannel)) {
						newChannel = (newChannel + step) & 15;
						newChannelSuffix = -1;
					}
				}

				if (newChannel == oldChannel && newChannelSuffix == oldChannelSuffix) {
					oldNonAudioInstrument->setChannel(oldChannel); // Put it back
					display->displayPopup(l10n::get(l10n::String::STRING_FOR_NO_FREE_CHANNEL_SLOTS_AVAILABLE_IN_SONG));
					return output;
				}

			} while (currentSong->getInstrumentFromPresetSlot(outputType, newChannel, newChannelSuffix, nullptr,
			                                                  nullptr, false));

			oldNonAudioInstrument->setChannel(oldChannel); // Put it back, before switching notes off etc
		}

		if (oldNonAudioInstrument->getActiveClip() && playbackHandler.isEitherClockActive()) {
			oldNonAudioInstrument->getActiveClip()->expectNoFurtherTicks(currentSong);
		}

		// Because these are just MIDI / CV instruments and we're changing them for all Clips, we can just change
		// the existing Instrument object!
		oldNonAudioInstrument->setChannel(newChannel);
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
		int32_t i = newInstrument->clipInstances.firstAtOrAfter(arrangement.getLivePos() + 1) - 1;

tryAgain:
		if (i >= 0) {

			ClipInstance* clipInstance = &newInstrument->clipInstances[i];

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

	Instrument* newInstrument = nullptr;

	// MIDI / CV
	if (newOutputType == OutputType::MIDI_OUT || newOutputType == OutputType::CV) {

		int32_t numChannels = (newOutputType == OutputType::MIDI_OUT) ? 16 : kNumCVInstrumentChannels;

		while (true) {

			if (!getInstrumentFromPresetSlot(newOutputType, newSlot, newSubSlot, nullptr, nullptr, false)) {
				break;
			}

			newSlot = (newSlot + 1) % numChannels;
			newSubSlot = -1;

			// If we've searched all channels...
			if (newSlot == oldSlot) {
				display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_AVAILABLE_CHANNELS));
				return nullptr;
			}
		}

		if (newOutputType == OutputType::MIDI_OUT) {
			newInstrument = grabHibernatingMIDIInstrument(newSlot, newSubSlot);
			if (newInstrument) {
				goto gotAnInstrument;
			}
		}
		newInstrument = StorageManager::createNewNonAudioInstrument(newOutputType, newSlot, newSubSlot);
		if (!newInstrument) {
			display->displayError(Error::INSUFFICIENT_RAM);
			return nullptr;
		}

gotAnInstrument: {}
	}

	// Synth or Kit
	else {
		Browser::currentDir = getInstrumentFolder(newOutputType);

		FileItem* fileItem = D_TRY_CATCH(loadInstrumentPresetUI.findAnUnlaunchedPresetIncludingWithinSubfolders(
		                                     this, newOutputType, Availability::INSTRUMENT_UNUSED),
		                                 error, {
			                                 display->displayError(error);
			                                 return nullptr;
		                                 });

		newInstrument = fileItem->instrument;
		bool isHibernating = newInstrument && !fileItem->instrumentAlreadyInSong;

		Error error = Error::NONE;
		if (!newInstrument) {
			std::string newPresetName = fileItem->getDisplayNameWithoutExtension();
			error =
			    StorageManager::loadInstrumentFromFile(this, nullptr, newOutputType, false, &newInstrument,
			                                           &fileItem->filePointer, &newPresetName, &Browser::currentDir);
		}

		Browser::emptyFileItems();

		if (error != Error::NONE) {
			display->displayError(error);
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
	ClipArray* clipArray = &project.clips.session_clips;
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
	for (Output* output = project.media.first_output; output; output = output->next) {
		if (output->type == OutputType::AUDIO) {
			return (AudioOutput*)output;
		}
	}
	return nullptr;
}

AudioInputChannel defaultAudioOutputInputChannel = AudioInputChannel::UNSET;

AudioOutput* Song::createNewAudioOutput(Output* replaceOutput) {
	int32_t highestNumber = 0;

	// Find highest number existent so far
	for (Output* output = project.media.first_output; output; output = output->next) {
		if (output->type == OutputType::AUDIO) {
			char const* nameChars = output->name.c_str();
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

	std::string newName;
	newName = "AUDIO";

	newName.append(deluge::string::fromInt(highestNumber + 1));

	ParamManagerForTimeline newParamManager;
	Error error = newParamManager.setupUnpatched();
	if (error != Error::NONE) {
		return nullptr;
	}

	void* outputMemory = deluge::memory::alloc_fast(sizeof(AudioOutput));
	if (!outputMemory) {
		return nullptr;
	}

	auto* newOutput = new (outputMemory) AudioOutput();
	newOutput->name = newName;

	// Set input channel to previously used one. If none selected, see what's in Song
	if (defaultAudioOutputInputChannel == AudioInputChannel::UNSET) {

		defaultAudioOutputInputChannel = AudioInputChannel::LEFT;
		for (Output* output = project.media.first_output; output; output = output->next) {
			if (output->type == OutputType::AUDIO) {
				defaultAudioOutputInputChannel = ((AudioOutput*)output)->inputChannel;
				break;
			}
		}
	}

	newOutput->inputChannel = defaultAudioOutputInputChannel;

	GlobalEffectableForClip::initParamsForAudioClip(&newParamManager);

	project.param_backup.backUp((ModControllableAudio*)newOutput->toModControllable(), nullptr, &newParamManager, true);

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
				newOutput = project.media.first_output;
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
				investigatingOutput = project.media.first_output;
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
	for (prevPointer = &project.media.first_output; *prevPointer != oldOutput; prevPointer = &(*prevPointer)->next) {}
	newOutput->next = oldOutput->next;
	*prevPointer = newOutput;

	// Migrate all ClipInstances from oldInstrument to newInstrument
	newOutput->clipInstances.swap(oldOutput->clipInstances);

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

void Song::getNoteLengthName(etl::istring& buffer, uint32_t noteLength, char const* const notesString,
                             bool clarifyPerColumn) const {
	getNoteLengthNameFromMagnitude(buffer, getNoteMagnitudeFfromNoteLength(noteLength, getInputTickMagnitude()),
	                               notesString, clarifyPerColumn);
}

Instrument* Song::getNonAudioInstrumentToSwitchTo(OutputType newOutputType, Availability availabilityRequirement,
                                                  int16_t newSlot, int8_t newSubSlot,
                                                  bool* instrumentWasAlreadyInSong) {
	int32_t numChannels = (newOutputType == OutputType::MIDI_OUT) ? 16 : kNumCVInstrumentChannels;
	Instrument* newInstrument;
	int32_t oldSlot = newSlot;

	while (true) {

		newInstrument = getInstrumentFromPresetSlot(newOutputType, newSlot, newSubSlot, nullptr, nullptr,
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

		newSlot = (newSlot + 1) % numChannels;
		newSubSlot = -1;

		// If we've searched all channels...
		if (newSlot == oldSlot) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_UNUSED_CHANNELS_AVAILABLE));
			return nullptr;
		}
	}

	*instrumentWasAlreadyInSong = (newInstrument != nullptr);

	// If that didn't work... make a new Instrument to switch to
	if (!newInstrument) {

		if (newOutputType == OutputType::MIDI_OUT) {
			newInstrument = grabHibernatingMIDIInstrument(newSlot, newSubSlot);
			if (newInstrument) {
				goto gotAnInstrument;
			}
		}
		newInstrument = StorageManager::createNewNonAudioInstrument(newOutputType, newSlot, newSubSlot);
		if (!newInstrument) {
			display->displayError(Error::INSUFFICIENT_RAM);
			return nullptr;
		}
	}

gotAnInstrument:
	return newInstrument;
}

void Song::removeSessionClip(Clip* clip, int32_t clipIndex, bool forceClipsAboveToMoveVertically) {

	// If this is the current Clip for the ClipView...
	if (runtimeView.currentClip == clip) {
		runtimeView.currentClip = nullptr;
	}

	// Must unsolo the Clip before we delete it, in case its play-pos needs to be grabbed for another Clip
	if (clip->soloingInSessionMode) {
		session.unsoloClip(clip);
	}

	// See if any instances in arranger
	bool foundAtLeastOneInstanceInArranger = false;
	Output* output = clip->output;

	for (int32_t i = 0; i < std::ssize(output->clipInstances); i++) {
		ClipInstance* clipInstance = &output->clipInstances[i];
		if (clipInstance->clip == clip) {

			int32_t lengthGotUpTo = clipInstance->length;
			int32_t startPos = clipInstance->pos;

			bool deletedAnyElements = false;

lookAtNextOne:
			if (i + 1 < std::ssize(output->clipInstances) && (lengthGotUpTo % clip->loopLength) == 0) {

				// See if next ClipInstance has the same Clip and lines up as a repeat...
				ClipInstance* nextClipInstance = &output->clipInstances[i + 1]; // We already checked that this exists
				if (nextClipInstance->clip == clip && startPos + lengthGotUpTo == nextClipInstance->pos) {

					lengthGotUpTo += nextClipInstance->length;

					// Delete that later ClipInstance
					arrangement.rowEdited(output, nextClipInstance->pos,
					                      nextClipInstance->pos + nextClipInstance->length, clip, nullptr);
					output->clipInstances.erase(output->clipInstances.begin() + i + 1);
					deletedAnyElements = true;
					goto lookAtNextOne;
				}
			}

			if (deletedAnyElements) {
				clipInstance = &output->clipInstances[i]; // Gotta re-get, since storage has changed
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
					arrangement.rowEdited(output, startPos + oldLength, startPos + lengthGotUpTo, nullptr,
					                      clipInstance);
				}
			}

			foundAtLeastOneInstanceInArranger = true;
		}
	}

	int32_t clipYDisplay = clipIndex - persistentView.songViewYScroll;
	int32_t bottomYDisplay = -persistentView.songViewYScroll;
	int32_t topYDisplay = bottomYDisplay + std::ssize(project.clips.session_clips) - 1;
	bottomYDisplay = std::max(bottomYDisplay, 0_i32);
	topYDisplay = std::min(topYDisplay, kDisplayHeight - 1);
	int32_t amountOfStuffAbove = topYDisplay - clipYDisplay;
	int32_t amountOfStuffBelow = clipYDisplay - bottomYDisplay;

	removeSessionClipLowLevel(clip, clipIndex);

	// If there was at least one instance, don't properly delete the Clip - just put it in the arranger only. But do
	// stop it playing!
	if (foundAtLeastOneInstanceInArranger) {

		(void)project.clips.arrangement_only_clips.insertClipAt((InstrumentClip*)clip, 0);
		clip->section = 255;
	}

	// Otherwise, delete as usual
	else {
		deleteClipObject(clip, false, InstrumentRemoval::DELETE_OR_HIBERNATE_IF_UNUSED);
	}

	if (forceClipsAboveToMoveVertically || amountOfStuffAbove > amountOfStuffBelow) {
		persistentView.songViewYScroll--;
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

	for (int y = 0; y < 8; y++) {
		auto& m = project.arrangement.session_macros[y];
		if (m.kind == SessionMacroKind::CLIP_LAUNCH && m.clip == clip) {
			m.kind = SessionMacroKind::NO_MACRO;
		}
	}

	project.clips.session_clips.erase(project.clips.session_clips.begin() + clipIndex);
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
	for (int32_t c = std::ssize(project.clips.session_clips) - 1; c >= 0; c--) {
		Clip* clip = project.clips.session_clips[c];

		if (clip->isPendingOverdub && (!onlyWithOutput || clip->output == onlyWithOutput)) {
			removeSessionClip(clip, c, true);

			if (originalClipIndex && *originalClipIndex > c) {
				(*originalClipIndex)--;
			}

			anyDeleted = true;
		}
		// this isn't actually deleting, but it is clearing a pending overdub which is what matters for the caller
		else if (clip->armState == ArmState::ON_TO_RECORD) {
			clip->armState = ArmState::OFF;
			anyDeleted = true;
		}
	}
	D_PRINTLN("Deleted pending overdubs");
	return anyDeleted;
}

int32_t Song::getYScrollSongViewWithoutPendingOverdubs() {
	int32_t numToSearch =
	    std::min<int32_t>(std::ssize(project.clips.session_clips), persistentView.songViewYScroll + kDisplayHeight);

	int32_t outputValue = persistentView.songViewYScroll;

	for (int32_t i = 0; i < numToSearch; i++) {
		Clip* clip = project.clips.session_clips[i];
		if (clip->isPendingOverdub) {
			outputValue--;
		}
	}

	return outputValue;
}

Clip* Song::getPendingOverdubWithOutput(Output* output) {

	// For each Clip in session
	for (int32_t c = 0; c < std::ssize(project.clips.session_clips); c++) {
		Clip* clip = project.clips.session_clips[c];

		if (clip->isPendingOverdub && clip->output == output) {
			return clip;
		}
	}

	return nullptr;
}

Clip* Song::getClipWithOutputAboutToBeginLinearRecording(Output* output) {

	// For each Clip in session
	for (int32_t c = 0; c < std::ssize(project.clips.session_clips); c++) {
		Clip* clip = project.clips.session_clips[c];

		if (clip->output == output && clip->armState != ArmState::OFF && !isClipActive(clip)
		    && clip->wantsToBeginLinearRecording(this)) {
			return clip;
		}
	}

	return nullptr;
}

Clip* Song::createPendingNextOverdubBelowClip(Clip* clip, int32_t clipIndex, OverDubType newOverdubNature) {
	Clip* newClip = clip; // we're returning the clip to be scheduled, default to this one
	// No automatic overdubs are allowed during soloing, cos that's just too complicated
	if (anyClipsSoloing) {
		return nullptr;
	}
	// if we're in rows or the clip can't support in place overdub then use the traditional deluge cloning looper
	if (persistentView.sessionLayout == SessionLayoutType::SessionLayoutTypeRows || clip->shouldCloneForOverdubs()) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

		newClip = clip->cloneAsNewOverdub(modelStackWithTimelineCounter, newOverdubNature);

		if (newClip && newClip != clip) {
			newClip->overdubNature = newOverdubNature;
			(void)project.clips.session_clips.insertClipAt(newClip, clipIndex);
			if (clipIndex != persistentView.songViewYScroll) {
				persistentView.songViewYScroll++;
			}

			// use root UI in case this is called from performance view
			sessionView.requestRendering(getRootUI());
		}
	}
	else {
		clip->setupOverdubInPlace(newOverdubNature);
	}

	return newClip;
}

bool Song::hasAnyPendingNextOverdubs() {

	// For each Clip in session
	for (int32_t c = 0; c < std::ssize(project.clips.session_clips); c++) {
		Clip* clip = project.clips.session_clips[c];

		if (clip->isPendingOverdub) {
			return true;
		}
	}

	return false;
}

int32_t Song::countAudioClips() const {
	int32_t i = 0;
	for (Output* output = project.media.first_output; output; output = output->next) {
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
	AudioClip* bestClip = nullptr;
	uint64_t lowestImmunity = 0xFFFFFFFFFFFFFFFF;

	for (Output* output = project.media.first_output; output; output = output->next) {
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
	project.clips.session_clips[clipIndex] = newClip;

	if (oldClip == getSyncScalingClip()) {
		project.transport.sync_scaling_clip = newClip;
	}

	if (oldClip == runtimeView.currentClip) {
		runtimeView.currentClip = newClip;
	}

	deleteClipObject(oldClip);
}

Clip* Song::replaceInstrumentClipWithAudioClip(Clip* oldClip, int32_t clipIndex) {

	// Allocate memory for audio clip
	void* clipMemory = deluge::memory::alloc_fast(sizeof(AudioClip));
	if (!clipMemory) {
		return nullptr;
	}

	// Suss output
	AudioOutput* newOutput = createNewAudioOutput();
	if (!newOutput) {
		delugeDealloc(clipMemory);
		return nullptr;
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
		for (int32_t c = 0; c < std::ssize(project.clips.session_clips); c++) {
			Clip* clip = project.clips.session_clips[c];

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

	project.groove.swing_interval = newValue;

	if (playbackHandler.isInternalClockActive()) {

		int32_t leftShift = 10 - project.groove.swing_interval;
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

uint32_t Song::getSixteenthNoteLength() {
	return increaseMagnitude(6, getInputTickMagnitude());
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
	if (persistentView.affectEntire) {
		modelStack->setTimelineCounter(this);
		modelStack->addOtherTwoThingsButNoNoteRow(&project.global_fx.global_effectable, &paramManager);
	}
	else {
		modelStack->setTimelineCounter(nullptr);
		modelStack->addOtherTwoThingsButNoNoteRow(nullptr, nullptr);
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
	for (Output* output = project.media.first_output; output; output = output->next) {
		if (output->type != OutputType::AUDIO) {
			((Instrument*)output)->defaultVelocity = newDefaultVelocity;
		}
	}

	for (Instrument* instrument = project.media.first_hibernating_instrument; instrument;
	     instrument = (Instrument*)instrument->next) {
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

std::string Song::getSongFullPath() {
	std::string fullPath;
	fullPath.append(project.metadata.dir_path);
	fullPath.append("/");
	fullPath.append(project.metadata.name);
	fullPath.append(".XML");
	return fullPath;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-usage="
void Song::setSongFullPath(const char* fullPath) {
	if (char* filename = strrchr((char*)fullPath, '/')) {
		auto fullPathLength = strlen(fullPath);
		char dir[sizeof(char) * fullPathLength + 1];

		memset(dir, 0, sizeof(char) * fullPathLength + 1);
		strncpy(dir, fullPath, fullPathLength - strlen(filename));

		project.metadata.dir_path = dir;
		project.metadata.name = ++filename;
	}
	else {
		project.metadata.name = fullPath;
	}
}
#pragma GCC diagnostic pop

void Song::midiCableBendRangeUpdatedViaMessage(ModelStack* modelStack, MIDICable& cable, int32_t channelOrZone,
                                               int32_t whichBendRange, int32_t bendSemitones) {

	// Go through all Instruments...
	for (Output* thisOutput = currentSong->project.media.first_output; thisOutput; thisOutput = thisOutput->next) {
		thisOutput->offerBendRangeUpdate(modelStack, cable, channelOrZone, whichBendRange, bendSemitones);
	}
}

Error Song::addInstrumentsToFileItems(OutputType outputType) {
	bool doingHibernatingOnes;
	Output* thisOutput;
	if (false) {
doHibernatingInstruments:
		thisOutput = project.media.first_hibernating_instrument;
		doingHibernatingOnes = true;
	}
	else {
		thisOutput = project.media.first_output;
		doingHibernatingOnes = false;
	}

	// First, check all other Instruments in memory, in case the next one in line isn't saved
	for (; thisOutput; thisOutput = thisOutput->next) {

		if (thisOutput->type != outputType) {
			continue;
		}

		Instrument* thisInstrument = (Instrument*)thisOutput;

		// If different path, it's not relevant.
		if (!(thisInstrument->dirPath == Browser::currentDir)) {
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

void Song::getCurrentRootNoteAndScaleName(etl::istring& buffer) {
	char noteName[5];
	int32_t isNatural = 1; // gets modified inside noteCodeToString to be 0 if sharp.
	noteCodeToString(currentSong->project.scale.key.rootNote, noteName, &isNatural);

	buffer.append(noteName);
	if (display->haveOLED()) {
		buffer.append(" ");
		buffer.append(getScaleName(project.scale.key.getCurrentScale()));
	}
}

void Song::displayCurrentRootNoteAndScaleName() {
	etl::string<40> popupMsg;
	getCurrentRootNoteAndScaleName(popupMsg);
	if (display->haveOLED()) {
		UI* currentUI = getCurrentUI();
		bool isSessionView = (currentUI == &sessionView || currentUI == &arrangerView);
		// only display pop-up if we're using 7SEG or we're not currently in Song / Arranger View
		if (isSessionView && !deluge::hid::display::OLED::isPermanentPopupPresent()) {
			sessionView.displayCurrentRootNoteAndScaleName(deluge::hid::display::OLED::main, popupMsg, true);
			deluge::hid::display::OLED::markChanged();
			return;
		}
	}
	display->displayPopup(popupMsg.c_str());
}

void Song::commandTranspose(int32_t interval) {
	if (Buttons::isShiftButtonPressed()) {
		adjustMasterTransposeInterval(interval);
	}
	else {
		transpose(interval);
	}
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
	etl::string<40> popupMsg;

	if (display->haveOLED()) {
		popupMsg.append("Transpose Interval: \n");
		if (masterTransposeInterval == 0) {
			popupMsg.append("Encoder");
		}
		else {
			deluge::string::appendInt(popupMsg, masterTransposeInterval);
			popupMsg.append(" Semitones");
		}
	}
	else {
		if (masterTransposeInterval == 0) {
			popupMsg.append("ENC");
		}
		else {
			deluge::string::appendInt(popupMsg, masterTransposeInterval);
		}
	}
	display->displayPopup(popupMsg.c_str());
}

ModelStackWithThreeMainThings* Song::setupModelStackWithSongAsTimelineCounter(void* memory) {
	return setupModelStackWithThreeMainThingsButNoNoteRow(memory, this, &project.global_fx.global_effectable, this,
	                                                      &paramManager);
}

ModelStackWithTimelineCounter* Song::setupModelStackWithCurrentClip(void* memory) {
	return setupModelStackWithTimelineCounter(memory, this, runtimeView.currentClip);
}

ModelStackWithThreeMainThings* Song::addToModelStack(ModelStack* modelStack) {
	return modelStack->addTimelineCounter(this)->addOtherTwoThingsButNoNoteRow(&project.global_fx.global_effectable,
	                                                                           &paramManager);
}

ModelStackWithAutoParam* Song::getModelStackWithParam(ModelStackWithThreeMainThings* modelStack, int32_t paramID) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (modelStack) {
		modelStackWithParam = modelStack->getUnpatchedAutoParamFromId(paramID);
	}

	return modelStackWithParam;
}

void Song::updateBPMFromAutomation() {
	// There seems to be a param manager bug where it occasionally reports unautomated params as 0 so just ignore
	// that
	int32_t currentTempo = currentSong->paramManager.getUnpatchedParamSet()->getValue(params::UNPATCHED_TEMPO);
	if (currentTempo && currentTempo != intBPM) {
		setBPMInner((float)currentTempo / 100, false);
		intBPM = currentTempo;
	}
}

void Song::changeThresholdRecordingMode(int8_t offset) {
	// have we displayed the current threshold recording mode?
	// if yes, allow user to edit threshold recording mode
	if (display->hasPopupOfType(PopupType::THRESHOLD_RECORDING_MODE)) {
		int8_t newThresholdRecordingMode = util::to_underlying(currentSong->thresholdRecordingMode);
		if (offset < 0) {
			newThresholdRecordingMode = std::clamp(int8_t(newThresholdRecordingMode + offset),
			                                       kFirstThresholdRecordingMode, newThresholdRecordingMode);
		}
		else if (offset > 0) {
			newThresholdRecordingMode = std::clamp(int8_t(newThresholdRecordingMode + offset),
			                                       newThresholdRecordingMode, kLastThresholdRecordingMode);
		}

		currentSong->thresholdRecordingMode = static_cast<ThresholdRecordingMode>(newThresholdRecordingMode);
	}

	displayThresholdRecordingMode();
}

void Song::displayThresholdRecordingMode() {
	etl::string<40> popupMsg;
	if (display->haveOLED()) {
		popupMsg.append("Threshold: ");
	}

	switch (currentSong->thresholdRecordingMode) {
	case ThresholdRecordingMode::OFF:
		popupMsg.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_DISABLED));
		break;

	case ThresholdRecordingMode::LOW:
		popupMsg.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_LOW));
		break;

	case ThresholdRecordingMode::MEDIUM:
		popupMsg.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MEDIUM));
		break;

	case ThresholdRecordingMode::HIGH:
		popupMsg.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_HIGH));
		break;

	default:
		break;
	}

	display->popupText(popupMsg.c_str(), PopupType::THRESHOLD_RECORDING_MODE);
}

/*
    // For each Clip in session and arranger
    ClipArray* clipArray = &sessionClips;
traverseClips:
    for (int32_t c = 0; c < std::ssize(*clipArray); c++) {
        Clip* clip = (*clipArray)[c];

    }
    if (clipArray != &arrangementOnlyClips) { clipArray = &arrangementOnlyClips; goto traverseClips; }



    // For each InstrumentClip in session and arranger
    ClipArray* clipArray = &sessionClips;
traverseClips:
    for (int32_t c = 0; c < std::ssize(*clipArray); c++) {
        Clip* clip = (*clipArray)[c];
        if (clip->type != ClipType::INSTRUMENT) continue;
        Clip* instrumentClip = (Clip*)clip;

    }
    if (clipArray != &arrangementOnlyClips) { clipArray = &arrangementOnlyClips; goto traverseClips; }



    // For each Clip in session
    for (int32_t c = 0; c < std::ssize(sessionClips); c++) {
        Clip* clip = sessionClips[c];

    }


    // For each Clip in arrangement
    for (int32_t c = 0; c < std::ssize(arrangementOnlyClips); c++) {
        Clip* clip = arrangementOnlyClips[c];

    }



    // For each Clip in session and arranger for specific Output
    int32_t numElements = std::ssize(sessionClips);
    bool doingArrangementClips = false;
traverseClips:
    for (int32_t c = 0; c < numElements; c++) {
        Clip* clip;
        if (!doingArrangementClips) {
            clip = sessionClips[c];
            if (clip->output != output) continue;
        }
        else {
            ClipInstance* clipInstance = &output->clipInstances[c];
            if (!clipInstance->clip) continue;
            if (!clipInstance->clip->isArrangementOnlyClip()) continue;
            clip = clipInstance->clip;
        }

    }
    if (!doingArrangementClips) { doingArrangementClips = true; numElements =
std::ssize(output->clipInstances); goto traverseClips; }





    // For each Clip in session and arranger for specific Output - but if currentlySwappingInstrument, use master
list for arranger Clips ClipArray* clipArray = &sessionClips; bool doingClipsProvidedByOutput = false;
decideNumElements: int32_t numElements = std::ssize(*clipArray); traverseClips: for (int32_t c = 0; c <
numElements; c++) { Clip* clip; if (!doingClipsProvidedByOutput) { clip = (*clipArray)[c]; if
(clip->output != output) continue;
        }
        else {
            ClipInstance* clipInstance = &output->clipInstances[c];
            if (!clipInstance->clip) continue;
            if (!clipInstance->clip->isArrangementOnlyClip()) continue;
            clip = clipInstance->clip;
        }

    }
    if (!doingClipsProvidedByOutput && clipArray == &sessionClips) {
        if (currentlySwappingInstrument) { clipArray = &arrangementOnlyClips; goto decideNumElements; }
        else { doingClipsProvidedByOutput = true; numElements = std::ssize(output->clipInstances); goto
traverseClips; }
    }
 */

//    for (Output* output = firstOutput; output; output = output->next) {
