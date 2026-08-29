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

#include "storage/storage_manager.h"
#include "definitions_cxx.hpp"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "io/file.hpp"
#include "libdeluge/block_device.h"
#include "libdeluge/control_surface.h"
#include "libdeluge/file_io.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/favourite/favourite_manager.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/file_item.h"
#include "storage/owner.h"

#include "etl/string.h"
#include "util/firmware_version.h"
#include "util/functions.h"
#include "util/try.h"
#include "version.h"
#include <string.h>

#include "libdeluge/display.h"

extern "C" {
#include <scheduler_api.h>
}

FirmwareVersion song_firmware_version = FirmwareVersion::current();
PLACE_SDRAM_BSS XMLSerializer smSerializer;
PLACE_SDRAM_BSS XMLDeserializer smDeserializer;
PLACE_SDRAM_BSS JsonSerializer smJsonSerializer;
PLACE_SDRAM_BSS JsonDeserializer smJsonDeserializer;
FileDeserializer* activeDeserializer = &smDeserializer;

const bool writeJsonFlag = false;

Serializer& GetSerializer() {
	if (writeJsonFlag) {
		return smJsonSerializer;
	}
	else {
		return smSerializer;
	}
}

extern void initialiseConditions();
extern void songLoaded(Song* song);

Error StorageManager::checkSpaceOnCard() {
	uint32_t freeClusters = 0;
	uint32_t totalClusters = 0;
	// Best-effort: if the query can't run (unmounted, off-fiber, or a non-efatfs BSP whose weak stub
	// returns false), don't spuriously report the card full — treat it as space available.
	if (!deluge_efatfs_stats(&freeClusters, &totalClusters)) {
		return Error::NONE;
	}
	D_PRINTLN("free clusters:  %d", freeClusters);
	return freeClusters ? Error::NONE : Error::SD_CARD_FULL;
}

// Creates folders and subfolders as needed!
std::expected<deluge::io::File, Error> StorageManager::createFile(char const* filePath, bool mayOverwrite) {

	Error error = initSD();
	if (error != Error::NONE) {
		return std::unexpected(error);
	}

	error = checkSpaceOnCard();
	if (error != Error::NONE) {
		return std::unexpected(error);
	}

	DelugeFileOpenMode mode = mayOverwrite ? DELUGE_FILE_WRITE_CREATE : DELUGE_FILE_WRITE_CREATE_NEW;

	bool triedCreatingFolder = false;

tryAgain:
	auto opened = deluge::io::File::open(filePath, mode);
	if (!opened) {

processError:
		// If folder doesn't exist, try creating it - once only
		if (opened.error() == deluge::io::Status::NOT_FOUND) {
			if (triedCreatingFolder) {
				return std::unexpected(Error::FOLDER_DOESNT_EXIST);
			}
			triedCreatingFolder = true;

			std::string folderPath;
			folderPath = filePath;

			// Get just the folder path
cutFolderPathAndTryCreating:
			char const* folderPathChars = folderPath.c_str();
			char const* slashAddr = strrchr(folderPathChars, '/');
			if (!slashAddr) {
				return std::unexpected(Error::UNSPECIFIED); // Shouldn't happen
			}
			int32_t slashPos = slashAddr - folderPathChars;

			folderPath.resize(slashPos);

			// Try making the folder
			auto made_dir = deluge::io::mkdir(folderPath.c_str());
			if (made_dir) {
				goto tryAgain;
			}
			// If that folder couldn't be created because its parent folder didn't exist...
			else if (made_dir.error() == deluge::io::Status::NOT_FOUND) {
				triedCreatingFolder = false;      // Let it do multiple tries again
				goto cutFolderPathAndTryCreating; // Go and try creating the parent folder
			}
			else {
				goto processError;
			}
		}

		// The file already exists and mayOverwrite was false.
		else if (opened.error() == deluge::io::Status::EXISTS) {
			return std::unexpected(Error::FILE_ALREADY_EXISTS);
		}

		// Otherwise, just return the appropriate error.
		else {
			error = delugeStatusToError(deluge::io::to_deluge_status(opened.error()));
			if (error == Error::SD_CARD) {
				error = Error::WRITE_FAIL; // Get a bit more specific if we only got the most general error.
			}
			return std::unexpected(error);
		}
	}

	return std::move(opened.value());
}

