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
/// Deliberately separate from the object model it feeds (Song, Kit, Sound, the drums, the clip
/// views): it moves bytes from a file or a buffer into a deserializer and hands back whether the
/// document opened, and knows nothing else. That is what lets a host test link the parser without
/// linking the instrument model behind it.

#include "gui/ui_timer_manager.h"
#include "io/file.hpp"
#include "io/stall_bound.h"
#include "libdeluge/control_surface.h"
#include "libdeluge/display.h"
#include "libdeluge/file_bytes.h"
#include "memory/general_memory_allocator.h"
#include "memory/heaps.h"
#include "processing/engines/audio_engine.h"
#include "storage/byte_channel.h"
#include "storage/cluster/cluster.h"
#include "storage/storage_manager.h"
#include "util/boot.h"

#include <algorithm>
#include <array>
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
	// its FILE_CORRUPTED exit and the staged opener closes again on the error it returns -- and
	// without this the second call dereferences a disengaged optional.
	if (!file) {
		return true;
	}
	auto result = file->close();
	file.reset();
	return result.has_value();
}

Error StorageManager::readWholeFileSynchronously(char const* path, StagingState* out) {
	auto opened = deluge::io::File::open(path, DELUGE_FILE_READ);
	if (!opened) {
		return Error::FILE_NOT_FOUND;
	}
	auto size = opened->size();
	if (!size) {
		return Error::FILE_NOT_FOUND;
	}
	if (*size == 0) {
		// It is there and it is empty. Not the same thing as missing: a zero-byte document simply has
		// no root tag, which is what the streaming reader said about one.
		return Error::FILE_CORRUPTED;
	}
	// alloc_sdram EVICTS rather than failing on a tight heap: the resource manager is the SDRAM
	// heap's sole reclaim coordinator (general_memory_allocator.cpp) and gives up its lowest-value
	// cached chunks, which are recomputable. A null here therefore means eviction ran and STILL
	// could not produce a contiguous buffer -- a real error condition, not a hint to stream instead.
	auto* bytes = static_cast<uint8_t*>(deluge::memory::alloc_sdram(*size));
	if (bytes == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}
	auto read = opened->read(std::span<std::byte>(reinterpret_cast<std::byte*>(bytes), *size));
	if (!read || read->size() != *size) {
		// A short read of a WHOLE document is a failure, not a small success.
		deluge::memory::dealloc(bytes);
		return Error::SD_CARD;
	}
	out->buffer = bytes;
	out->size = *size;
	return Error::NONE;
}

namespace {
/// A released buffer the byte channel was still reading into when its consumer let it go.
struct OrphanedStaging {
	uint8_t* buffer = nullptr;
	uint32_t size = 0;
};
/// The channel admits one operation at a time, so at most one buffer is held at once; the spare slots cover buffers
/// released before the next reap.
constexpr size_t kMaxOrphanedStaging = 4;
std::array<OrphanedStaging, kMaxOrphanedStaging> orphanedStaging{};

void freeStagingBuffer(uint8_t* buffer, uint32_t size) {
#ifdef DELUGE_DETERMINISTIC_ALLOC
	// Sim/golden only, and the mirror of the allocator's zero-on-hand-out: scrub the document on
	// the way OUT so that reading it after this point resolves to defined zeroes rather than to
	// bytes that merely happen to still be there. Freeing a staged document before its
	// deserializer has finished is the mistake this whole design is arranged to prevent.
	// Firmware leaves it off: the memset would cost a megabyte per song load to catch nothing at
	// runtime.
	memset(buffer, 0, size);
#endif
	deluge::memory::dealloc(buffer);
}
} // namespace

void StorageManager::reapOrphanedStaging() {
	for (OrphanedStaging& orphan : orphanedStaging) {
		if (orphan.buffer != nullptr && !deluge_file_bytes_holds(orphan.buffer)) {
			freeStagingBuffer(orphan.buffer, orphan.size);
			orphan = {};
		}
	}
}

int32_t StorageManager::orphanedStagingCount() {
	return static_cast<int32_t>(
	    std::ranges::count_if(orphanedStaging, [](const OrphanedStaging& orphan) { return orphan.buffer != nullptr; }));
}

