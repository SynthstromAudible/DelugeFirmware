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
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

namespace deluge::dx7 {

Error DX7Converter::convertSysexToXML(const char* syxPath) {
	// Extract filename without extension from path
	const char* path_str = syxPath;
	size_t path_len = strlen(syxPath);

	// Find the last slash to get just the filename
	const char* filename_start = path_str;
	for (size_t i = 0; i < path_len; ++i) {
		if (path_str[i] == '/') {
			filename_start = path_str + i + 1;
		}
	}

	// Find the last dot in the filename to strip extension
	const char* extension_start = nullptr;
	for (const char* p = filename_start; p < path_str + path_len; ++p) {
		if (*p == '.') {
			extension_start = p;
		}
	}

	// Copy filename without extension to buffer
	std::array<char, 256> syx_filename{};
	size_t filename_len = 0;
	if (extension_start != nullptr) {
		filename_len = extension_start - filename_start;
	}
	else {
		filename_len = (path_str + path_len) - filename_start;
	}

	if (filename_len >= syx_filename.size()) {
		filename_len = syx_filename.size() - 1;
	}

	strncpy(syx_filename.data(), filename_start, filename_len);
	syx_filename[filename_len] = '\0';

	// Check if destination directory already exists
	if (destinationExists(syx_filename.data())) {
		display->displayPopup("ALREADY CONVERTED");
		return Error::NONE; // Return NONE since we've handled it with the popup
	}

	// Load the sysex file using the same method as DX7Cartridge loading
	DX7Cartridge cartridge;
	constexpr size_t min_size = kSmallSysexSize;

	FatFS::FileInfo fno = D_TRY_CATCH(FatFS::stat(syxPath), error, {
		display->displayError(Error::FILE_NOT_FOUND);
		return Error::FILE_NOT_FOUND;
	});

	FSIZE_t filesize = fno.fsize;
	if (filesize < min_size) {
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

	std::span<std::byte> readbuffer = D_TRY_CATCH(file.read({buffer.get(), static_cast<size_t>(readsize)}), error, {
		display->displayError(Error::FILE_UNREADABLE);
		return Error::FILE_UNREADABLE;
	});

	if (readbuffer.size() < min_size) {
		display->displayError(Error::FILE_UNREADABLE);
		return Error::FILE_UNREADABLE;
	}

	// Load cartridge from buffer
	auto load_error = cartridge.load(readbuffer);
	if (load_error != deluge::l10n::String::EMPTY_STRING) {
		display->displayError(Error::FILE_CORRUPTED);
		return Error::FILE_CORRUPTED;
	}

	// Create destination directory
	Error err = createDestinationDirectory(syx_filename.data());
	if (err != Error::NONE) {
		return err;
	}

	// Convert each preset
	int num_presets = cartridge.numPatches();

	// Remove any existing working animation before showing popup
	display->removeWorkingAnimation();

	// Show "Converting..." popup to indicate activity (0 flashes = persistent)
	display->displayPopup("Converting...", 0, false, 255, 1, PopupType::LOADING);

	int converted_count = 0;
	int skipped_count = 0;

	for (int i = 0; i < num_presets; i++) {
		err = DX7Converter::convertPresetToXML(cartridge, i, syx_filename.data());
		if (err == Error::FILE_ALREADY_EXISTS) {
			// Skip individual presets that already exist
			skipped_count++;
			continue;
		}

		if (err != Error::NONE) {
			// Stop on other failures - cancel popup before showing error
			display->cancelPopup();
			display->displayError(err);
			return err;
		}

		converted_count++;
	}

	// Cancel "Converting..." popup before showing completion message
	display->cancelPopup();

	// Show completion message with counts
	std::array<char, 64> completion_msg{};
	if (skipped_count > 0) {
		snprintf(completion_msg.data(), completion_msg.size(), "%d done, %d skipped", converted_count, skipped_count);
	}
	else {
		snprintf(completion_msg.data(), completion_msg.size(), "%d converted", converted_count);
	}
	display->displayPopup(completion_msg.data());

	return Error::NONE;
}

bool DX7Converter::destinationExists(const char* syxFilename) {
	std::array<char, 256> path{};
	snprintf(path.data(), path.size(), "SYNTHS/DX7/%s", syxFilename);

	FILINFO fno;
	return (f_stat(path.data(), &fno) == FR_OK && (fno.fattrib & AM_DIR) != 0);
}

Error DX7Converter::createDestinationDirectory(const char* syxFilename) {
	std::array<char, 256> path{};
	snprintf(path.data(), path.size(), "SYNTHS/DX7/%s", syxFilename);

	// Use StorageManager's buildPathToFile to create the directory structure
	if (!StorageManager::buildPathToFile(path.data())) {
		return Error::WRITE_FAIL;
	}

	return Error::NONE;
}

Error DX7Converter::convertPresetToXML(DX7Cartridge& cartridge, int presetIndex, const char* syxFilename) {
	// Get preset name
	std::array<char, 11> preset_name{};
	cartridge.getProgramName(presetIndex, preset_name.data());

	// Skip presets with empty names
	if (preset_name[0] == '\0' || (preset_name[0] == ' ' && preset_name[1] == '\0')) {
		return Error::NONE; // Skip silently
	}

	// Sanitize filename
	std::array<char, 256> sanitized_filename{};
	generateSanitizedFilename(preset_name.data(), sanitized_filename.data(), sanitized_filename.size());

	// Check if XML file already exists
	std::array<char, 1024> xml_path{};
	buildPresetPath(syxFilename, sanitized_filename.data(), xml_path.data(), xml_path.size());

	if (StorageManager::fileExists(xml_path.data())) {
		// Skip this preset if file already exists
		return Error::FILE_ALREADY_EXISTS;
	}

	// Use the existing sound editor's current sound
	Sound* current_sound_base = soundEditor.currentSound;
	if (current_sound_base == nullptr) {
		return Error::UNSPECIFIED;
	}
	SoundInstrument* current_sound = nullptr;
	current_sound = static_cast<SoundInstrument*>(current_sound_base);

	// Find the clip that contains this sound for proper saving context
	Clip* clip_for_saving = nullptr;
	for (int32_t i = 0; i < currentSong->sessionClips.getNumElements(); i++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(i);
		if (clip && clip->output == current_sound) {
			clip_for_saving = clip;
			break;
		}
	}

	// Ensure the current source has a DX7 patch
	Source* source = soundEditor.currentSource;
	if (source == nullptr) {
		return Error::UNSPECIFIED;
	}

	DxPatch* patch = source->ensureDxPatch();
	if (patch == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}

	// Apply the preset to the current sound (same as UI does)
	cartridge.unpackProgram(std::span<uint8_t>(patch->params, 156), presetIndex);
	current_sound->killAllVoices();

	// Set the instrument name
	if (preset_name[0] != 0) {
		current_sound->name.set(preset_name.data());
	}

	// Save the current sound to XML using existing save logic
	Error error = StorageManager::createXMLFile(xml_path.data(), smSerializer, true, false);
	if (error != Error::NONE) {
		return error;
	}

	// Write the instrument using the standard save method
	static_cast<Output*>(current_sound)->writeToFile(clip_for_saving, currentSong);
	error = GetSerializer().closeFileAfterWriting(xml_path.data(), "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n",
	                                              "\n</sound>\n");

	return error;
}

void DX7Converter::generateSanitizedFilename(const char* presetName, char* buffer, unsigned int bufferSize) {
	size_t i = 0;
	size_t j = 0;

	while (presetName[i] != '\0' && j < bufferSize - 1) {
		char c = presetName[i];

		// Replace invalid filename characters
		if (c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*' || c == '/' || c == '\\'
		    || c < 32 || c > 126) {
			c = '_'; // Invalid filename characters or non-printable
		}

		buffer[j++] = c;
		i++;
	}

	buffer[j] = '\0';

	// Trim trailing spaces/underscores
	while (j > 0 && (buffer[j - 1] == ' ' || buffer[j - 1] == '_')) {
		buffer[--j] = '\0';
	}
}

void DX7Converter::buildPresetPath(const char* syxFilename, const char* presetFilename, char* path,
                                   unsigned int pathSize) {
	snprintf(path, pathSize, "SYNTHS/DX7/%s/%s.XML", syxFilename, presetFilename);
}

} // namespace deluge::dx7
