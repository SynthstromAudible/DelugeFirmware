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
#include "libdeluge/file_write.h"
#include "memory/general_memory_allocator.h"
#include "memory/heaps.h"
#include "processing/engines/audio_engine.h"
#include "storage/byte_channel.h"
#include "storage/cluster/cluster.h"
#include "storage/owner.h"
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

bool StorageManager::readWholeFileSynchronously(char const* path, StagingState* out) {
	auto opened = deluge::io::File::open(path, DELUGE_FILE_READ);
	if (!opened) {
		return false;
	}
	auto size = opened->size();
	if (!size || *size == 0) {
		return false;
	}
	// alloc_sdram EVICTS rather than failing on a tight heap: the resource manager is the SDRAM
	// heap's sole reclaim coordinator (general_memory_allocator.cpp) and gives up its lowest-value
	// cached chunks, which are recomputable. A null here therefore means eviction ran and STILL
	// could not produce a contiguous buffer -- a real error condition, not a hint to stream instead.
	auto* bytes = static_cast<uint8_t*>(deluge::memory::alloc_sdram(*size));
	if (bytes == nullptr) {
		return false;
	}
	auto read = opened->read(std::span<std::byte>(reinterpret_cast<std::byte*>(bytes), *size));
	if (!read || read->size() != *size) {
		// A short read of a WHOLE document is a failure, not a small success.
		deluge::memory::dealloc(bytes);
		return false;
	}
	out->buffer = bytes;
	out->size = *size;
	return true;
}

void StorageManager::releaseStaging(StagingState* state) {
	if (state->buffer != nullptr) {
		deluge::memory::dealloc(state->buffer);
	}
	*state = {};
}

StorageManager::StagedOpen StorageManager::openDocumentStaged(char const* path, char const* firstTagName,
                                                              char const* altTagName, StagingState* state) {
	AudioEngine::logAction("openDocumentStaged");

	// An async request issued from a context that will not YIELD can never complete: the driver only
	// runs when this caller returns to its executor. On the owner we therefore fill synchronously,
	// which is legal there and is what shipped before this channel existed.
	const bool mayUseChannel =
	    !deluge::storage::Owner::on_owner() && deluge::storage::Owner::started() && deluge_file_write_async_active();

	if (!mayUseChannel) {
		if (state->buffer == nullptr && !readWholeFileSynchronously(path, state)) {
			return StagedOpen::Failed;
		}
	}
	else if (!state->issued) {
		// Size first, so the buffer can be allocated exactly. `out_needed` on the collect below is
		// what catches the file changing between this stat and the read.
		uint32_t size = 0;
		DelugeStatus statStatus = DELUGE_OK;
		switch (deluge::storage::bytes::collectStat(path, &size, &statStatus)) {
		case deluge::storage::bytes::ByteIo::Unknown:
			(void)deluge::storage::bytes::requestStat(path);
			return StagedOpen::Deferred;
		case deluge::storage::bytes::ByteIo::Pending:
			return StagedOpen::Deferred;
		case deluge::storage::bytes::ByteIo::Failed:
			return StagedOpen::Failed;
		case deluge::storage::bytes::ByteIo::Ready:
			break;
		}
		if (size == 0) {
			return StagedOpen::Failed;
		}
		if (state->buffer == nullptr) {
			state->buffer = static_cast<uint8_t*>(deluge::memory::alloc_sdram(size));
			if (state->buffer == nullptr) {
				return StagedOpen::Failed;
			}
			state->size = size;
		}
		if (!deluge_file_write_request_read_file(path, state->buffer, state->size)) {
			// Refused, not failed: nothing was queued, so ask again next tick.
			return StagedOpen::Deferred;
		}
		state->issued = true;
		return StagedOpen::Deferred;
	}
	else {
		uint32_t got = 0;
		uint32_t needed = 0;
		const uint8_t collected = deluge_file_write_collect_read_file(path, &got, &needed);
		if (collected == DELUGE_FILE_WRITE_PENDING) {
			return StagedOpen::Deferred;
		}
		if (collected == DELUGE_FILE_WRITE_UNKNOWN) {
			// Never keyed, so nothing is coming. Re-issue next tick rather than wait forever.
			state->issued = false;
			return StagedOpen::Deferred;
		}
		if (collected == DELUGE_FILE_WRITE_FAILED) {
			state->issued = false;
			if (needed != 0) {
				// The file grew between the stat and the read. Resize and try once more; the retry
				// is bounded because a second FAILED with a needed size re-enters here with a buffer
				// already that large, and the read then either fits or reports a genuine failure.
				releaseStaging(state);
				state->buffer = static_cast<uint8_t*>(deluge::memory::alloc_sdram(needed));
				if (state->buffer == nullptr) {
					return StagedOpen::Failed;
				}
				state->size = needed;
				return StagedOpen::Deferred;
			}
			releaseStaging(state);
			return StagedOpen::Failed;
		}
		state->size = got;
		state->issued = false;
	}

	// One parse entry point, whichever arm filled the buffer. The extension decides the parser,
	// exactly as the file-backed opener decided it.
	const bool isJson = strstr(path, ".Json") != nullptr;
	if (openDelugeBuffer(std::span<uint8_t>(state->buffer, state->size), isJson, firstTagName, altTagName)
	    != Error::NONE) {
		releaseStaging(state);
		return StagedOpen::Failed;
	}
	return StagedOpen::Opened;
}

// One staging slot for the single-shot openers. They are never in flight at once: each is driven by
// a single load* wrapper on one call stack, which opens, parses and closes before returning. If that
// ever stops being true this becomes a silent cross-document corruption, so it is stated here rather
// than left to be inferred from the call graph.
namespace {
StorageManager::StagingState documentStaging;
} // namespace

Error StorageManager::openDocumentSynchronously(char const* path, char const* firstTagName, char const* altTagName) {
	AudioEngine::logAction("openDocumentSynchronously");
	releaseDocumentStaging();
	if (!readWholeFileSynchronously(path, &documentStaging)) {
		return Error::FILE_NOT_FOUND;
	}
	// The extension picks the parser, exactly as the file-backed opener picked it.
	const bool isJson = strstr(path, ".Json") != nullptr;
	Error err = openDelugeBuffer(std::span<uint8_t>(documentStaging.buffer, documentStaging.size), isJson, firstTagName,
	                             altTagName);
	if (err != Error::NONE) {
		releaseDocumentStaging();
		return Error::FILE_CORRUPTED;
	}
	return Error::NONE;
}

void StorageManager::releaseDocumentStaging() {
	releaseStaging(&documentStaging);
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