Error StorageManager::createXMLFile(char const* filePath, XMLSerializer& writer, bool mayOverwrite,
                                    bool displayErrors) {
	auto created = createFile(filePath, mayOverwrite);
	writer.reset();
	if (!created) {
		if (displayErrors) {
			display->removeWorkingAnimation();
			display->displayError(created.error());
		}
		return created.error();
	}
	writer.file = std::move(created.value());
	writer.reset();
	if (!writeJsonFlag) {
		writer.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	}
	return Error::NONE;
}

Error StorageManager::createJsonFile(char const* filePath, JsonSerializer& writer, bool mayOverwrite,
                                     bool displayErrors) {
	auto created = createFile(filePath, mayOverwrite);
	writer.reset();
	if (!created) {
		if (displayErrors) {
			display->removeWorkingAnimation();
			display->displayError(created.error());
		}
		return created.error();
	}
	writer.file = std::move(created.value());
	writer.reset();
	return Error::NONE;
}

// Existence check routed through the deluge::io::File port (efatfs when active, else C-FatFS via the
// selector in file.cpp) - opening for read and letting RAII close it is the FS-agnostic way to ask
// "does this path exist" without reaching for a raw f_stat/f_open.
std::expected<bool, deluge::io::Status> StorageManager::fileExists(char const* pathName) {
	if (Error error = initSD(); error != Error::NONE) {
		// The card is not usable, so existence is unknowable -- NOT absent.
		return std::unexpected{deluge::io::Status::NO_FILESYSTEM};
	}

	return deluge::io::presence_from_open(deluge::io::File::open(pathName, DELUGE_FILE_READ));
}

// Gets ready to access SD card.
// You should call this before you're gonna do any accessing - otherwise any errors won't reflect if there's in fact
// just no card inserted.
Error StorageManager::initSD() {

	// If there's no card present, we're in trouble - check this first, before touching the
	// filesystem, so callers get SD_CARD_NOT_PRESENT (not a generic mount failure) when the card
	// is simply absent. This is the BSP's card-detect line, not a FatFS/efatfs concept.
	if (!deluge_block_ready(deluge_block_sd_unit())) {
		return Error::SD_CARD_NOT_PRESENT;
	}

	if (!deluge_efatfs_mount()) {
		return Error::SD_CARD;
	}
	// The transition is decided by the shared counter, not by watching our own call. It used to be
	// `wasMounted` sampled either side of the mount above, which finds the edge only while this is
	// the ONLY thing that mounts -- and the directory channel's driver now mounts too, on the storage
	// tier, because mounting is card I/O and its caller is on the interaction tier. Every caller of
	// initSD() is somewhere the work can run, hence true.
	noticeCardMount(/*mayRunHere=*/true);
	return Error::NONE;
}

namespace {
/// Owner-op trampoline: the heavy half of a mount notification, on the storage tier.
void runFirstCardReadOp(void*) {
	audioFileManager.firstCardRead();
}
/// How many mount edges have been acted on. See StorageManager::cardMountNotices().
uint32_t g_cardMountNotices = 0;
} // namespace

uint32_t StorageManager::cardMountNotices() {
	return g_cardMountNotices;
}

