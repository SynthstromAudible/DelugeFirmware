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

#include "dx7_converter.h"
#include "definitions_cxx.hpp"
#include "dsp/dx/dx7note.h"
#include "fatfs/ff.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "memory/allocate_unique.h"
#include "memory/sdram_allocator.h"
#include "model/song/song.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "storage/DX7Cartridge.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include "util/try.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string_view>

namespace deluge::dx7 {

Error DX7Converter::convertSysexToXML(std::string_view syxPath) {
	// Extract filename without extension from path
	const char* pathStr = syxPath.data();
	size_t pathLen = syxPath.length();

	// Find the last slash to get just the filename
	const char* filenameStart = pathStr;
	for (size_t i = 0; i < pathLen; ++i) {
		if (pathStr[i] == '/') {
			filenameStart = pathStr + i + 1;
		}
	}

	// Find the last dot in the filename to strip extension
	const char* extensionStart = nullptr;
	for (const char* p = filenameStart; p < pathStr + pathLen; ++p) {
		if (*p == '.') {
			extensionStart = p;
		}
	}

	// Copy filename without extension to buffer
	char syxFilename[256];
	size_t filenameLen;
	if (extensionStart) {
		filenameLen = extensionStart - filenameStart;
	}
	else {
		filenameLen = (pathStr + pathLen) - filenameStart;
	}

	if (filenameLen >= sizeof(syxFilename)) {
		filenameLen = sizeof(syxFilename) - 1;
	}

	strncpy(syxFilename, filenameStart, filenameLen);
	syxFilename[filenameLen] = '\0';

	// Check if destination directory already exists
	if (destinationExists(syxFilename)) {
		display->displayPopup("ALREADY CONVERTED");
		return Error::NONE; // Return NONE since we've handled it with the popup
	}

	// Load the sysex file using the same method as DX7Cartridge loading
	DX7Cartridge cartridge;
	constexpr size_t minSize = kSmallSysexSize;

	FatFS::FileInfo fno = D_TRY_CATCH(FatFS::stat(syxPath), error, {
		display->displayError(Error::FILE_NOT_FOUND);
		return Error::FILE_NOT_FOUND;
	});

	FSIZE_t filesize = fno.fsize;
	if (filesize < minSize) {
		display->displayError(Error::FILE_UNREADABLE);
		return Error::FILE_UNREADABLE;
	}

	// Open the file
	FatFS::File file = D_TRY_CATCH(FatFS::File::open(syxPath, FA_READ), error, {
		display->displayError(Error::FILE_UNREADABLE);
		return Error::FILE_UNREADABLE;
	});

	int readsize = std::min((int)filesize, 8192);

	std::unique_ptr buffer = D_TRY_CATCH_MOVE((allocate_unique<std::byte, memory::sdram_allocator>(readsize)), error, {
		display->displayError(Error::INSUFFICIENT_RAM);
		return Error::INSUFFICIENT_RAM;
	});

	std::span<std::byte> readbuffer = D_TRY_CATCH(file.read({buffer.get(), filesize}), error, {
		display->displayError(Error::FILE_UNREADABLE);
		return Error::FILE_UNREADABLE;
	});

	if (readbuffer.size() < minSize) {
		display->displayError(Error::FILE_UNREADABLE);
		return Error::FILE_UNREADABLE;
	}

	// Load cartridge from buffer
	auto loadError = cartridge.load(readbuffer);
	if (loadError != deluge::l10n::String::EMPTY_STRING) {
		display->displayError(Error::FILE_CORRUPTED);
		return Error::FILE_CORRUPTED;
	}

	// Create destination directory
	Error err = createDestinationDirectory(syxFilename);
	if (err != Error::NONE) {
		return err;
	}

	// Convert each preset
	int numPresets = cartridge.numPatches();

	// Remove any existing working animation before showing popup
	display->removeWorkingAnimation();

	// Show "Converting..." popup to indicate activity (0 flashes = persistent)
	display->displayPopup("Converting...", 0, false, 255, 1, PopupType::LOADING);

	int convertedCount = 0;
	int skippedCount = 0;

	for (int i = 0; i < numPresets; i++) {
		err = convertPresetToXML(cartridge, i, syxFilename);
		if (err == Error::FILE_ALREADY_EXISTS) {
			// Skip individual presets that already exist
			skippedCount++;
			continue;
		}
		else if (err != Error::NONE) {
			// Stop on other failures - cancel popup before showing error
			display->cancelPopup();
			display->displayError(err);
			return err;
		}
		else {
			convertedCount++;
		}
	}

	// Cancel "Converting..." popup before showing completion message
	display->cancelPopup();

	// Show completion message with counts
	char completionMsg[64];
	if (skippedCount > 0) {
		snprintf(completionMsg, sizeof(completionMsg), "DX7: %d done, %d skipped", convertedCount, skippedCount);
	}
	else {
		snprintf(completionMsg, sizeof(completionMsg), "DX7: %d converted", convertedCount);
	}
	display->displayPopup(completionMsg);

	return Error::NONE;
}

bool DX7Converter::destinationExists(const char* syxFilename) {
	char path[256];
	snprintf(path, sizeof(path), "SYNTHS/DX7/%s", syxFilename);

	FILINFO fno;
	return (f_stat(path, &fno) == FR_OK && (fno.fattrib & AM_DIR));
}

Error DX7Converter::createDestinationDirectory(const char* syxFilename) {
	char path[256];
	snprintf(path, sizeof(path), "SYNTHS/DX7/%s", syxFilename);

	// Use StorageManager's buildPathToFile to create the directory structure
	if (!StorageManager::buildPathToFile(path)) {
		return Error::WRITE_FAIL;
	}

	return Error::NONE;
}

Error DX7Converter::convertPresetToXML(DX7Cartridge& cartridge, int presetIndex, const char* syxFilename) {
	// Get preset name
	char presetName[11];
	cartridge.getProgramName(presetIndex, presetName);

	// Skip presets with empty names
	if (presetName[0] == '\0' || (presetName[0] == ' ' && presetName[1] == '\0')) {
		return Error::NONE; // Skip silently
	}

	// Sanitize filename
	char sanitizedFilename[256];
	generateSanitizedFilename(presetName, sanitizedFilename, sizeof(sanitizedFilename));

	// Check if XML file already exists
	char xmlPath[1024];
	buildPresetPath(syxFilename, sanitizedFilename, xmlPath, sizeof(xmlPath));

	if (StorageManager::fileExists(xmlPath)) {
		// Skip this preset if file already exists
		return Error::FILE_ALREADY_EXISTS;
	}

	// Use the existing sound editor's current sound
	Sound* currentSoundBase = soundEditor.currentSound;
	if (!currentSoundBase) {
		return Error::UNSPECIFIED;
	}
	SoundInstrument* currentSound = static_cast<SoundInstrument*>(currentSoundBase);

	// Find the clip that contains this sound for proper saving context
	Clip* clipForSaving = nullptr;
	for (int32_t i = 0; i < currentSong->sessionClips.getNumElements(); i++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(i);
		if (clip && clip->output == currentSound) {
			clipForSaving = clip;
			break;
		}
	}

	// Ensure the current source has a DX7 patch
	Source* source = soundEditor.currentSource;
	if (!source) {
		return Error::UNSPECIFIED;
	}

	DxPatch* patch = source->ensureDxPatch();
	if (!patch) {
		return Error::INSUFFICIENT_RAM;
	}

	// Apply the preset to the current sound (same as UI does)
	cartridge.unpackProgram(std::span<uint8_t>(patch->params, 156), presetIndex);
	currentSound->killAllVoices();

	// Set the instrument name
	if (presetName[0] != 0) {
		currentSound->name.set(presetName);
	}

	// Save the current sound to XML using existing save logic
	Error error = StorageManager::createXMLFile(xmlPath, smSerializer, true, false);
	if (error != Error::NONE) {
		return error;
	}

	// Write the instrument using the standard save method
	static_cast<Output*>(currentSound)->writeToFile(clipForSaving, currentSong);
	error =
	    GetSerializer().closeFileAfterWriting(xmlPath, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", "\n</sound>\n");

	return error;
}

void DX7Converter::generateSanitizedFilename(const char* presetName, char* buffer, size_t bufferSize) {
	size_t i = 0;
	size_t j = 0;

	while (presetName[i] && j < bufferSize - 1) {
		char c = presetName[i];

		// Convert to uppercase
		c = std::toupper(c);

		// Replace invalid filename characters
		if (c == '<')
			c = '_';
		else if (c == '>')
			c = '_';
		else if (c == ':')
			c = '_';
		else if (c == '"')
			c = '_';
		else if (c == '|')
			c = '_';
		else if (c == '?')
			c = '_';
		else if (c == '*')
			c = '_';
		else if (c == '/')
			c = '_';
		else if (c == '\\')
			c = '_';
		else if (c < 32 || c > 126)
			c = '_'; // Non-printable characters

		buffer[j++] = c;
		i++;
	}

	buffer[j] = '\0';

	// Trim trailing spaces/underscores
	while (j > 0 && (buffer[j - 1] == ' ' || buffer[j - 1] == '_')) {
		buffer[--j] = '\0';
	}
}

void DX7Converter::buildPresetPath(const char* syxFilename, const char* presetFilename, char* path, size_t pathSize) {
	snprintf(path, pathSize, "SYNTHS/DX7/%s/%s.XML", syxFilename, presetFilename);
}

} // namespace deluge::dx7
