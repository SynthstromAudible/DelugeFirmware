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

#pragma once

#include <string_view>

// Forward declarations
class DX7Cartridge;
class SoundInstrument;
class String;
enum class Error;

namespace deluge::dx7 {

class DX7Converter {
public:
	/**
	 * Convert a DX7 sysex file to individual XML instrument files
	 *
	 * @param syxPath Path to the .syx file to convert
	 * @return Error::NONE on success, or appropriate error code
	 */
	Error convertSysexToXML(std::string_view syxPath);

private:
	/**
	 * Check if the destination directory already exists
	 *
	 * @param syxFilename The name of the sysex file (without extension)
	 * @return true if directory exists, false otherwise
	 */
	bool destinationExists(const char* syxFilename);

	/**
	 * Create the destination directory structure: SYNTHS/DX7/{syxFilename}/
	 *
	 * @param syxFilename The name of the sysex file (without extension)
	 * @return Error::NONE on success, or appropriate error code
	 */
	Error createDestinationDirectory(const char* syxFilename);

	/**
	 * Convert a single preset to XML and save to disk
	 *
	 * @param cartridge The loaded DX7 cartridge
	 * @param presetIndex Index of the preset to convert (0-based)
	 * @param syxFilename The name of the sysex file (without extension)
	 * @return Error::NONE on success, or appropriate error code
	 */
	Error convertPresetToXML(DX7Cartridge& cartridge, int presetIndex, const char* syxFilename);

	/**
	 * Generate sanitized filename from preset name
	 *
	 * @param presetName The raw preset name from sysex
	 * @param buffer Buffer to store sanitized name
	 * @param bufferSize Size of buffer
	 */
	void generateSanitizedFilename(const char* presetName, char* buffer, size_t bufferSize);

	/**
	 * Build the full path for a preset XML file
	 *
	 * @param syxFilename The name of the sysex file (without extension)
	 * @param presetFilename The sanitized preset filename (without extension)
	 * @param path Output buffer for the full path
	 * @param pathSize Size of path buffer
	 */
	void buildPresetPath(const char* syxFilename, const char* presetFilename, char* path, size_t pathSize);
};

} // namespace deluge::dx7