void StorageManager::noticeCardMount(bool mayRunHere) {
	// Static rather than a member: the counter is a property of the one filesystem, and every caller
	// is asking the same question about it.
	static uint32_t lastSeenMountGeneration = 0;
	// Seeded from the current value the first time, so a device that boots with the card already
	// mounted does not fire a spurious "the card changed" on its first listing.
	static bool seeded = false;
	const uint32_t generation = deluge_efatfs_mount_generation();
	if (!seeded) {
		seeded = true;
		lastSeenMountGeneration = generation;
		return;
	}
	if (generation == lastSeenMountGeneration) {
		return;
	}
	// Taken BEFORE the work, so a second caller arriving while it is queued does not queue it twice.
	lastSeenMountGeneration = generation;
	g_cardMountNotices++;
	if (mayRunHere) {
		audioFileManager.firstCardRead(); // tell the audio file manager that we have a new card
		return;
	}
	// firstCardRead() -> cardReinserted() opens every resident sample to check it survived the swap,
	// which is card I/O and may not happen on the interaction tier. Handing it to the storage owner
	// is a dispatch, not a wait. If the queue is full the edge is genuinely lost, so put the counter
	// back and let the next tick try again rather than dropping the notification.
	if (!deluge::storage::Owner::run(&runFirstCardReadOp, nullptr)) {
		lastSeenMountGeneration = generation - 1;
	}
}

bool StorageManager::checkSDPresent() {
	bool present = deluge_block_ready(deluge_block_sd_unit());
	return present;
}

bool StorageManager::checkSDInitialized() {
	return deluge_block_ready(deluge_block_sd_unit());
}

Error StorageManager::openInstrumentFile(OutputType outputType, char const* path) {

	AudioEngine::logAction("openInstrumentFile");
	char const* firstTagName;
	char const* altTagName = "";

	if (outputType == OutputType::SYNTH) {
		firstTagName = "sound";
		altTagName = "synth"; // Compatibility with old xml files
	}
	else if (outputType == OutputType::MIDI_OUT) {
		firstTagName = "midi";
	}
	else {
		firstTagName = "kit";
	}

	Error error = openXMLFile(path, smDeserializer, firstTagName, altTagName);
	return error;
}

// Returns error status
// clip may be NULL
Error StorageManager::loadInstrumentFromFile(Song* song, InstrumentClip* clip, OutputType outputType,
                                             bool mayReadSamplesFromFiles, Instrument** getInstrument, char const* path,
                                             std::string* name, std::string* dirPath) {

	AudioEngine::logAction("loadInstrumentFromFile");
	D_PRINTLN("opening instrument file -  %s %s  from path  %s", dirPath->c_str(), name->c_str(), path);

	Error error = openInstrumentFile(outputType, path);
	if (error != Error::NONE) {
		D_PRINTLN("opening instrument file failed -  %s", name->c_str());
		return error;
	}

	AudioEngine::logAction("loadInstrumentFromFile");
	Instrument* newInstrument = createNewInstrument(outputType);

	if (!newInstrument) {
		smDeserializer.closeWriter();
		D_PRINTLN("Allocating instrument file failed -  %d", name->c_str());
		return Error::INSUFFICIENT_RAM;
	}

	error = newInstrument->readFromFile(smDeserializer, song, clip, 0);

	bool fileSuccess = activeDeserializer->closeWriter();

	// If that somehow didn't work...
	if (error != Error::NONE || !fileSuccess) {
		D_PRINTLN("reading instrument file failed -  %s", name->c_str());
		if (fileSuccess) {
			error = Error::SD_CARD;
		}

deleteInstrumentAndGetOut:
		D_PRINTLN("abandoning load -  %s", name->c_str());
		newInstrument->deleteBackedUpParamManagers(song);
		void* toDealloc = static_cast<void*>(newInstrument);
		newInstrument->~Instrument();
		delugeDealloc(toDealloc);

		return error;
	}

	// Check that a ParamManager was actually loaded for the Instrument, cos if not, that'd spell havoc
	if (!song->getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)newInstrument->toModControllable(),
	                                                     nullptr)) {

		// Prior to V2.0 (or was it only in V1.0 on the 40-pad?) Kits didn't have anything that would have caused the
		// paramManager to be created when we read the Kit just now. So, just make one.
		if (song_firmware_version < FirmwareVersion::official({2, 2, 0, "beta"}) && outputType == OutputType::KIT) {
			ParamManagerForTimeline paramManager;
			error = paramManager.setupUnpatched();
			if (error != Error::NONE) {
				goto deleteInstrumentAndGetOut;
			}

			GlobalEffectableForClip::initParams(&paramManager);
			((Kit*)newInstrument)->compensateInstrumentVolumeForResonance(&paramManager, song); // Necessary?
			song->backUpParamManager(((Kit*)newInstrument), clip, &paramManager, true);
		}
		else if (outputType == OutputType::MIDI_OUT) {
			// midi instruments make the param manager later
		}
		else {
paramManagersMissing:
			D_PRINTLN("creating param manager failed -  %s", name->c_str());
			error = Error::FILE_CORRUPTED;
			goto deleteInstrumentAndGetOut;
		}
	}

	// For Kits, ensure that every audio Drum has a ParamManager somewhere
	if (newInstrument->type == OutputType::KIT) {
		Kit* kit = (Kit*)newInstrument;
		for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {
			if (thisDrum->type == DrumType::SOUND) {
				SoundDrum* soundDrum = (SoundDrum*)thisDrum;

				// If no backedUpParamManager...
				if (!currentSong->getBackedUpParamManagerPreferablyWithClip(soundDrum, NULL)) {
					goto paramManagersMissing;
				}
			}
		}
	}

	newInstrument->name = *name;
	newInstrument->dirPath = *dirPath;
	newInstrument->mightExistOnCard = true;
	newInstrument->loadAllAudioFiles(mayReadSamplesFromFiles); // Needs name, directory and slots set first, above.

	*getInstrument = newInstrument;
	return Error::NONE;
}

