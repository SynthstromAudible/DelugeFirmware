/*
 * Copyright © 2026 Synthstrom Audible Limited
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
/// Host seams for the deserializer stack (storage/file_reader.cpp, Deserializer.cpp,
/// JsonDeserializer.cpp), which the unit tests link for real.
///
/// Everything here is a boundary the reader only touches to be polite -- the periodic "keep the audio
/// engine and display alive while I chew through a file" callbacks, and the SDRAM heap it takes its
/// cluster buffer from. None of it participates in parsing, so a host stand-in cannot change what a
/// walk decodes. The deserializer objects themselves are the real ones; only their storage lives here,
/// because the firmware defines them in storage_manager.cpp alongside the whole instrument model.

#include "gui/ui_timer_manager.h"
#include "storage/byte_channel.h"
#include "storage/cluster/cluster.h"
#include "storage/owner.h"
#include "storage/storage_manager.h"

#include <cstdio>
#include <cstdlib>

extern "C" {
#include "libdeluge/control_surface.h"
#include "libdeluge/display.h"
}

// The card's cluster size. The firmware learns this from the mounted volume (cluster.cpp); the host
// tests never mount one, so pin the 32 KiB the deserializers' own buffer allocation assumes.
size_t Cluster::size = 32768;

namespace deluge::memory {
// The reader's 32 KiB cluster buffer. On the device this comes from the SDRAM heap; here it is plain
// host memory, which the reader cannot tell apart -- it only ever reads and writes bytes through it.
void* alloc_sdram(std::size_t size, std::size_t align) {
	return std::aligned_alloc(align, ((size + align - 1) / align) * align);
}
// Deliberately does NOT free. ~FileReader() hands back `fileClusterBuffer`, which is the allocation
// base plus CACHE_LINE_SIZE -- an interior pointer that libc's free() rejects outright ("free():
// invalid size"). The firmware never notices because these readers are statics it never destroys; the
// only calls here are at process exit, so leaking is both harmless and the honest stand-in. Do not
// "fix" this by guessing the offset back off: that would encode the caller's padding in the allocator.
void dealloc(void* /*ptr*/) {
}
} // namespace deluge::memory

// The storage owner and the byte channel, as they exist on a host with neither.
//
// StorageManager::openDocumentStaged() consults these to choose an arm. `started()` is false because
// no owner has ever run here, and that alone routes every staged open to its synchronous arm -- which
// is the arm these specs want, since a request issued from a test that never yields could not be
// answered by anything. The byte-channel pair below is therefore UNREACHABLE from the specs; it is
// defined so the link succeeds, and answers "nothing is known" rather than inventing a result.
namespace deluge::storage {
bool Owner::on_owner() {
	return false;
}
bool Owner::started() {
	return false;
}
} // namespace deluge::storage

namespace deluge::storage::bytes {
ByteIo requestStat(const char* /*path*/) {
	return ByteIo::Failed;
}
ByteIo collectStat(const char* /*path*/, uint32_t* /*outSize*/, DelugeStatus* /*outStatus*/) {
	return ByteIo::Unknown;
}
} // namespace deluge::storage::bytes

namespace AudioEngine {
void logAudioAction(char const* /*string*/, const char* /*file*/, int /*line*/) {
}
// FileReader::readDone() calls this every 64th read so a long parse does not starve audio. Nothing is
// rendering here.
void routineWithClusterLoading() {
}
} // namespace AudioEngine

// FileReader::readDone() also pokes the UI timers on the way past. Nothing schedules anything here,
// so the manager exists only to be called: default-constructed (its members already have initialisers)
// and with a routine() that has nothing to service.
UITimerManager::UITimerManager() = default;
void UITimerManager::routine() {
}
UITimerManager uiTimerManager{};

// The other two halves of readDone()'s politeness: push the display and flush the pad-LED queue. No
// display and no pads in a host test.
extern "C" void deluge_display_service(void) {
}
extern "C" void deluge_control_flush(void) {
}

// FREEZE_WITH_ERROR's landing pad: the deserializers assert on malformed documents. On the device this
// halts with a code on the display; in a test it must be loud and immediate, because carrying on past a
// tripped assertion is how a corrupt parse gets mistaken for a decoded one.
extern "C" void freezeWithError(char const* error) {
	std::fprintf(stderr, "freezeWithError(%s)\n", error);
	std::abort();
}

// The deserializer objects the firmware places in SDRAM from storage_manager.cpp, which the host tests
// cannot link (it builds Songs, Kits, Sounds and Drums). Plain host storage instead; the objects are
// the production ones.
FirmwareVersion song_firmware_version = FirmwareVersion::current();
XMLDeserializer smDeserializer;
JsonDeserializer smJsonDeserializer;
FileDeserializer* activeDeserializer = &smDeserializer;
