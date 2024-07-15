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
#include "drivers/pic/pic.h"
#include "fatfs/fatfs.hpp"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
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
#include "util/firmware_version.h"
#include "util/functions.h"
#include "util/try.h"
#include "version.h"
#include <string.h>

extern "C" {
#include "RZA1/oled/oled_low_level.h"
#include "fatfs/diskio.h"
#include "fatfs/ff.h"

FRESULT f_readdir_get_filepointer(DIR* dp,      /* Pointer to the open directory object */
                                  FILINFO* fno, /* Pointer to file information to return */
                                  FilePointer* filePointer);

void routineForSD(void);
}

StorageManager storageManager{};
FILINFO staticFNO;
DIR staticDIR;
XMLSerializer smSerializer;
XMLDeserializer smDeserializer;

extern void initialiseConditions();
extern void songLoaded(Song* song);

// Because FATFS and FIL objects store buffers for SD read data to be read to via DMA, we have to space them apart from
// any other data so that invalidation and stuff works
FatFS::Filesystem fileSystem;

Error StorageManager::checkSpaceOnCard() {
	D_PRINTLN("free clusters:  %d", fileSystem.free_clst);
	return fileSystem.free_clst ? Error::NONE : Error::SD_CARD_FULL; // This doesn't seem to always be 100% accurate...
}

// Creates folders and subfolders as needed!
std::expected<FatFS::File, Error> StorageManager::createFile(char const* filePath, bool mayOverwrite) {

	Error error = initSD();
	if (error != Error::NONE) {
		return std::unexpected(error);
	}

	error = checkSpaceOnCard();
	if (error != Error::NONE) {
		return std::unexpected(error);
	}

	bool triedCreatingFolder = false;

	BYTE mode = FA_WRITE;
	if (mayOverwrite) {
		mode |= FA_CREATE_ALWAYS;
	}
	else {
		mode |= FA_CREATE_NEW;
	}

tryAgain:
	auto opened = FatFS::File::open(filePath, mode);
	if (!opened) {

processError:
		// If folder doesn't exist, try creating it - once only
		if (opened.error() == FatFS::Error::NO_PATH) {
			if (triedCreatingFolder) {
				return std::unexpected(Error::FOLDER_DOESNT_EXIST);
			}
			triedCreatingFolder = true;

			String folderPath;
			error = folderPath.set(filePath);
			if (error != Error::NONE) {
				return std::unexpected(error);
			}

			// Get just the folder path
cutFolderPathAndTryCreating:
			char const* folderPathChars = folderPath.get();
			char const* slashAddr = strrchr(folderPathChars, '/');
			if (!slashAddr) {
				return std::unexpected(Error::UNSPECIFIED); // Shouldn't happen
			}
			int32_t slashPos = (uint32_t)slashAddr - (uint32_t)folderPathChars;

			error = folderPath.shorten(slashPos);
			if (error != Error::NONE) {
				return std::unexpected(error);
			}

			// Try making the folder
			auto made_dir = FatFS::mkdir(folderPath.get());
			if (made_dir) {
				goto tryAgain;
			}
			// If that folder couldn't be created because its parent folder didn't exist...
			else if (made_dir.error() == FatFS::Error::NO_PATH) {
				triedCreatingFolder = false;      // Let it do multiple tries again
				goto cutFolderPathAndTryCreating; // Go and try creating the parent folder
			}
			else {
				goto processError;
			}
		}

		// Otherwise, just return the appropriate error.
		else {

			error = fatfsErrorToDelugeError(opened.error());
			if (error == Error::SD_CARD) {
				error = Error::WRITE_FAIL; // Get a bit more specific if we only got the most general error.
			}
			return std::unexpected(error);
		}
	}

	return opened.value();
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
	writer.writeFIL = created.value().inner();
	writer.reset();

	writer.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");

	return Error::NONE;
}

bool StorageManager::fileExists(char const* pathName) {
	Error error = initSD();
	if (error != Error::NONE) {
		return false;
	}

	FRESULT result = f_stat(pathName, &staticFNO);
	return (result == FR_OK);
}

// Lets you get the FilePointer for the file.
bool StorageManager::fileExists(char const* pathName, FilePointer* fp) {
	FIL fil;
	Error error = initSD();
	if (error != Error::NONE) {
		return false;
	}

	FRESULT result = f_open(&fil, pathName, FA_READ);
	if (result != FR_OK) {
		return false;
	}

	fp->sclust = fil.obj.sclust;
	fp->objsize = fil.obj.objsize;

	f_close(&fil);
	return true;
}

// Returns false if some error, including error while writing
bool StorageManager::closeFile(FIL& fileToClose) {
	FRESULT result = f_close(&fileToClose);
	return (result == FR_OK);
}