Error StorageManager::openMidiDeviceDefinitionFile(char const* path) {

	AudioEngine::logAction("openMidiDeviceDefinitionFile");
	char const* firstTagName = "midiDevice";
	char const* altTagName = "";

	Error error = openXMLFile(path, smDeserializer, firstTagName, altTagName);
	return error;
}

// Returns error status
Error StorageManager::loadMidiDeviceDefinitionFile(MIDIInstrument* midiInstrument, char const* path,
                                                   std::string* fileName, bool updateFileName) {
	midiInstrument->loadDeviceDefinitionFile = false;

	AudioEngine::logAction("loadMidiDeviceDefinitionFile");
	D_PRINTLN("opening midi device definition file -  %s  from path  %s", fileName->c_str(), path);

	Error error = openMidiDeviceDefinitionFile(path);
	if (error != Error::NONE) {
		D_PRINTLN("opening midi device definition file failed -  %s", fileName->c_str());
		return error;
	}

	AudioEngine::logAction("readMidiDeviceDefinitionFile");

	error = midiInstrument->readDeviceDefinitionFile(smDeserializer, false);

	bool fileSuccess = activeDeserializer->closeWriter();

	// If that somehow didn't work...
	if (error != Error::NONE || !fileSuccess) {
		D_PRINTLN("reading midi device definition file failed -  %s", fileName->c_str());
		if (fileSuccess) {
			error = Error::SD_CARD;
		}

		return error;
	}
	else if (updateFileName) {
		midiInstrument->deviceDefinitionFileName = fileName->c_str();
	}

	return Error::NONE;
}

Error StorageManager::openPatternFile(char const* path) {

	AudioEngine::logAction("openPatternFile");
	char const* firstTagName = "pattern";
	char const* altTagName = "";

	Error error = openXMLFile(path, smDeserializer, firstTagName, altTagName);
	return error;
}

Error StorageManager::openFavouriteFile(char const* path) {

	AudioEngine::logAction("openFavouriteFile");
	char const* firstTagName = "favourites";
	char const* altTagName = "";

	Error error = openXMLFile(path, smDeserializer, firstTagName, altTagName);
	return error;
}

