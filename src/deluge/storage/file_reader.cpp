/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

/// @file
/// FileReader — the byte pump under every deserializer — and the calls that point one at a document.
///
/// Split out of storage_manager.cpp, which otherwise reaches the whole object model (Song, Kit, Sound,
/// the drums, the clip views) to build what it loads. Nothing here knows any of that: it moves bytes
/// from a file or a buffer into a deserializer and hands back whether the document opened. That is
/// what lets a host test link the parser without linking the instrument model behind it.

#include "gui/ui_timer_manager.h"
#include "io/file.hpp"
#include "libdeluge/control_surface.h"
#include "libdeluge/display.h"
#include "memory/general_memory_allocator.h"
#include "processing/engines/audio_engine.h"
#include "storage/cluster/cluster.h"
#include "storage/storage_manager.h"

#include <cstring>

FileReader::FileReader() {
	void* temp = deluge::memory::alloc_sdram(32768 + CACHE_LINE_SIZE * 2);
	fileClusterBuffer = (char*)temp + CACHE_LINE_SIZE;
}

// Used to read an in-memory file stream.
// Caller 'owns' the memBuffer.
FileReader::FileReader(char* memBuffer, uint32_t bufLen) {
	fileClusterBuffer = memBuffer;
	currentReadBufferEndPos = bufLen;
	memoryBased = true;
	callRoutines = false;
	fileReadBufferCurrentPos = 0;
}

FileReader::~FileReader() {
	if (!memoryBased)
		deluge::memory::dealloc(fileClusterBuffer);
}

void FileReader::resetReader() {
	if (!memoryBased) {
		fileReadBufferCurrentPos = Cluster::size;
		currentReadBufferEndPos = Cluster::size;
	}
	else {
		fileReadBufferCurrentPos = 0;
	}
	readCount = 0;
	reachedBufferEnd = false;
}