// Gets ready to access SD card.
// You should call this before you're gonna do any accessing - otherwise any errors won't reflect if there's in fact
// just no card inserted.
Error StorageManager::initSD() {

	// If we know the SD card is still initialised, no need to actually initialise
	DSTATUS status = disk_status(SD_PORT);
	if ((status & STA_NOINIT) == 0) {
		return Error::NONE;
	}

	// But if there's no card present, we're in trouble
	if (status & STA_NODISK) {
		return Error::SD_CARD_NOT_PRESENT;
	}

	// Otherwise, we can mount the filesystem...
	bool success = D_TRY_CATCH(fileSystem.mount(1).transform_error(fatfsErrorToDelugeError), error, {
		return error; //<
	});
	if (success) {
		return Error::NONE;
	}
	return Error::SD_CARD;
}

bool StorageManager::checkSDPresent() {
	DSTATUS status = disk_status(SD_PORT);
	bool present = !(status & STA_NODISK);
	return present;
}

bool StorageManager::checkSDInitialized() {
	DSTATUS status = disk_status(SD_PORT);
	return !(status & STA_NOINIT);
}

// Function can't fail.
void StorageManager::openFilePointer(FilePointer* fp, XMLDeserializer& reader) {

	AudioEngine::logAction("openFilePointer");

	D_PRINTLN("openFilePointer");

	reader.readFIL.obj.sclust = fp->sclust;
	reader.readFIL.obj.objsize = fp->objsize;
	reader.readFIL.obj.fs = &fileSystem; /* Validate the file object */
	reader.readFIL.obj.id = fileSystem.id;

	reader.readFIL.flag = FA_READ; /* Set file access mode */
	reader.readFIL.err = 0;        /* Clear error flag */
	reader.readFIL.sect = 0;       /* Invalidate current data sector */
	reader.readFIL.fptr = 0;       /* Set file pointer top of the file */
}

Error StorageManager::openInstrumentFile(OutputType outputType, FilePointer* filePointer) {

	AudioEngine::logAction("openInstrumentFile");
	if (!filePointer->sclust) {
		return Error::FILE_NOT_FOUND;
	}
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

	Error error = openXMLFile(filePointer, smDeserializer, firstTagName, altTagName);
	return error;
}