// Returns error status
Error StorageManager::loadPatternFile(char const* path, std::string* fileName, bool overwriteExisting, bool noScaling,
                                      bool previewOnly, bool selectedDrumOnly) {

	AudioEngine::logAction("loadPatternFile");

	Error error = openPatternFile(path);
	if (error != Error::NONE) {
		return error;
	}

	AudioEngine::logAction("readPatternFile");

	error = instrumentClipView.pasteNotesFromFile(smDeserializer, overwriteExisting, noScaling, previewOnly,
	                                              selectedDrumOnly);

	bool fileSuccess = activeDeserializer->closeWriter();

	// If that somehow didn't work...
	if (error != Error::NONE || !fileSuccess) {
		if (fileSuccess) {
			error = Error::SD_CARD;
		}

		return error;
	}

	return Error::NONE;
}

// Returns error status
Error StorageManager::loadFavouriteFile(char const* path, std::string* fileName) {

	AudioEngine::logAction("loadFavouriteFile");

	Error error = openFavouriteFile(path);
	if (error != Error::NONE) {
		return error;
	}

	AudioEngine::logAction("readFavouriteFile");

	error = favouritesManager.loadFavouritesFromFile(smDeserializer);

	bool fileSuccess = activeDeserializer->closeWriter();

	// If that somehow didn't work...
	if (error != Error::NONE || !fileSuccess) {
		if (fileSuccess) {
			error = Error::SD_CARD;
		}

		return error;
	}

	return Error::NONE;
}

/**
 * Special function to read a synth preset into a sound drum
 */
Error StorageManager::loadSynthToDrum(Song* song, InstrumentClip* clip, bool mayReadSamplesFromFiles,
                                      SoundDrum** getInstrument, char const* path, std::string* name,
                                      std::string* dirPath) {
	OutputType outputType = OutputType::SYNTH;
	SoundDrum* newDrum = (SoundDrum*)createNewDrum(DrumType::SOUND);
	if (!newDrum) {
		return Error::INSUFFICIENT_RAM;
	}

	AudioEngine::logAction("loadSynthDrumFromFile");

	Error error = openInstrumentFile(outputType, path);
	if (error != Error::NONE) {
		return error;
	}

	AudioEngine::logAction("loadInstrumentFromFile");

	error = newDrum->readFromFile(smDeserializer, song, clip, 0);

	bool fileSuccess = activeDeserializer->closeWriter();

	// If that somehow didn't work...
	if (error != Error::NONE || !fileSuccess) {

		void* toDealloc = static_cast<void*>(newDrum);
		newDrum->~SoundDrum();
		deluge::memory::dealloc(toDealloc);
		return error;

		if (!fileSuccess) {
			error = Error::SD_CARD;
			return error;
		}
	}
	// these have to get cleared, otherwise we keep creating drums that aren't attached to note rows
	if (*getInstrument) {
		song->deleteBackedUpParamManagersForModControllable(*getInstrument);
		(*getInstrument)->wontBeRenderedForAWhile();
		void* toDealloc = static_cast<void*>(*getInstrument);
		(*getInstrument)->~SoundDrum();
		deluge::memory::dealloc(toDealloc);
	}

	*getInstrument = newDrum;
	return error;
}

// After calling this, you must make sure you set dirPath of Instrument.
Instrument* StorageManager::createNewInstrument(OutputType newOutputType, ParamManager* paramManager) {

	uint32_t instrumentSize;

	if (newOutputType == OutputType::SYNTH) {
		instrumentSize = sizeof(SoundInstrument);
	}

	else if (newOutputType == OutputType::MIDI_OUT) {
		instrumentSize = sizeof(MIDIInstrument);
	}

	// Kit
	else {
		instrumentSize = sizeof(Kit);
	}

	void* instrumentMemory = deluge::memory::alloc_fast(instrumentSize);
	if (!instrumentMemory) {
		return nullptr;
	}

	Instrument* newInstrument;

	Error error;

	// Synth
	if (newOutputType == OutputType::SYNTH) {
		if (paramManager) {
			error = paramManager->setupWithPatching();
			if (error != Error::NONE) {
paramManagerSetupError:
				delugeDealloc(instrumentMemory);
				return nullptr;
			}
			Sound::initParams(paramManager);
		}
		newInstrument = new (instrumentMemory) SoundInstrument();
	}

	else if (newOutputType == OutputType::MIDI_OUT) {
		newInstrument = new (instrumentMemory) MIDIInstrument();
	}

	// Kit
	else {
		if (paramManager) {
			error = paramManager->setupUnpatched();
			if (error != Error::NONE) {
				goto paramManagerSetupError;
			}

			GlobalEffectableForClip::initParams(paramManager);
		}
		newInstrument = new (instrumentMemory) Kit();
	}

	return newInstrument;
}

