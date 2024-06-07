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

char charAtEndOfValue;

extern void initialiseConditions();
extern void songLoaded(Song* song);

// Because FATFS and FIL objects store buffers for SD read data to be read to via DMA, we have to space them apart from
// any other data so that invalidation and stuff works
FatFS::Filesystem  fileSystem;


Error StorageManager::checkSpaceOnCard() {
	D_PRINTLN("free clusters:  %d", fileSystem.free_clst);
	return fileSystem.free_clst
	           ? Error::NONE
	           : Error::SD_CARD_FULL; // This doesn't seem to always be 100% accurate...
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

	writer.fileWriteBufferCurrentPos = 0;
	writer.fileTotalBytesWritten = 0;
	writer.fileAccessFailedDuringWrite = false;
	writer.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");

	writer.indentAmount = 0;

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
bool StorageManager::closeFile(FIL &fileToClose) {
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

	bool fileSuccess = closeFile(smDeserializer.readFIL);;

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

/*******************************************************************************

    XMLSerializer

********************************************************************************/

void Serializer::writeAbsoluteSyncLevelToFile(Song* song, char const* name, SyncLevel internalValue, bool onNewLine) {
	writeAttribute(name, song->convertSyncLevelFromInternalValueToFileValue(internalValue), onNewLine);
}

void Serializer::writeFirmwareVersion() {
	writeAttribute("firmwareVersion", kFirmwareVersionStringShort);
}

XMLSerializer::XMLSerializer() {
	void* temp = GeneralMemoryAllocator::get().allocLowSpeed(32768 + CACHE_LINE_SIZE * 2);
	writeClusterBuffer = (char*)temp + CACHE_LINE_SIZE;
	reset();
}

XMLSerializer::~XMLSerializer() {
	GeneralMemoryAllocator::get().dealloc(writeClusterBuffer);
}

void XMLSerializer::reset() {
	fileWriteBufferCurrentPos = 0;
	fileTotalBytesWritten = 0;
	fileAccessFailedDuringWrite = false;

}

// TODO: this is really inefficient
void XMLSerializer::write(char const* output) {

	while (*output) {

		if (fileWriteBufferCurrentPos == audioFileManager.clusterSize) {

			if (!fileAccessFailedDuringWrite) {
				Error error = writeXMLBufferToFile();
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
			AudioEngine::logAction("writeCharXML");

			// AudioEngine::routineWithClusterLoading();

			uiTimerManager.routine();

			if (display->haveOLED()) {
				oledRoutine();
			}
			PIC::flush();
		}
	}
}

void XMLSerializer::writeTag(char const* tag, int32_t number) {
	char* buffer = shortStringBuffer;
	intToString(number, buffer);
	writeTag(tag, buffer);
}

void XMLSerializer::writeTag(char const* tag, char const* contents) {

	printIndents();
	write("<");
	write(tag);
	write(">");
	write(contents);
	write("</");
	write(tag);
	write(">\n");
}

void XMLSerializer::writeAttribute(char const* name, int32_t number, bool onNewLine) {

	char buffer[12];
	intToString(number, buffer);

	writeAttribute(name, buffer, onNewLine);
}

// numChars may be up to 8
void XMLSerializer::writeAttributeHex(char const* name, int32_t number, int32_t numChars, bool onNewLine) {

	char buffer[11];
	buffer[0] = '0';
	buffer[1] = 'x';
	intToHex(number, &buffer[2], numChars);

	writeAttribute(name, buffer, onNewLine);
}

// numChars may be up to 8
void XMLSerializer::writeAttributeHexBytes(char const* name, uint8_t* data, int32_t numBytes, bool onNewLine) {

	if (onNewLine) {
		write("\n");
		printIndents();
	}
	else {
		write(" ");
	}
	write(name);
	write("=\"");

	char buffer[3];
	for (int i = 0; i < numBytes; i++) {
		intToHex(data[i], &buffer[0], 2);
		write(buffer);
	}
	write("\"");
}

void XMLSerializer::writeAttribute(char const* name, char const* value, bool onNewLine) {

	if (onNewLine) {
		write("\n");
		printIndents();
	}
	else {
		write(" ");
	}

	write(name);
	write("=\"");
	write(value);
	write("\"");
}

void XMLSerializer::writeOpeningTag(char const* tag, bool startNewLineAfter) {
	writeOpeningTagBeginning(tag);
	writeOpeningTagEnd(startNewLineAfter);
}

void XMLSerializer::writeOpeningTagBeginning(char const* tag) {
	printIndents();
	write("<");
	write(tag);
	indentAmount++;
}

void XMLSerializer::closeTag() {
	write(" /");
	writeOpeningTagEnd();
	indentAmount--;
}

void XMLSerializer::writeOpeningTagEnd(bool startNewLineAfter) {
	if (startNewLineAfter) {
		write(">\n");
	}
	else {
		write(">");
	}
}

void XMLSerializer::writeClosingTag(char const* tag, bool shouldPrintIndents) {
	indentAmount--;
	if (shouldPrintIndents) {
		printIndents();
	}
	write("</");
	write(tag);
	write(">\n");
}

void XMLSerializer::printIndents() {
	for (int32_t i = 0; i < indentAmount; i++) {
		write("\t");
	}
}

Error XMLSerializer::writeXMLBufferToFile() {
	UINT bytesWritten;
	FRESULT result =
	    f_write(&writeFIL, writeClusterBuffer, fileWriteBufferCurrentPos, &bytesWritten);
	if (result != FR_OK || bytesWritten != fileWriteBufferCurrentPos) {
		return Error::SD_CARD;
	}

	fileTotalBytesWritten += fileWriteBufferCurrentPos;

	return Error::NONE;
}

// Returns false if some error, including error while writing
Error XMLSerializer::closeFileAfterWriting(char const* path, char const* beginningString, char const* endString) {
	if (fileAccessFailedDuringWrite) {
		return Error::WRITE_FAIL; // Calling f_close if this is false might be dangerous - if access has failed, we
		                          // don't want it to flush any data to the card or anything
	}
	Error error = writeXMLBufferToFile();
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

/*******************************************************************************

    XMLDeserializer

********************************************************************************/

#define BETWEEN_TAGS 0
#define IN_TAG_NAME 1
#define IN_TAG_PAST_NAME 2
#define IN_ATTRIBUTE_NAME 3
#define PAST_ATTRIBUTE_NAME 4
#define PAST_EQUALS_SIGN 5
#define IN_ATTRIBUTE_VALUE 6

XMLDeserializer::XMLDeserializer() {

	void* temp = GeneralMemoryAllocator::get().allocLowSpeed(32768 + CACHE_LINE_SIZE * 2);
	fileClusterBuffer = (char*)temp + CACHE_LINE_SIZE;
	reset();
}

XMLDeserializer::~XMLDeserializer() {
	GeneralMemoryAllocator::get().dealloc(fileClusterBuffer);
}

void XMLDeserializer::reset() {

	xmlReadCount = 0;
	// Prep to read first Cluster shortly
	fileReadBufferCurrentPos = audioFileManager.clusterSize;
	currentReadBufferEndPos = audioFileManager.clusterSize;

	firmware_version = FirmwareVersion{FirmwareVersion::Type::OFFICIAL, {}};

	tagDepthFile = 0;
	tagDepthCaller = 0;
	xmlReachedEnd = false;
	xmlArea = BETWEEN_TAGS;
}

// Only call this if IN_TAG_NAME
// TODO: this is very sub-optimal and still calls readCharXML()
char const* XMLDeserializer::readTagName() {

	if (false) {
skipToNextTag:
		skipUntilChar('>');
		skipUntilChar('<');
	}

	char thisChar;
	int32_t charPos = 0;

	while (readCharXML(&thisChar)) {
		switch (thisChar) {
		case '/':
			goto skipPastRest;

		case ' ':
		case '\r':
		case '\n':
		case '\t':
			xmlArea = IN_TAG_PAST_NAME;
			goto getOut;

		case '?':
			goto skipToNextTag;

		case '>':
			xmlArea = BETWEEN_TAGS;
			goto getOut;

		default:
			if (!charPos) {
				tagDepthFile++;
			}

			// Store this character, if space in our un-ideal buffer
			if (charPos < kFilenameBufferSize - 1) {
				stringBuffer[charPos++] = thisChar;
			}
		}
	}

getOut:
	xmlReadDone();

	if (false) {
skipPastRest:
		tagDepthFile--;
		skipUntilChar('>');
		xmlArea = BETWEEN_TAGS;
	}

	stringBuffer[charPos] = 0;
	return stringBuffer;
}

// Only call when IN_TAG_PAST_NAME
char const* XMLDeserializer::readNextAttributeName() {

	char thisChar;
	int32_t charPos = 0;

	while (readCharXML(&thisChar)) {
		switch (thisChar) {
		case ' ':
		case '\r':
		case '\n':
		case '\t':
			break;

		case '/':
			tagDepthFile--;
			skipUntilChar('>');
			// No break

		case '>':
			xmlArea = BETWEEN_TAGS;
			// No break

		case '<': // This is an error - there definitely shouldn't be a '<' inside a tag! TODO: make way to return error
			goto noMoreAttributes;

		default:
			goto doReadName;
		}
	}

noMoreAttributes:
	return "";

	// Here, we're in IN_ATTRIBUTE_NAME, and we're not allowed to leave this while loop until our xmlArea changes to
	// something else
	// - or there's an error or file-end, in which case we'll return error below
doReadName:
	xmlArea = IN_ATTRIBUTE_NAME;
	tagDepthFile++;
	fileReadBufferCurrentPos--; // This means we don't need to call readXMLFileClusterIfNecessary()

	bool haveReachedNameEnd = false;

	// This is basically copied and tweaked from readUntilChar()
	do {
		int32_t bufferPosAtStart = fileReadBufferCurrentPos;
		while (fileReadBufferCurrentPos < currentReadBufferEndPos) {
			char thisChar = fileClusterBuffer[fileReadBufferCurrentPos];

			switch (thisChar) {
			case ' ':
			case '\r':
			case '\n':
			case '\t':
				xmlArea = PAST_ATTRIBUTE_NAME;
				goto reachedNameEnd;

			case '=':
				xmlArea = PAST_EQUALS_SIGN;
				goto reachedNameEnd;

			// If we get a close-tag name, it means we saw some sorta attribute name with no value, which isn't allowed,
			// so treat it as invalid
			case '>':
				xmlArea = BETWEEN_TAGS;
				goto noMoreAttributes;

				// TODO: a '/' should get us outta here too...
			}

			fileReadBufferCurrentPos++;
		}

		if (false) {
reachedNameEnd:
			xmlReadDone();
			haveReachedNameEnd = true;
			// If possible, just return a pointer to the chars within the existing buffer
			if (!charPos && fileReadBufferCurrentPos < currentReadBufferEndPos) {
				fileClusterBuffer[fileReadBufferCurrentPos] = 0; // NULL end of the string we're returning
				fileReadBufferCurrentPos++;                      // Gets us past the endChar
				return &fileClusterBuffer[bufferPosAtStart];
			}
		}

		int32_t numCharsHere = fileReadBufferCurrentPos - bufferPosAtStart;
		int32_t numCharsToCopy = std::min<int32_t>(numCharsHere, kFilenameBufferSize - 1 - charPos);

		if (numCharsToCopy > 0) {
			memcpy(&stringBuffer[charPos], &fileClusterBuffer[bufferPosAtStart], numCharsToCopy);

			charPos += numCharsToCopy;
		}

		if (haveReachedNameEnd) {
			stringBuffer[charPos] = 0;
			fileReadBufferCurrentPos++; // Gets us past the endChar
			return stringBuffer;
		}

	} while (fileReadBufferCurrentPos == currentReadBufferEndPos && readXMLFileClusterIfNecessary());

	// If here, file ended
	return "";
}

// char charAtEndOfValue; // *** JFF get rid of this global!

char const* XMLDeserializer::readNextTagOrAttributeName() {

	char const* toReturn;
	int32_t tagDepthStart = tagDepthFile;

	switch (xmlArea) {

	default:
#if ALPHA_OR_BETA_VERSION
		// Can happen with invalid files, though I'm implementing error checks whenever a user alerts me to a scenario.
		// Fraser got this, Nov 2021.
		FREEZE_WITH_ERROR("E365");
#else
		__builtin_unreachable();
#endif
		break;

	case IN_ATTRIBUTE_VALUE: // Could have been left here during a char-at-a-time read
		skipUntilChar(charAtEndOfValue);
		xmlArea = IN_TAG_PAST_NAME;
		// No break

	case IN_TAG_PAST_NAME:
		toReturn = readNextAttributeName();
		// If depth has changed, this means we met a /> and must get out
		if (*toReturn || tagDepthFile != tagDepthStart) {
			break;
		}
		// No break

	case BETWEEN_TAGS:
		skipUntilChar('<');
		xmlArea = IN_TAG_NAME;
		// No break

	case IN_TAG_NAME:
		toReturn = readTagName();
	}

	if (*toReturn) {
		/*
		for (int32_t t = 0; t < tagDepthCaller; t++) {
D_PRINTLN("\t");
		}
		D_PRINTLN(toReturn);
		*/
		tagDepthCaller++;
		AudioEngine::logAction(toReturn);
	}

	return toReturn;
}

// Only call if PAST_ATTRIBUTE_NAME or PAST_EQUALS_SIGN
// Returns the quote character that opens the value - or 0 if fail
bool XMLDeserializer::getIntoAttributeValue() {

	char thisChar;

	switch (xmlArea) {
	case PAST_ATTRIBUTE_NAME:
		while (readCharXML(&thisChar)) {
			switch (thisChar) {
			case ' ':
			case '\r':
			case '\n':
			case '\t':
				break;

			case '=':
				xmlArea = PAST_EQUALS_SIGN;
				goto pastEqualsSign;

			default:
				return false; // There shouldn't be any other characters. If there are, that's an error
			}
		}

		break;

	case PAST_EQUALS_SIGN:
pastEqualsSign:
		while (readCharXML(&thisChar)) {
			switch (thisChar) {
			case ' ':
			case '\r':
			case '\n':
			case '\t':
				break;

			case '"':
			case '\'':
				goto inAttributeValue;

			default:
				return false; // There shouldn't be any other characters. If there are, that's an error
			}
		}
		break;
	}

	if (false) {
inAttributeValue:
		xmlArea = IN_ATTRIBUTE_VALUE;
		tagDepthFile--;
		charAtEndOfValue = thisChar;
		return true;
	}

	return false; // Fail
}

// Only call if PAST_ATTRIBUTE_NAME or PAST_EQUALS_SIGN
char const* XMLDeserializer::readAttributeValue() {

	if (!getIntoAttributeValue()) {
		return "";
	}
	xmlArea = IN_TAG_PAST_NAME; // How it'll be after this next call
	return readUntilChar(charAtEndOfValue);
}

// Only call if PAST_ATTRIBUTE_NAME or PAST_EQUALS_SIGN
int32_t XMLDeserializer::readAttributeValueInt() {

	if (!getIntoAttributeValue()) {
		return 0;
	}
	xmlArea = IN_TAG_PAST_NAME; // How it'll be after this next call
	return readIntUntilChar(charAtEndOfValue);
}

// Only call if PAST_ATTRIBUTE_NAME or PAST_EQUALS_SIGN
Error XMLDeserializer::readAttributeValueString(String* string) {

	if (!getIntoAttributeValue()) {
		string->clear();
		return Error::NONE;
	}
	Error error = readStringUntilChar(string, charAtEndOfValue);
	if (error == Error::NONE) {
		xmlArea = IN_TAG_PAST_NAME;
	}
	return error;
}

void XMLDeserializer::xmlReadDone() {
	xmlReadCount++; // Increment first, cos we don't want to call SD routine immediately when it's 0

	if (!(xmlReadCount & 63)) { // 511 bad. 255 almost fine. 127 almost always fine
		AudioEngine::routineWithClusterLoading();

		uiTimerManager.routine();

		if (display->haveOLED()) {
			oledRoutine();
		}
		PIC::flush();
	}
}

void XMLDeserializer::skipUntilChar(char endChar) {

	readXMLFileClusterIfNecessary(); // Does this need to be here? Originally I didn't have it...
	do {
		while (fileReadBufferCurrentPos < currentReadBufferEndPos
		       && fileClusterBuffer[fileReadBufferCurrentPos] != endChar) {
			fileReadBufferCurrentPos++;
		}

	} while (fileReadBufferCurrentPos == currentReadBufferEndPos && readXMLFileClusterIfNecessary());

	fileReadBufferCurrentPos++; // Gets us past the endChar

	xmlReadDone();
}

// Returns memory error. If error, caller must deal with the fact that the end-character hasn't been reached
Error XMLDeserializer::readStringUntilChar(String* string, char endChar) {

	int32_t newStringPos = 0;

	do {
		int32_t bufferPosNow = fileReadBufferCurrentPos;
		while (bufferPosNow < currentReadBufferEndPos && fileClusterBuffer[bufferPosNow] != endChar) {
			bufferPosNow++;
		}

		int32_t numCharsHere = bufferPosNow - fileReadBufferCurrentPos;

		if (numCharsHere) {
			Error error =
			    string->concatenateAtPos(&fileClusterBuffer[fileReadBufferCurrentPos], newStringPos, numCharsHere);

			fileReadBufferCurrentPos = bufferPosNow;

			if (error != Error::NONE) {
				return error;
			}

			newStringPos += numCharsHere;
		}

	} while (fileReadBufferCurrentPos == currentReadBufferEndPos && readXMLFileClusterIfNecessary());

	fileReadBufferCurrentPos++; // Gets us past the endChar

	xmlReadDone();
	return Error::NONE;
}

char const* XMLDeserializer::readUntilChar(char endChar) {
	int32_t charPos = 0;

	do {
		int32_t bufferPosAtStart = fileReadBufferCurrentPos;
		while (fileReadBufferCurrentPos < currentReadBufferEndPos
		       && fileClusterBuffer[fileReadBufferCurrentPos] != endChar) {
			fileReadBufferCurrentPos++;
		}

		// If possible, just return a pointer to the chars within the existing buffer
		if (!charPos && fileReadBufferCurrentPos < currentReadBufferEndPos) {
			fileClusterBuffer[fileReadBufferCurrentPos] = 0;

			fileReadBufferCurrentPos++; // Gets us past the endChar
			return &fileClusterBuffer[bufferPosAtStart];
		}

		int32_t numCharsHere = fileReadBufferCurrentPos - bufferPosAtStart;
		int32_t numCharsToCopy = std::min<int32_t>(numCharsHere, kFilenameBufferSize - 1 - charPos);

		if (numCharsToCopy > 0) {
			memcpy(&stringBuffer[charPos], &fileClusterBuffer[bufferPosAtStart], numCharsToCopy);

			charPos += numCharsToCopy;
		}

	} while (fileReadBufferCurrentPos == currentReadBufferEndPos && readXMLFileClusterIfNecessary());

	fileReadBufferCurrentPos++; // Gets us past the endChar

	xmlReadDone();

	stringBuffer[charPos] = 0;
	return stringBuffer;
}

// Unlike readUntilChar(), above, does not put a null character at the end of the returned "string". And, has a preset
// number of chars. And, returns NULL when nothing more to return. numChars must be <= FILENAME_BUFFER_SIZE
char const* XMLDeserializer::readNextCharsOfTagOrAttributeValue(int32_t numChars) {

	int32_t charPos = 0;

	do {
		int32_t bufferPosAtStart = fileReadBufferCurrentPos;
		int32_t bufferPosAtEnd = bufferPosAtStart + numChars - charPos;

		int32_t currentReadBufferEndPosNow = std::min<int32_t>(currentReadBufferEndPos, bufferPosAtEnd);

		while (fileReadBufferCurrentPos < currentReadBufferEndPosNow) {
			if (fileClusterBuffer[fileReadBufferCurrentPos] == charAtEndOfValue) {
				goto reachedEndCharEarly;
			}
			fileReadBufferCurrentPos++;
		}

		int32_t numCharsHere = fileReadBufferCurrentPos - bufferPosAtStart;

		// If we were able to just read the whole thing in one go, just return a pointer to the chars within the
		// existing buffer
		if (numCharsHere == numChars) {
			xmlReadDone();
			return &fileClusterBuffer[bufferPosAtStart];
		}

		// Otherwise, so long as we read something, add it to our buffer we're putting the output in
		if (numCharsHere > 0) {
			memcpy(&stringBuffer[charPos], &fileClusterBuffer[bufferPosAtStart], numCharsHere);

			charPos += numCharsHere;

			// And if we've now got all the chars we needed, return
			if (charPos == numChars) {
				xmlReadDone();
				return stringBuffer;
			}
		}

	} while (fileReadBufferCurrentPos == currentReadBufferEndPos && readXMLFileClusterIfNecessary());

	// If we're here, the file ended
	return NULL;

	// And, additional bit we jump to when end-char reached
reachedEndCharEarly:
	fileReadBufferCurrentPos++; // Gets us past the endChar
	if (charAtEndOfValue == '<') {
		xmlArea = IN_TAG_NAME;
	}
	else {
		xmlArea = IN_TAG_PAST_NAME; // Could be ' or "
	}
	return NULL;
}

// This is almost never called now - TODO: get rid
char XMLDeserializer::readNextCharOfTagOrAttributeValue() {

	char thisChar;
	if (!readCharXML(&thisChar)) {
		return 0;
	}
	if (thisChar == charAtEndOfValue) {
		if (charAtEndOfValue == '<') {
			xmlArea = IN_TAG_NAME;
		}
		else {
			xmlArea = IN_TAG_PAST_NAME; // Could be ' or "
		}
		xmlReadDone();
		return 0;
	}
	return thisChar;
}

// Will always skip up until the end-char, even if it doesn't like the contents it sees
int32_t XMLDeserializer::readIntUntilChar(char endChar) {
	uint32_t number = 0;
	char thisChar;

	if (!readCharXML(&thisChar)) {
		return 0;
	}

	bool isNegative = (thisChar == '-');
	if (!isNegative) {
		goto readDigit;
	}

	while (readCharXML(&thisChar)) {
readDigit:
		if (!(thisChar >= '0' && thisChar <= '9')) {
			goto getOut;
		}
		number *= 10;
		number += (thisChar - '0');
	}

	if (false) {
getOut:
		if (thisChar != endChar) {
			skipUntilChar(endChar);
		}
	}

	if (isNegative) {
		if (number >= 2147483648) {
			return -2147483648;
		}
		else {
			return -(int32_t)number;
		}
	}
	else {
		return number;
	}
}

char const* XMLDeserializer::readTagOrAttributeValue() {

	switch (xmlArea) {

	case BETWEEN_TAGS:
		xmlArea = IN_TAG_NAME; // How it'll be after this call
		return readUntilChar('<');

	case PAST_ATTRIBUTE_NAME:
	case PAST_EQUALS_SIGN:
		return readAttributeValue();

	case IN_TAG_PAST_NAME: // Could happen if trying to read a value but instead of a value there are multiple more
	                       // contents, like attributes etc. Obviously not "meant" to happen, but we need to cope.
		return "";

	default:
		FREEZE_WITH_ERROR("BBBB");
		__builtin_unreachable();
	}
}

int32_t XMLDeserializer::readTagOrAttributeValueInt() {

	switch (xmlArea) {

	case BETWEEN_TAGS:
		xmlArea = IN_TAG_NAME; // How it'll be after this call
		return readIntUntilChar('<');

	case PAST_ATTRIBUTE_NAME:
	case PAST_EQUALS_SIGN:
		return readAttributeValueInt();

	case IN_TAG_PAST_NAME: // Could happen if trying to read a value but instead of a value there are multiple more
	                       // contents, like attributes etc. Obviously not "meant" to happen, but we need to cope.
		return 0;

	default:
		FREEZE_WITH_ERROR("BBBB");
		__builtin_unreachable();
	}
}

// This isn't super optimal, like the int32_t version is, but only rarely used
int32_t XMLDeserializer::readTagOrAttributeValueHex(int32_t errorValue) {
	char const* string = readTagOrAttributeValue();
	if (string[0] != '0' || string[1] != 'x') {
		return errorValue;
	}
	return hexToInt(&string[2]);
}

int XMLDeserializer::readTagOrAttributeValueHexBytes(uint8_t* bytes, int32_t maxLen) {
	switch (xmlArea) {

	case BETWEEN_TAGS:
		xmlArea = IN_TAG_NAME; // How it'll be after this call
		return readHexBytesUntil(bytes, maxLen, '<');

	case PAST_ATTRIBUTE_NAME:
	case PAST_EQUALS_SIGN:
		if (!getIntoAttributeValue())
			return 0;
		xmlArea = IN_TAG_PAST_NAME; // How it'll be after this next call
		return readHexBytesUntil(bytes, maxLen, charAtEndOfValue);

	case IN_TAG_PAST_NAME: // Could happen if trying to read a value but instead of a value there are multiple more
	                       // contents, like attributes etc. Obviously not "meant" to happen, but we need to cope.
		return 0;

	default:
		FREEZE_WITH_ERROR("BBBB");
		__builtin_unreachable();
	}
}

bool getNibble(char ch, int* nibble) {
	if ('0' <= ch and ch <= '9') {
		*nibble = ch - '0';
	}
	else if ('a' <= ch and ch <= 'f') {
		*nibble = ch - 'a' + 10;
	}
	else if ('A' <= ch and ch <= 'F') {
		*nibble = ch - 'A' + 10;
	}
	else {
		return false;
	}
	return true;
}

int XMLDeserializer::readHexBytesUntil(uint8_t* bytes, int32_t maxLen, char endChar) {
	int read;
	char thisChar;

	for (read = 0; read < maxLen; read++) {
		int highNibble, lowNibble;

		if (!readCharXML(&thisChar))
			return 0;
		if (!getNibble(thisChar, &highNibble)) {
			goto getOut;
		}

		if (!readCharXML(&thisChar))
			return 0;
		if (!getNibble(thisChar, &lowNibble)) {
			goto getOut;
		}

		bytes[read] = (highNibble << 4) + lowNibble;
	}
getOut:
	if (thisChar != endChar) {
		skipUntilChar(endChar);
	}
	return read;
}

// Returns memory error
Error XMLDeserializer::readTagOrAttributeValueString(String* string) {

	Error error;

	switch (xmlArea) {
	case BETWEEN_TAGS:
		error = readStringUntilChar(string, '<');
		if (error == Error::NONE) {
			xmlArea = IN_TAG_NAME;
		}
		return error;

	case PAST_ATTRIBUTE_NAME:
	case PAST_EQUALS_SIGN:
		return readAttributeValueString(string);

	case IN_TAG_PAST_NAME: // Could happen if trying to read a value but instead of a value there are multiple more
	                       // contents, like attributes etc. Obviously not "meant" to happen, but we need to cope.
		return Error::FILE_CORRUPTED;

	default:
		if (ALPHA_OR_BETA_VERSION) {
			FREEZE_WITH_ERROR("BBBB");
		}
		__builtin_unreachable();
	}
}

int32_t XMLDeserializer::getNumCharsRemainingInValue() {

	int32_t pos = fileReadBufferCurrentPos;
	while (pos < currentReadBufferEndPos && fileClusterBuffer[pos] != charAtEndOfValue) {
		pos++;
	}

	return pos - fileReadBufferCurrentPos;
}

// Returns whether we're all good to go
bool XMLDeserializer::prepareToReadTagOrAttributeValueOneCharAtATime() {
	switch (xmlArea) {

	case BETWEEN_TAGS:
		// xmlArea = IN_TAG_NAME; // How it'll be after reading all chars
		charAtEndOfValue = '<';
		return true;

	case PAST_ATTRIBUTE_NAME:
	case PAST_EQUALS_SIGN:
		return getIntoAttributeValue();

	default:
		if (ALPHA_OR_BETA_VERSION) {
			FREEZE_WITH_ERROR("CCCC");
		}
		__builtin_unreachable();
	}
}

// Returns whether successful loading took place
bool XMLDeserializer::readXMLFileClusterIfNecessary() {

	// Load next Cluster if necessary
	if (fileReadBufferCurrentPos >= audioFileManager.clusterSize) {
		xmlReadCount = 0;
		bool result = readXMLFileCluster();
		if (!result) {
			xmlReachedEnd = true;
		}
		return result;
	}

	// Watch out for end of file
	if (fileReadBufferCurrentPos >= currentReadBufferEndPos) {
		xmlReachedEnd = true;
	}

	return false;
}

bool XMLDeserializer::readXMLFileCluster() {

	AudioEngine::logAction("readXMLFileCluster");

	FRESULT result = f_read(&readFIL, (UINT*)fileClusterBuffer, audioFileManager.clusterSize,
	                        &currentReadBufferEndPos);
	if (result) {
		fileAccessFailedDuring = true;
		return false;
	}

	// If error or we reached end of file
	if (!currentReadBufferEndPos) {
		return false;
	}

	fileReadBufferCurrentPos = 0;

	return true;
}

uint32_t XMLDeserializer::readCharXML(char* thisChar) {

	bool stillGoing = readXMLFileClusterIfNecessary();
	if (xmlReachedEnd) {
		return 0;
	}

	*thisChar = fileClusterBuffer[fileReadBufferCurrentPos];

	fileReadBufferCurrentPos++;

	return 1;
}

void XMLDeserializer::exitTag(char const* exitTagName) {
	// back out the file depth to one less than the caller depth
	while (tagDepthFile >= tagDepthCaller) {

		if (xmlReachedEnd) {
			return;
		}

		switch (xmlArea) {

		case IN_ATTRIBUTE_VALUE: // Could get left in here after a char-at-a-time read
			skipUntilChar(charAtEndOfValue);
			xmlArea = IN_TAG_PAST_NAME;
			// No break

		case IN_TAG_PAST_NAME:
			readNextAttributeName();
			break;

		case PAST_ATTRIBUTE_NAME:
		case PAST_EQUALS_SIGN:
			readAttributeValue();
			break;

		case BETWEEN_TAGS:
			skipUntilChar('<');
			xmlArea = IN_TAG_NAME;
			// Got to next tag start
			// No break

		case IN_TAG_NAME:
			readTagName();
			break;

		default:
			if (ALPHA_OR_BETA_VERSION) {
				FREEZE_WITH_ERROR("AAAA"); // Really shouldn't be possible anymore, I feel fairly certain...
			}
			__builtin_unreachable();
		}
	}
	// It is possible for caller and file tag depths to get out of sync due to faulty error handling
	// On exit reset the caller depth to match tag depth. File depth represents the parsers view of
	// where we are in the xml parsing, caller depth represents the callers view. The caller can be shallower
	// as the file will open past empty or unused tags, but should never be deeper.
	tagDepthCaller = tagDepthFile;
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

Error XMLDeserializer::openXMLFile(FilePointer* filePointer, char const* firstTagName, char const* altTagName,
                                   bool ignoreIncorrectFirmware) {

	AudioEngine::logAction("openXMLFile");

	reset();

	char const* tagName;

	while (*(tagName = readNextTagOrAttributeName())) {

		if (!strcmp(tagName, firstTagName) || !strcmp(tagName, altTagName)) {
			return Error::NONE;
		}

		Error result = tryReadingFirmwareTagFromFile(tagName, ignoreIncorrectFirmware);
		if (result != Error::NONE && result != Error::RESULT_TAG_UNUSED) {
			return result;
		}
		exitTag(tagName);
	}

	f_close(&readFIL);
	return Error::FILE_CORRUPTED;
}

Error XMLDeserializer::tryReadingFirmwareTagFromFile(char const* tagName, bool ignoreIncorrectFirmware) {

	if (!strcmp(tagName, "firmwareVersion")) {
		char const* firmware_version_string = readTagOrAttributeValue();
		firmware_version = FirmwareVersion::parse(firmware_version_string);
	}

	// If this tag doesn't exist, it's from old firmware so is ok
	else if (!strcmp(tagName, "earliestCompatibleFirmware")) {
		char const* firmware_version_string = readTagOrAttributeValue();
		auto earliestFirmware = FirmwareVersion::parse(firmware_version_string);
		if (earliestFirmware > FirmwareVersion::current() && !ignoreIncorrectFirmware) {
			f_close(&readFIL);
			return Error::FILE_FIRMWARE_VERSION_TOO_NEW;
		}
	}

	else {
		return Error::RESULT_TAG_UNUSED;
	}

	return Error::NONE;
}