void StorageManager::releaseStaging(StagingState* state) {
	if (state->buffer != nullptr) {
		// The whole-file read writes into this buffer from the storage tier and cannot be cancelled. A consumer that
		// gives up on a read the channel still holds would otherwise free memory the read is about to write.
		if (deluge_file_bytes_holds(state->buffer)) {
			reapOrphanedStaging();
			auto slot = std::ranges::find_if(orphanedStaging,
			                                 [](const OrphanedStaging& orphan) { return orphan.buffer == nullptr; });
			// No free slot cannot happen with one buffer held at a time; if it did, keeping the buffer for good is the
			// safe failure.
			if (slot != orphanedStaging.end()) {
				*slot = {.buffer = state->buffer, .size = state->size};
				// Reaped by the byte-channel job, which the channel's DELUGE_WAKE_STORAGE runs once it lets go. A run
				// now covers a let-go that landed before the buffer was parked.
				deluge::runtime::appLoop.request(deluge::storage::bytes::serviceJob);
			}
		}
		else {
			freeStagingBuffer(state->buffer, state->size);
		}
	}
	// A fresh state for the next staging, with the caller's stall limit.
	*state = {.stallBound = deluge::io::StallBound{state->stallBound.limitUs()}};
}

namespace {
/// Completed whole-file staging reads served by the channel. See stagingChannelServedOps().
int32_t stagingServedOps = 0;

/// Defer one more caller tick, or give up once the byte channel has finished nothing for the whole stall bound. A
/// stall bound, not a timeout on the read: a refusal behind someone else's long operation, or a wait on this read,
/// keeps waiting for as long as the channel keeps finishing work, and only a channel that has stopped fails the load.
StorageManager::StagedOpen deferStaging(StorageManager::StagingState* state, StorageManager::StagingState::Wait wait) {
	state->wait = wait;
	if (state->stallBound.tick(deluge_file_bytes_progress())) {
		return StorageManager::StagedOpen::Failed;
	}
	return StorageManager::StagedOpen::Deferred;
}
} // namespace

uint64_t StorageManager::stagingNextRunUs(const StagingState& state) {
	const uint64_t now = deluge::runtime::nowUs();
	const uint64_t deadline = state.stallBound.deadlineUs();
	switch (state.wait) {
	case StagingState::Wait::Now:
		return 0;
	case StagingState::Wait::Refused:
		return std::min(deadline, now + deluge::storage::bytes::kRefusedRecheckUs);
	case StagingState::Wait::Answer:
		break;
	}
	return (deadline != deluge::runtime::kNever) ? deadline : now + state.stallBound.limitUs();
}