Instrument* StorageManager::createNewNonAudioInstrument(OutputType outputType, int32_t slot, int32_t subSlot) {
	int32_t size = (outputType == OutputType::MIDI_OUT) ? sizeof(MIDIInstrument) : sizeof(CVInstrument);
	// Paul: Might make sense to put these into Internal?
	void* instrumentMemory = deluge::memory::alloc_sdram(size);
	if (!instrumentMemory) { // RAM fail
		return nullptr;
	}

	NonAudioInstrument* newInstrument;

	if (outputType == OutputType::MIDI_OUT) {
		newInstrument = new (instrumentMemory) MIDIInstrument();
		((MIDIInstrument*)newInstrument)->channelSuffix = subSlot;
	}
	else {
		newInstrument = new (instrumentMemory) CVInstrument();
	}
	newInstrument->setChannel(slot);

	return newInstrument;
}

Drum* StorageManager::createNewDrum(DrumType drumType) {
	int32_t memorySize;
	if (drumType == DrumType::SOUND) {
		memorySize = sizeof(SoundDrum);
	}
	else if (drumType == DrumType::MIDI) {
		memorySize = sizeof(MIDIDrum);
	}
	else if (drumType == DrumType::GATE) {
		memorySize = sizeof(GateDrum);
	}

	void* drumMemory = deluge::memory::alloc_fast(memorySize);
	if (!drumMemory) {
		return nullptr;
	}

	Drum* newDrum = nullptr;
	if (drumType == DrumType::SOUND)
		newDrum = new (drumMemory) SoundDrum();
	else if (drumType == DrumType::MIDI)
		newDrum = new (drumMemory) MIDIDrum();
	else if (drumType == DrumType::GATE)
		newDrum = new (drumMemory) GateDrum();

	return newDrum;
}

bool StorageManager::buildPathToFile(const char* fileName) {

	etl::string<255> s_container;
	s_container.append(fileName);
	char* s = s_container.data();
	int i = strlen(s);

	while (i > 0 && s[i - 1] != '/') // Find decomposition point
		i--;

	if (i > 0) // Move to '/'
		i--;

	if (i > 0) {
		s[i] = 0; // replace '/' with NUL

		auto res = deluge::io::mkdir(s);

		if (!res.has_value() && res.error() == deluge::io::Status::NOT_FOUND) {
			// try the next folder in the path
			if (buildPathToFile(s)) {
				// if that worked, try again
				res = deluge::io::mkdir(s);
			}
		}

		if (res.has_value() || res.error() == deluge::io::Status::EXISTS)
			return true;
	}
	return false;
}

FileWriter::FileWriter() {
	bufferSize = 32768;
	void* temp = deluge::memory::alloc_sdram(bufferSize + CACHE_LINE_SIZE * 2);
	writeClusterBuffer = (char*)temp + CACHE_LINE_SIZE;
}

FileWriter::FileWriter(bool inMem) : FileWriter() {
	memoryBased = true;
}

FileWriter::~FileWriter() {
	deluge::memory::dealloc(writeClusterBuffer);
}

int32_t FileWriter::bytesWritten() {
	return fileTotalBytesWritten + fileWriteBufferCurrentPos;
}

void FileWriter::resetWriter() {
	fileWriteBufferCurrentPos = 0;
	fileTotalBytesWritten = 0;
	fileAccessFailedDuringWrite = false;
}