// Returns error status
// clip may be NULL
Error StorageManager::loadInstrumentFromFile(Song* song, InstrumentClip* clip, OutputType outputType,
                                             bool mayReadSamplesFromFiles, Instrument** getInstrument,
                                             FilePointer* filePointer, String* name, String* dirPath) {

	AudioEngine::logAction("loadInstrumentFromFile");
	D_PRINTLN("opening instrument file -  %s %s  from FP  %lu", dirPath->get(), name->get(),
	          (int32_t)filePointer->sclust);

	Error error = openInstrumentFile(outputType, filePointer);
	if (error != Error::NONE) {
		D_PRINTLN("opening instrument file failed -  %s", name->get());
		return error;
	}

	AudioEngine::logAction("loadInstrumentFromFile");
	Instrument* newInstrument = createNewInstrument(outputType);

	if (!newInstrument) {
		closeFile(smDeserializer.readFIL);
		D_PRINTLN("Allocating instrument file failed -  %d", name->get());
		return Error::INSUFFICIENT_RAM;
	}

	error = newInstrument->readFromFile(smDeserializer, song, clip, 0);

	bool fileSuccess = closeFile(smDeserializer.readFIL);

	// If that somehow didn't work...
	if (error != Error::NONE || !fileSuccess) {
		D_PRINTLN("reading instrument file failed -  %s", name->get());
		if (!fileSuccess) {
			error = Error::SD_CARD;
		}

deleteInstrumentAndGetOut:
		D_PRINTLN("abandoning load -  %s", name->get());
		newInstrument->deleteBackedUpParamManagers(song);
		void* toDealloc = static_cast<void*>(newInstrument);
		newInstrument->~Instrument();
		delugeDealloc(toDealloc);

		return error;
	}

	// Check that a ParamManager was actually loaded for the Instrument, cos if not, that'd spell havoc
	if (!song->getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)newInstrument->toModControllable(),
	                                                     NULL)) {

		// Prior to V2.0 (or was it only in V1.0 on the 40-pad?) Kits didn't have anything that would have caused the
		// paramManager to be created when we read the Kit just now. So, just make one.
		if (smDeserializer.firmware_version < FirmwareVersion::official({2, 2, 0, "beta"})
		    && outputType == OutputType::KIT) {
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
			D_PRINTLN("creating param manager failed -  %s", name->get());
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

	newInstrument->name.set(name);
	newInstrument->dirPath.set(dirPath);
	newInstrument->existsOnCard = true;
	newInstrument->loadAllAudioFiles(mayReadSamplesFromFiles); // Needs name, directory and slots set first, above.

	*getInstrument = newInstrument;
	return Error::NONE;
}

/**
 * Special function to read a synth preset into a sound drum
 */
Error StorageManager::loadSynthToDrum(Song* song, InstrumentClip* clip, bool mayReadSamplesFromFiles,
                                      SoundDrum** getInstrument, FilePointer* filePointer, String* name,
                                      String* dirPath) {
	OutputType outputType = OutputType::SYNTH;
	SoundDrum* newDrum = (SoundDrum*)createNewDrum(DrumType::SOUND);
	if (!newDrum) {
		return Error::INSUFFICIENT_RAM;
	}

	AudioEngine::logAction("loadSynthDrumFromFile");

	Error error = openInstrumentFile(outputType, filePointer);
	if (error != Error::NONE) {
		return error;
	}

	AudioEngine::logAction("loadInstrumentFromFile");

	error = newDrum->readFromFile(smDeserializer, song, clip, 0);

	bool fileSuccess = closeFile(smDeserializer.readFIL);
	;

	// If that somehow didn't work...
	if (error != Error::NONE || !fileSuccess) {

		void* toDealloc = static_cast<void*>(newDrum);
		newDrum->~SoundDrum();
		GeneralMemoryAllocator::get().dealloc(toDealloc);
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
		GeneralMemoryAllocator::get().dealloc(toDealloc);
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

	void* instrumentMemory = GeneralMemoryAllocator::get().allocMaxSpeed(instrumentSize);
	if (!instrumentMemory) {
		return NULL;
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
				return NULL;
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
	void* instrumentMemory = GeneralMemoryAllocator::get().allocLowSpeed(size);
	if (!instrumentMemory) { // RAM fail
		return NULL;
	}

	NonAudioInstrument* newInstrument;

	if (outputType == OutputType::MIDI_OUT) {
		newInstrument = new (instrumentMemory) MIDIInstrument();
		((MIDIInstrument*)newInstrument)->channelSuffix = subSlot;
	}
	else {
		newInstrument = new (instrumentMemory) CVInstrument();
	}
	newInstrument->channel = slot;

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

	void* drumMemory = GeneralMemoryAllocator::get().allocMaxSpeed(memorySize);
	if (!drumMemory) {
		return NULL;
	}

	Drum* newDrum;
	if (drumType == DrumType::SOUND)
		newDrum = new (drumMemory) SoundDrum();
	else if (drumType == DrumType::MIDI)
		newDrum = new (drumMemory) MIDIDrum();
	else if (drumType == DrumType::GATE)
		newDrum = new (drumMemory) GateDrum();

	return newDrum;
}

Error StorageManager::openXMLFile(FilePointer* filePointer, XMLDeserializer& reader, char const* firstTagName,
                                  char const* altTagName, bool ignoreIncorrectFirmware) {

	AudioEngine::logAction("openXMLFile");
	reader.reset();
	// Prep to read first Cluster shortly
	openFilePointer(filePointer, reader);
	Error err = reader.openXMLFile(filePointer, firstTagName, altTagName, ignoreIncorrectFirmware);
	if (err == Error::NONE)
		return Error::NONE;
	f_close(&reader.readFIL);

	return Error::FILE_CORRUPTED;
}

FileReader::FileReader() {
	void* temp = GeneralMemoryAllocator::get().allocLowSpeed(32768 + CACHE_LINE_SIZE * 2);
	fileClusterBuffer = (char*)temp + CACHE_LINE_SIZE;
}

FileReader::~FileReader() {
	GeneralMemoryAllocator::get().dealloc(fileClusterBuffer);
}

void FileReader::resetReader() {
	fileReadBufferCurrentPos = audioFileManager.clusterSize;
	currentReadBufferEndPos = audioFileManager.clusterSize;
	readCount = 0;
	reachedBufferEnd = false;
}

// Returns whether successful loading took place
bool FileReader::readFileClusterIfNecessary() {

	// Load next Cluster if necessary
	if (fileReadBufferCurrentPos >= audioFileManager.clusterSize) {
		readCount = 0;
		bool result = readFileCluster();
		if (!result) {
			reachedBufferEnd = true;
		}
		return result;
	}

	// Watch out for end of file
	if (fileReadBufferCurrentPos >= currentReadBufferEndPos) {
		reachedBufferEnd = true;
	}

	return false;
}

bool FileReader::readFileCluster() {

	AudioEngine::logAction("readFileCluster");

	FRESULT result = f_read(&readFIL, (UINT*)fileClusterBuffer, audioFileManager.clusterSize, &currentReadBufferEndPos);
	if (result) {
		return false;
	}

	// If error or we reached end of file
	if (!currentReadBufferEndPos) {
		return false;
	}

	fileReadBufferCurrentPos = 0;

	return true;
}

uint32_t FileReader::readChar(char* thisChar) {

	bool stillGoing = readFileClusterIfNecessary();
	if (reachedBufferEnd) {
		return 0;
	}

	*thisChar = fileClusterBuffer[fileReadBufferCurrentPos];

	fileReadBufferCurrentPos++;

	return 1;
}

// Call various routines 1 out of N times, where N = 64 at present.
void FileReader::readDone() {
	readCount++; // Increment first, cos we don't want to call SD routine immediately when it's 0

	if (!(readCount & 63)) { // 511 bad. 255 almost fine. 127 almost always fine
		AudioEngine::routineWithClusterLoading();

		uiTimerManager.routine();

		if (display->haveOLED()) {
			oledRoutine();
		}
		PIC::flush();
	}
}

FileWriter::FileWriter() {
	void* temp = GeneralMemoryAllocator::get().allocLowSpeed(32768 + CACHE_LINE_SIZE * 2);
	writeClusterBuffer = (char*)temp + CACHE_LINE_SIZE;
}

FileWriter::~FileWriter() {
	GeneralMemoryAllocator::get().dealloc(writeClusterBuffer);
}

void FileWriter::resetWriter() {
	fileWriteBufferCurrentPos = 0;
	fileTotalBytesWritten = 0;
	fileAccessFailedDuringWrite = false;
}

void FileWriter::writeChars(char const* output) {

	while (*output) {

		if (fileWriteBufferCurrentPos == audioFileManager.clusterSize) {

			if (!fileAccessFailedDuringWrite) {
				Error error = writeBufferToFile();
				if (error != Error::NONE) {
					fileAccessFailedDuringWrite = true;
					return;
				}
			}

			fileWriteBufferCurrentPos = 0;
		}

		writeClusterBuffer[fileWriteBufferCurrentPos] = *output;

		output++;
		fileWriteBufferCurrentPos++;

		// Ensure we do some of the audio routine once in a while
		if (!(fileWriteBufferCurrentPos & 0b11111111)) {
			AudioEngine::logAction("writeCharsJSON");

			// AudioEngine::routineWithClusterLoading();

			uiTimerManager.routine();

			if (display->haveOLED()) {
				oledRoutine();
			}
			PIC::flush();
		}
	}
}

Error FileWriter::writeBufferToFile() {
	UINT bytesWritten;
	FRESULT result = f_write(&writeFIL, writeClusterBuffer, fileWriteBufferCurrentPos, &bytesWritten);
	if (result != FR_OK || bytesWritten != fileWriteBufferCurrentPos) {
		return Error::SD_CARD;
	}

	fileTotalBytesWritten += fileWriteBufferCurrentPos;

	return Error::NONE;
}

// Returns false if some error, including error while writing
Error FileWriter::closeAfterWriting(char const* path, char const* beginningString, char const* endString) {
	if (fileAccessFailedDuringWrite) {
		return Error::WRITE_FAIL; // Calling f_close if this is false might be dangerous - if access has failed, we
		                          // don't want it to flush any data to the card or anything
	}
	Error error = writeBufferToFile();
	if (error != Error::NONE) {
		return Error::WRITE_FAIL;
	}

	FRESULT result = f_close(&writeFIL);
	if (result) {
		return Error::WRITE_FAIL;
	}

	if (path) {
		// Check file exists
		result = f_open(&writeFIL, path, FA_READ);
		if (result) {
			return Error::WRITE_FAIL;
		}
	}

	// Check size
	if (f_size(&writeFIL) != fileTotalBytesWritten) {
		return Error::WRITE_FAIL;
	}

	// Check beginning
	if (beginningString) {
		UINT dontCare;
		int32_t length = strlen(beginningString);
		result = f_read(&writeFIL, miscStringBuffer, length, &dontCare);
		if (result) {
			return Error::WRITE_FAIL;
		}
		if (memcmp(miscStringBuffer, beginningString, length)) {
			return Error::WRITE_FAIL;
		}
	}

	// Check end
	if (endString) {
		UINT dontCare;
		int32_t length = strlen(endString);

		result = f_lseek(&writeFIL, fileTotalBytesWritten - length);
		if (result) {
			return Error::WRITE_FAIL;
		}

		result = f_read(&writeFIL, miscStringBuffer, length, &dontCare);
		if (result) {
			return Error::WRITE_FAIL;
		}
		if (memcmp(miscStringBuffer, endString, length)) {
			return Error::WRITE_FAIL;
		}
	}

	result = f_close(&writeFIL);
	if (result) {
		return Error::WRITE_FAIL;
	}

	return Error::NONE;
}