// Returns whether successful loading took place
// return true "if still going".
bool FileReader::readFileClusterIfNecessary() {
	if (memoryBased) {
		if (fileReadBufferCurrentPos >= currentReadBufferEndPos) {
			reachedBufferEnd = true;
		}
		return !reachedBufferEnd;
	}
	// Load next Cluster if necessary
	if (fileReadBufferCurrentPos >= Cluster::size) {
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
	if (memoryBased) {
		return true;
	}

	auto result = file->read(std::span<std::byte>(reinterpret_cast<std::byte*>(fileClusterBuffer), Cluster::size));
	if (!result) {
		return false;
	}
	currentReadBufferEndPos = static_cast<uint32_t>(result->size());

	// If error or we reached end of file
	if (!currentReadBufferEndPos) {
		return false;
	}

	fileReadBufferCurrentPos = 0;

	return true;
}

// Similar to readChar, but it does not advance the fileReadBufferCurrentPos.
// If you later want that to happen, you can call readChar then.
bool FileReader::peekChar(char* thisChar) {

	bool stillGoing = readFileClusterIfNecessary();
	if (reachedBufferEnd) {
		return false;
	}

	*thisChar = fileClusterBuffer[fileReadBufferCurrentPos];

	return true;
}

bool FileReader::readChar(char* thisChar) {

	bool stillGoing = readFileClusterIfNecessary();
	if (reachedBufferEnd) {
		return false;
	}

	*thisChar = fileClusterBuffer[fileReadBufferCurrentPos];

	fileReadBufferCurrentPos++;

	return true;
}

// Call various routines 1 out of N times, where N = 64 at present.
void FileReader::readDone() {
	readCount++; // Increment first, cos we don't want to call SD routine immediately when it's 0

	if (!callRoutines) {
		return;
	}

	if (!(readCount & 63)) { // 511 bad. 255 almost fine. 127 almost always fine
		AudioEngine::routineWithClusterLoading();

		uiTimerManager.routine();

		deluge_display_service();
		deluge_control_flush();
	}
}

bool FileReader::closeWriter() {
	if (memoryBased) {
		return true;
	}
	// Already closed. A corrupt document reaches this twice -- XMLDeserializer::openXMLFile() closes on
	// its FILE_CORRUPTED exit and StorageManager::openXMLFile() closes again on the error it returns --
	// and without this the second call dereferences a disengaged optional.
	if (!file) {
		return true;
	}
	auto result = file->close();
	file.reset();
	return result.has_value();
}

Error StorageManager::openXMLFile(char const* path, XMLDeserializer& reader, char const* firstTagName,
                                  char const* altTagName, bool ignoreIncorrectFirmware) {

	AudioEngine::logAction("openXMLFile");
	reader.reset();
	auto opened = deluge::io::File::open(path, DELUGE_FILE_READ);
	if (!opened) {
		return Error::FILE_NOT_FOUND;
	}
	reader.file = std::move(opened.value());
	Error err = reader.openXMLFile(firstTagName, altTagName, ignoreIncorrectFirmware);
	activeDeserializer = &reader;
	if (err == Error::NONE)
		return Error::NONE;
	reader.closeWriter();

	return Error::FILE_CORRUPTED;
}

Error StorageManager::openJsonFile(char const* path, JsonDeserializer& reader, char const* firstTagName,
                                   char const* altTagName, bool ignoreIncorrectFirmware) {

	AudioEngine::logAction("openJsonFile");
	reader.reset();
	auto opened = deluge::io::File::open(path, DELUGE_FILE_READ);
	if (!opened) {
		return Error::FILE_NOT_FOUND;
	}
	reader.file = std::move(opened.value());
	Error err = reader.openJsonFile(firstTagName, altTagName, ignoreIncorrectFirmware);
	activeDeserializer = &reader;
	if (err == Error::NONE)
		return Error::NONE;
	reader.closeWriter();

	return Error::FILE_CORRUPTED;
}

Error StorageManager::openDelugeFile(char const* path, char const* firstTagName, char const* altTagName,
                                     bool ignoreIncorrectFirmware) {
	Error error;
	if (strstr(path, ".Json") != nullptr) {
		error =
		    StorageManager::openJsonFile(path, smJsonDeserializer, firstTagName, altTagName, ignoreIncorrectFirmware);
	}
	else {
		error = StorageManager::openXMLFile(path, smDeserializer, firstTagName, altTagName, ignoreIncorrectFirmware);
	}
	return error;
}

// The memory-backed readers are separate objects from smDeserializer/smJsonDeserializer rather than a
// reuse of them: the default FileReader constructor allocates a 32 KiB SDRAM cluster buffer, while the
// memory constructor allocates nothing and borrows the caller's bytes instead, so a second pair costs
// only the objects. std::optional, because a deserializer holds an open-file handle and so is neither
// copyable nor movable -- emplace() runs the memory constructor in place, which is the only way to
// re-point one of these at a new buffer without duplicating that constructor's initialisation here.
// Both start disengaged, so they are constant-initialised: no static constructor, no allocation. They
// do still GROW .sdram_bss by their own size, and src/bsp/rza1/memory.c derives the device's external
// heap base from &__sdram_bss_end -- so the device heap base shifts down and the heap shrinks by that
// much. Harmless at this size, and invisible to the goldens, which run on the host BSP, where
// src/bsp/host/host_bsp.c `.set`s __sdram_bss_end to a fixed host_sdram array the linker never moves.
//
// These are the first std::optionals in .sdram_bss, and being disengaged at boot is a fact they INHERIT
// from resetprg.c's emptySection(&__sdram_bss_start, &__sdram_bss_end) -- nothing else zeroes them. If
// that memset is ever dropped again (it has been deleted once before), the first emplace() here would
// see a garbage-engaged optional, destroy a garbage XMLDeserializer, and hand its garbage
// fileClusterBuffer to dealloc(). Do not move these out from under that memset.
PLACE_SDRAM_BSS std::optional<XMLDeserializer> smXmlBufferDeserializer;
PLACE_SDRAM_BSS std::optional<JsonDeserializer> smJsonBufferDeserializer;

Error StorageManager::openDelugeBuffer(std::span<uint8_t> buffer, bool isJson, char const* firstTagName,
                                       char const* altTagName, bool ignoreIncorrectFirmware) {
	// Mutable, and genuinely so: the walk NUL-terminates names and values in place, in the caller's
	// bytes (XMLDeserializer::readNextAttributeName, XMLDeserializer::readUntilChar,
	// JsonDeserializer::readUntilChar), none of it conditional on the reader being memory-backed. A
	// span of read-only storage handed here would fault at the first terminator.
	uint8_t* bytes = buffer.data();

	if (isJson) {
		smJsonBufferDeserializer.emplace(bytes, buffer.size());
		Error err = smJsonBufferDeserializer->openJsonFile(firstTagName, altTagName, ignoreIncorrectFirmware);
		activeDeserializer = &*smJsonBufferDeserializer;
		return err == Error::NONE ? Error::NONE : Error::FILE_CORRUPTED;
	}

	smXmlBufferDeserializer.emplace(bytes, buffer.size());
	Error err = smXmlBufferDeserializer->openXMLFile(firstTagName, altTagName, ignoreIncorrectFirmware);
	activeDeserializer = &*smXmlBufferDeserializer;
	return err == Error::NONE ? Error::NONE : Error::FILE_CORRUPTED;
}