bool FileWriter::closeWriter() {
	if (memoryBased) {
		if (fileWriteBufferCurrentPos < bufferSize) {
			writeClusterBuffer[fileWriteBufferCurrentPos] = 0;
			return true;
		}
		else {
			return false;
		}
	}
	auto result = file->close();
	file.reset();
	return result.has_value();
}

void FileWriter::writeBlock(uint8_t* block, uint32_t size) {
	for (uint32_t ix = 0; ix < size; ++ix) {
		writeByte(block[ix]);
	}
}

void FileWriter::writeByte(int8_t b) {

	if (fileWriteBufferCurrentPos == bufferSize) {
		if (!memoryBased && !fileAccessFailedDuringWrite) {
			Error error = writeBufferToFile();
			if (error != Error::NONE) {
				fileAccessFailedDuringWrite = true;
				return;
			}
		}
		if (memoryBased) {
			fileAccessFailedDuringWrite = true;
			return;
		}
		fileWriteBufferCurrentPos = 0;
	}

	writeClusterBuffer[fileWriteBufferCurrentPos] = b;
	fileWriteBufferCurrentPos++;
}
void FileWriter::writeChars(char const* output) {
	while (*output) {
		writeByte(*output);
		output++;
	}
}

Error FileWriter::writeBufferToFile() {
	auto written = file->write(
	    std::span<const std::byte>(reinterpret_cast<const std::byte*>(writeClusterBuffer), fileWriteBufferCurrentPos));
	if (!written || *written != fileWriteBufferCurrentPos) {
		return Error::SD_CARD;
	}

	fileTotalBytesWritten += fileWriteBufferCurrentPos;

	return Error::NONE;
}

// Returns false if some error, including error while writing
Error FileWriter::closeAfterWriting(char const* path, char const* beginningString, char const* endString) {

	if (fileAccessFailedDuringWrite) {
		return Error::WRITE_FAIL; // Calling close if this is false might be dangerous - if access has failed, we
		                          // don't want it to flush any data to the card or anything
	}
	if (memoryBased)
		return Error::NONE;

	if ((beginningString || endString) && !path) {
		return Error::WRITE_FAIL; // Can't verify beginning/end strings without reopening by path.
	}

	Error error = writeBufferToFile();
	if (error != Error::NONE) {
		return Error::WRITE_FAIL;
	}

	// Check size while the file is still open in write mode - deluge::io::File has no way to query a
	// closed handle's last-known size.
	auto size = file->size();
	if (!size || *size != fileTotalBytesWritten) {
		return Error::WRITE_FAIL;
	}

	bool result = closeWriter();
	if (!result) {
		return Error::WRITE_FAIL;
	}

	if (path) {
		// Reopen for reading, to verify the beginning/end strings.
		auto opened = deluge::io::File::open(path, DELUGE_FILE_READ);
		if (!opened) {
			return Error::WRITE_FAIL;
		}
		file = std::move(opened.value());
	}

	// Check beginning
	if (beginningString) {
		int32_t length = strlen(beginningString);
		auto read = file->read(std::span<std::byte>(reinterpret_cast<std::byte*>(miscStringBuffer), length));
		if (!read) {
			return Error::WRITE_FAIL;
		}
		if (memcmp(miscStringBuffer, beginningString, length)) {
			return Error::WRITE_FAIL;
		}
	}

	// Check end
	if (endString) {
		int32_t length = strlen(endString);

		auto sought = file->seek(fileTotalBytesWritten - length);
		if (!sought) {
			return Error::WRITE_FAIL;
		}

		auto read = file->read(std::span<std::byte>(reinterpret_cast<std::byte*>(miscStringBuffer), length));
		if (!read) {
			return Error::WRITE_FAIL;
		}
		if (memcmp(miscStringBuffer, endString, length)) {
			return Error::WRITE_FAIL;
		}
	}

	if (path) {
		result = closeWriter();
		if (!result) {
			return Error::WRITE_FAIL;
		}
	}

	return Error::NONE;
}
