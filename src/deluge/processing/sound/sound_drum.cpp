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

#include "processing/sound/sound_drum.h"
#include "definitions_cxx.hpp"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "mem_functions.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "model/voice/voice.h"
#include "model/voice/voice_vector.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"
#include "util/misc.h"
#include <new>

SoundDrum::SoundDrum() : Drum(DrumType::SOUND), arpeggiator() {
	nameIsDiscardable = false;
}

/*
// Started but didn't finish this - it's hard!
Drum* SoundDrum::clone() {
    void* drumMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(SoundDrum));
    if (!drumMemory) return NULL;
    SoundDrum* newDrum = new (drumMemory) SoundDrum();



    return newDrum;
}
*/

bool SoundDrum::readTagFromFile(Deserializer& reader, char const* tagName) {
	if (!strcmp(tagName, "name")) {
		reader.readTagOrAttributeValueString(&name);
		reader.exitTag("name");
	}
	else if (!strcmp(tagName, "path")) {
		reader.readTagOrAttributeValueString(&path);
		reader.exitTag("path");
	}

	else if (readDrumTagFromFile(reader, tagName)) {}
	else {
		return false;
	}

	return true;
}

bool SoundDrum::allowNoteTails(ModelStackWithSoundFlags* modelStack, bool disregardSampleLoop) {
	return Sound::allowNoteTails(modelStack, disregardSampleLoop);
}

bool SoundDrum::anyNoteIsOn() {
	return Sound::anyNoteIsOn();
}

bool SoundDrum::hasAnyVoices() {
	return Sound::hasAnyVoices(false);
}

void SoundDrum::resetTimeEnteredState() {

	// the sound drum might have multiple voices sounding, but only one will be sustaining and switched to hold
	int32_t ends[2];
	AudioEngine::activeVoices.getRangeForSound(this, ends);
	for (int32_t v = ends[0]; v < ends[1]; v++) {
		Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
		thisVoice->envelopes[0].resetTimeEntered();
	}
}

void SoundDrum::noteOn(ModelStackWithThreeMainThings* modelStack, uint8_t velocity, Kit* kit, int16_t const* mpeValues,
                       int32_t fromMIDIChannel, uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate) {

	// If part of a Kit, and in choke mode, choke other drums
	if (polyphonic == PolyphonyMode::CHOKE) {
		kit->choke();
	}

	Sound::noteOn(modelStack, &arpeggiator, kNoteForDrum, mpeValues, sampleSyncLength, ticksLate, samplesLate, velocity,
	              fromMIDIChannel);
}
void SoundDrum::noteOff(ModelStackWithThreeMainThings* modelStack, int32_t velocity) {
	Sound::allNotesOff(modelStack, &arpeggiator);
}

extern bool expressionValueChangesMustBeDoneSmoothly;

void SoundDrum::expressionEvent(int32_t newValue, int32_t whichExpressionDimension) {

	int32_t s = whichExpressionDimension + util::to_underlying(PatchSource::X);

	// sourcesChanged |= 1 << s; // We'd ideally not want to apply this to all voices though...

	int32_t ends[2];
	AudioEngine::activeVoices.getRangeForSound(this, ends);
	for (int32_t v = ends[0]; v < ends[1]; v++) {
		Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
		if (expressionValueChangesMustBeDoneSmoothly) {
			thisVoice->expressionEventSmooth(newValue, s);
		}
		else {
			thisVoice->expressionEventImmediate(this, newValue, s);
		}
	}

	// Must update MPE values in Arp too - useful either if it's on, or if we're in true monophonic mode - in either
	// case, we could need to suddenly do a note-on for a different note that the Arp knows about, and need these MPE
	// values.
	arpeggiator.arpNote.mpeValues[whichExpressionDimension] = newValue >> 16;
}

void SoundDrum::polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t whichExpressionDimension,
                                                         int32_t channelOrNoteNumber,
                                                         MIDICharacteristic whichCharacteristic) {
	// Because this is a Drum, we disregard the noteCode (which is what channelOrNoteNumber always is in our case - but
	// yeah, that's all irrelevant.
	expressionEvent(newValue, whichExpressionDimension);
}

void SoundDrum::unassignAllVoices() {
	Sound::unassignAllVoices();
}

void SoundDrum::setupPatchingForAllParamManagers(Song* song) {
	song->setupPatchingForAllParamManagersForDrum(this);
}

Error SoundDrum::loadAllSamples(bool mayActuallyReadFiles) {
	return Sound::loadAllAudioFiles(mayActuallyReadFiles);
}

void SoundDrum::prepareForHibernation() {
	Sound::prepareForHibernation();
}
void SoundDrum::writeToFileAsInstrument(StorageManager& bdsm, bool savingSong, ParamManager* paramManager) {
	Serializer& writer = GetSerializer();
	writer.writeOpeningTagBeginning("sound");
	writer.writeFirmwareVersion();
	writer.writeEarliestCompatibleFirmwareVersion("4.1.0-alpha");
	Sound::writeToFile(writer, savingSong, paramManager, &arpSettings, NULL);

	if (savingSong) {
		Drum::writeMIDICommandsToFile(writer);
	}

	writer.writeClosingTag("sound");
}

void SoundDrum::writeToFile(Serializer& writer, bool savingSong, ParamManager* paramManager) {
	writer.writeOpeningTagBeginning("sound");
	writer.writeAttribute("name", name.get());

	Sound::writeToFile(writer, savingSong, paramManager, &arpSettings, path.get());

	if (savingSong) {
		Drum::writeMIDICommandsToFile(writer);
	}

	writer.writeClosingTag("sound");
}

void SoundDrum::getName(char* buffer) {
}

Error SoundDrum::readFromFile(Deserializer& reader, Song* song, Clip* clip, int32_t readAutomationUpToPos) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithModControllable* modelStack =
	    setupModelStackWithSong(modelStackMemory, song)->addTimelineCounter(clip)->addModControllableButNoNoteRow(this);

	return Sound::readFromFile(reader, modelStack, readAutomationUpToPos, &arpSettings);
}

// modelStack may be NULL
void SoundDrum::choke(ModelStackWithSoundFlags* modelStack) {
	if (polyphonic == PolyphonyMode::CHOKE) {

		// Don't choke it if it's auditioned
		if ((getRootUI() == &instrumentClipView || getRootUI() == &automationView)
		    && instrumentClipView.isDrumAuditioned(this)) {
			return;
		}

		// Ok, choke it
		fastReleaseAllVoices(modelStack); // Accepts NULL
	}
}

void SoundDrum::setSkippingRendering(bool newSkipping) {
	if (kit && newSkipping != skippingRendering) {
		if (newSkipping) {
			kit->drumsWithRenderingActive.deleteAtKey((int32_t)(Drum*)this);
		}
		else {
			kit->drumsWithRenderingActive.insertAtKey((int32_t)(Drum*)this);
		}
	}

	Sound::setSkippingRendering(newSkipping);
}

uint8_t* SoundDrum::getModKnobMode() {
	return &kit->modKnobMode;
}

void SoundDrum::drumWontBeRenderedForAWhile() {
	Sound::wontBeRenderedForAWhile();
}

ArpeggiatorBase* SoundDrum::getArp() {
	return &arpeggiator;
}