StorageManager::StagedOpen StorageManager::openDocumentStaged(char const* path, char const* firstTagName,
                                                              char const* altTagName, StagingState* state) {
	AudioEngine::logAction("openDocumentStaged");
	reapOrphanedStaging();

	// During boot the buffer is filled synchronously, which is legal then and needs no channel task
	// to answer. After it the byte channel is always there: its stat sizes the buffer and its read-into
	// fills it.
	const bool booting = !deluge::boot::finished();

	if (!booting) {
		AudioEngine::noteSongStageTick();
	}

	if (booting) {
		if (state->buffer == nullptr && readWholeFileSynchronously(path, state) != Error::NONE) {
			return StagedOpen::Failed;
		}
	}
	else if (!state->issued) {
		// Size first, so the buffer can be allocated exactly. DELUGE_ERR_NO_MEMORY on the collect below
		// is what catches the file growing between this stat and the read. A buffer already sized -- by
		// this stat, or by that retry -- is not sized again: a read refused while the channel is busy is
		// retried on its own, or every refusal would send this back to a fresh stat.
		if (state->buffer == nullptr) {
			uint32_t size = 0;
			DelugeStatus statStatus = DELUGE_OK;
			switch (deluge::storage::bytes::collectStat(path, &size, &statStatus)) {
			case deluge::storage::bytes::ByteIo::Unknown:
				// Ask -- and HONOUR the answer: requestStat reports Failed for a path it cannot key at
				// all, and discarding that leaves this asking a channel that will never reply.
				if (deluge::storage::bytes::requestStat(path) == deluge::storage::bytes::ByteIo::Failed) {
					return StagedOpen::Failed;
				}
				// A refused stat is dropped with no answer, so this may wait on nothing.
				return deferStaging(state, StagingState::Wait::Refused);
			case deluge::storage::bytes::ByteIo::Pending:
				return deferStaging(state, StagingState::Wait::Answer);
			case deluge::storage::bytes::ByteIo::Failed:
				return StagedOpen::Failed;
			case deluge::storage::bytes::ByteIo::Ready:
				state->stallBound.reset();
				break;
			}
			if (size == 0) {
				return StagedOpen::Failed;
			}
			state->buffer = static_cast<uint8_t*>(deluge::memory::alloc_sdram(size));
			if (state->buffer == nullptr) {
				return StagedOpen::Failed;
			}
			state->size = size;
		}
		if (!deluge_file_bytes_request_read_into(path, state->buffer, state->size)) {
			// Refused, not failed: nothing was queued, so ask again next tick.
			return deferStaging(state, StagingState::Wait::Refused);
		}
		state->issued = true;
		state->stallBound.reset();
		state->wait = StagingState::Wait::Answer;
		return StagedOpen::Deferred;
	}
	else {
		uint32_t got = 0;
		DelugeStatus error = DELUGE_OK;
		uint32_t needed = 0;
		const DelugeChannelStatus collected = deluge_file_bytes_collect_read_into(path, &got, &error, &needed);
		if (collected == DELUGE_CHANNEL_PENDING) {
			return deferStaging(state, StagingState::Wait::Answer);
		}
		if (collected == DELUGE_CHANNEL_UNKNOWN) {
			// Never keyed, so nothing is coming. Re-issue next tick rather than wait forever.
			state->issued = false;
			return deferStaging(state, StagingState::Wait::Now);
		}
		if (collected == DELUGE_CHANNEL_FAILED) {
			state->issued = false;
			if (error == DELUGE_ERR_NO_MEMORY) {
				// The file grew between the stat and the read. Resize and try once more; the retry
				// is bounded because a second NO_MEMORY re-enters here with a buffer already that
				// large, and the read then either fits or reports a genuine failure.
				releaseStaging(state);
				state->buffer = static_cast<uint8_t*>(deluge::memory::alloc_sdram(needed));
				if (state->buffer == nullptr) {
					return StagedOpen::Failed;
				}
				state->size = needed;
				state->wait = StagingState::Wait::Now;
				return StagedOpen::Deferred;
			}
			releaseStaging(state);
			return StagedOpen::Failed;
		}
		state->size = got;
		state->issued = false;
		++stagingServedOps;
	}

	// One parse entry point, whichever arm filled the buffer.
	if (openDelugeBuffer(std::span<uint8_t>(state->buffer, state->size), documentIsJson(path), firstTagName, altTagName)
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

int32_t StorageManager::stagingChannelServedOps() {
	return stagingServedOps;
}

bool StorageManager::documentIsJson(char const* path) {
	return strstr(path, ".Json") != nullptr;
}

Error StorageManager::openDocumentSynchronously(char const* path, char const* firstTagName, char const* altTagName) {
	AudioEngine::logAction("openDocumentSynchronously");
	releaseDocumentStaging();
	Error staged = readWholeFileSynchronously(path, &documentStaging);
	if (staged != Error::NONE) {
		return staged;
	}
	Error err = openDelugeBuffer(std::span<uint8_t>(documentStaging.buffer, documentStaging.size), documentIsJson(path),
	                             firstTagName, altTagName);
	if (err != Error::NONE) {
		releaseDocumentStaging();
		return Error::FILE_CORRUPTED;
	}
	return Error::NONE;
}

void StorageManager::releaseDocumentStaging() {
	releaseStaging(&documentStaging);
}

// The memory-backed readers are separate objects from smDeserializer/smJsonDeserializer rather than a
// reuse of them: the default FileReader constructor allocates a 32 KiB SDRAM cluster buffer, while the
// memory constructor allocates nothing and borrows the caller's bytes instead, so a second pair costs
// only the objects. std::optional, because a deserializer holds an open-file handle and so is neither
// copyable nor movable -- emplace() runs the memory constructor in place, which is the only way to
// re-point one of these at a new buffer without duplicating that constructor's initialisation here.
// Both start disengaged, so they are constant-initialised: no static constructor, no allocation. They
// do still GROW .sdram_bss by their own size, and the Rust BSP derives the device's external heap base
// from &__sdram_bss_end (services.rs, deluge_memory_region) -- so the device heap base shifts down and
// the heap shrinks by that much. Harmless at this size, and invisible to the goldens, which run on the Embassy host,
// where
// __sdram_bss_end is `.set` to a fixed host array the linker never moves.
//
// These are the first std::optionals in .sdram_bss, and being disengaged at boot is a fact they INHERIT
// from the boot's zeroing of [__sdram_bss_start, __sdram_bss_end) (src/bsp/rust/src/boot_mem.rs,
// init_sdram_memory) -- nothing else zeroes them. If
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
