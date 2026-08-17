/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "storage/audio/audio_file_manager.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "io/file.hpp"
#include "io/midi/midi_device_manager.h"
#include "io/stream.hpp"
#include "libdeluge/block_device.h"
#include "libdeluge/file_io.h"       // deluge_efatfs_cluster_size
#include "libdeluge/storage_owner.h" // deluge_storage_on_owner
#include "libdeluge/stream_io.h"
#include "libdeluge/system.h" // deluge_in_interrupt
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"

#include "deluge_resource.h" // resource manager: unlease manager-owned SAMPLE clusters
#include "model/sample/sample_cache.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/file_byte_source.h"
#include "storage/cluster/cluster.h"
#include "storage/existence_policy.h"
#include "storage/owner.h" // deluge::storage::Coalescer
#include "storage/storage_manager.h"
#include "storage/wave_table/wave_table.h"
#include "util/string.h"
#include "util/try.h"
#include <cstddef>

#include <new>
#include <string.h>
#include <strings.h>

AudioFileManager audioFileManager{};

// === Resource-manager adopt for AudioFile objects (the unified SDRAM reclaim coordinator) =====
// An AudioFile (Sample/WaveTable) object is adopted as an evictable block: its reasons are manager
// leases (AudioFile::add/removeReason), and when unleased the manager may reclaim it under memory
// pressure via audioFileEvict — erasing it from `audioFiles` and destructing it, which frees its
// clusters (Sample → release_asset) and its wavetable bands (~WaveTable). Replaces the CacheManager
// NO_SONG_AUDIO_FILE_OBJECTS queue. (Note: more correct than the legacy steal path, which freed the
// object's raw memory without ~AudioFile — leaking clusters/bands.)
namespace {
void audioFileEvict(void* /*ctx*/, void* ptr) {
	auto* audioFile = static_cast<AudioFile*>(ptr);
	int32_t i = audioFileManager.audioFiles.searchForExactObject(audioFile);
	if (i >= 0) {
		audioFileManager.audioFiles.erase(audioFileManager.audioFiles.begin() + i);
	}
	audioFile->~AudioFile(); // the manager frees the object's backing right after this
}
} // namespace

// Adopt a freshly placement-new'd AudioFile (call before its first addReason). The manager is the
// sole SDRAM evictor, so adoption must succeed — a missing manager / exhausted chunk table is fatal
// (no legacy fallback). On eviction the manager runs `audioFileEvict` + frees the backing.
void AudioFileManager::adoptAudioFileObject(AudioFile* audioFile) {
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	// Cost OBJECT is modest (a re-scan), but the object is tiny — so the manager's cost-per-byte
	// eviction keeps the descriptor resident over the fat clusters it owns. Pass its allocated size.
	void* p = (mgr != nullptr) ? deluge_resource_adopt(mgr, audioFile, audioFile->allocatedSize(),
	                                                   DELUGE_RESOURCE_COST_OBJECT, nullptr, audioFileEvict)
	                           : nullptr;
	if (p == nullptr) {
		FREEZE_WITH_ERROR("RAF1"); // resource chunk table exhausted adopting an AudioFile object
	}
	// Record the slot handle so the object's reason count (leaseCount / isProjectReferenced) is O(1).
	audioFile->resourceSlot = deluge_resource_slot_of(mgr, p);
}

// Destruct + free an AudioFile object (caller has already removed it from `audioFiles` if present).
// Every AudioFile is manager-adopted, so the free routes through the manager (un-adopt).
void AudioFileManager::destroyAudioFileObject(AudioFile& audioFile) {
	audioFile.~AudioFile();
	deluge_resource_evict_chunk(GeneralMemoryAllocator::get().resourceManager(), &audioFile);
}

AudioFileManager::AudioFileManager() {
	highestUsedAudioRecordingNumber.fill(-1);
	highestUsedAudioRecordingNumberNeedsReChecking.set();
}

void AudioFileManager::firstCardRead() {
	if (cardReadOnce) {
		cardReinserted();
	}
	else {
		init();
	}
}

void AudioFileManager::init() {

	Error error = StorageManager::initSD();
	if (error == Error::NONE) {
		uint32_t clusterBytes = 0;
		// Shouldn't fail right after a successful mount, but never let a 0 cluster size through -
		// Cluster::set_size(0) would make every file read request 0 bytes.
		if (!deluge_efatfs_cluster_size(&clusterBytes)) {
			clusterBytes = Cluster::kSizeFAT16Max;
		}
		Cluster::set_size(clusterBytes);

		D_PRINTLN("Cluster::size  %d clusterSizeMagnitude  %d", Cluster::size, Cluster::size_magnitude);
		cardEjected = false;
		cardReadOnce = true;
	}

	else {
		Cluster::set_size(Cluster::kSizeFAT16Max);
		cardEjected = true;
	}

	clusterSizeAtBoot = Cluster::size;
}

void AudioFileManager::cardReinserted() {

	cardDisabled = false;
	for (int32_t i = 0; i < kNumAudioRecordingFolders; i++) {
		highestUsedAudioRecordingNumberNeedsReChecking[i] = true;
	}

	// Query the reinserted card's cluster size. On failure (shouldn't happen right after a fresh
	// mount) the out-param is left untouched, so pre-seeding it with the current size makes an
	// unavailable query fall through to the "stayed the same" branch below - never a spurious
	// increased/decreased trip.
	uint32_t newClusterSize = Cluster::size;
	deluge_efatfs_cluster_size(&newClusterSize);

	// If cluster size has increased, we're in trouble
	if (newClusterSize > Cluster::size) {

		// But, if it's still not as big as it was when we booted up, that's still manageable
		if (newClusterSize <= clusterSizeAtBoot) {
			goto clusterSizeChangedButItsOk;
		}

		D_PRINTLN("cluster size increased and we're in trouble");
		cardDisabled = true;
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_REBOOT_TO_USE_THIS_SD_CARD));
	}

	// If cluster size decreased, we have to stop all current samples from ever sounding again. Pretty big trouble
	// really...
	else if (newClusterSize < Cluster::size) {

clusterSizeChangedButItsOk:
		D_PRINTLN("cluster size changed, and smaller than original so it's ok");
		AudioEngine::killAllVoices(); // Will also stop synth voices - too bad.

		for (int32_t e = 0; e < static_cast<int32_t>(audioFiles.size()); e++) {
			AudioFile* thisAudioFile = audioFiles[e];

			// If AudioFile isn't used currently, take this opportunity to remove it from memory
			if (!thisAudioFile->isProjectReferenced()) {
				deleteUnusedAudioFileFromMemory(*thisAudioFile, e);
				e--;
			}

			// Otherwise, mark the sample as unplayable
			else {
				if (thisAudioFile->type == AudioFileType::SAMPLE) {
					((Sample*)thisAudioFile)->unplayable = true;
				}
			}
		}

		// That was all a pain, but now we can update the cluster size
		Cluster::set_size(newClusterSize);
	}

	// Or if cluster size stayed the same...
	else {
		// Go through every Sample in memory
		for (int32_t e = 0; e < static_cast<int32_t>(audioFiles.size()); e++) {

			AudioFile* thisAudioFile = audioFiles[e];

			// If Sample isn't used currently, take this opportunity to remove it from memory
			if (!thisAudioFile->isProjectReferenced()) {
				deleteUnusedAudioFileFromMemory(*thisAudioFile, e);
				e--;
			}

			// Or if it is still used by someone...
			else {
				if (thisAudioFile->type == AudioFileType::SAMPLE) {
					// Check the Sample's file still exists
					char const* filePath = ((Sample*)thisAudioFile)->tempFilePathForRecording.c_str();
					if (!*filePath) {
						filePath = thisAudioFile->filePath.c_str();
					}

					auto sampleStream = deluge::io::Stream::open(filePath, DELUGE_STREAM_READ);
					if (!sampleStream) {
						D_PRINTLN("couldn't open file");
						((Sample*)thisAudioFile)->markAsUnloadable();
						continue;
					}

					auto firstSector = sampleStream->sector_of(0);
					// sampleStream's destructor closes the handle once it goes out of scope below.

					// If we couldn't resolve cluster 0's sector at all, the file is gone/unreadable on the
					// reinserted card. A successful open + resolvable sector 0 is the whole check.
					if (!firstSector) {
						((Sample*)thisAudioFile)->markAsUnloadable();
						continue;
					}

					// Or if we're still here, the file's fine - who knows, maybe it's even fine again after it wasn't
					// for a while (e.g. if the user temporarily had a different card inserted)
					if (((Sample*)thisAudioFile)->unloadable) {
						// It just became loadable again: re-arm the background overview pre-scan so its waveform
						// overview gets rebuilt (the scan skips unloadable samples and may have gone idle) (#4460).
						overviewScanAllDone = false;
					}
					((Sample*)thisAudioFile)->unloadable = false;
				}
			}
		}
	}

	MIDIDeviceManager::readDevicesFromFile(); // Hopefully we can do this now. It'll only happen if it
	                                          // wasn't able to do it before.
}

// Call this after deleting the current (or in other words previous) Song from memory - meaning there won't be any
// further reason we'd ever move these temp samples into the permanent sample folder, meaning we don't want them in
// memory listed with their would-be real permanent filenames. Also, we won't be needing to play them back again. You
// must not call this during the card or audio routines.
void AudioFileManager::deleteAnyTempRecordedSamplesFromMemory() {

	// Also though, in case any of these Samples were still being recorded before the Song-delete, we need to make sure
	// SampleRecorder::cardRoutine() gets called first to "detach" the Sample from the recorder. So, do this:
	AudioEngine::doRecorderCardRoutines();

	for (int32_t e = 0; e < static_cast<int32_t>(audioFiles.size()); e++) {
		AudioFile* audioFile = audioFiles[e];

		if (audioFile->type == AudioFileType::SAMPLE) {
			// If it's a temp-recorded one
			if (!((Sample*)audioFile)->tempFilePathForRecording.empty()) {

				// if (ALPHA_OR_BETA_VERSION && audioFile->numReasons) FREEZE_WITH_ERROR("E281"); // It definitely
				// shouldn't still have any reasons
				//  No - it could still have a reason - the reason of its SampleRecorder. Scenario where this happened
				//  was: recording AudioClip (instance) into Arranger when loading a new song, first causes Arranger
				//  playback to switch to Session playback, which causes finishLinearRecording() on AudioClip, so when
				//  song-swap does happen, the AudioClip no longer has a recorder, so the recorder doesn't clear stuff,
				//  and it's still not quite yet finalized the file, so still holds the "reason" to the Sample.
				//  TODO: although the Sample doesn't store a pointer to the SampleRecorder, we could easily search for
				//  it - and delete it and its "reason"?

				// We know Sample belonged to an AudioClip originally because only those ones can be TEMP
				highestUsedAudioRecordingNumberNeedsReChecking[util::to_underlying(AudioRecordingFolder::CLIPS)] = true;

				// We may have deleted several, so do make sure we go and re-check from 0
				highestUsedAudioRecordingNumber[util::to_underlying(AudioRecordingFolder::CLIPS)] = -1;

				deleteUnusedAudioFileFromMemory(*audioFile, e);
				e--;
			}
		}
	}
}

// Oi, don't even think about modifying this to take a Sample* pointer - cos the whole Sample could get deleted during
// the card access.
Error AudioFileManager::getUnusedAudioRecordingFilePath(std::string& filePath, std::string* tempFilePathForRecording,
                                                        AudioRecordingFolder folder, uint32_t* getNumber,
                                                        const char* channelName, std::string* songName) {
	const auto folderID = util::to_underlying(folder);

	Error error = StorageManager::initSD();
	if (error != Error::NONE) {
		return error;
	}
	// this caches the last used numbers in the main folders to avoid repeated reads. This is probably good there since
	// some people have hundreds of recordings, but it's unnecessary for song specific folders since the number of
	// recordings will be much smaller
	if (highestUsedAudioRecordingNumberNeedsReChecking[folderID]) {

		// Local RAII handle: the destructor closes it on every return path below. (Historically this
		// contrasted with a shared FatFS::Directory global behind a port selector — both are gone:
		// the global went with C-FatFS, and efatfs is the sole filesystem.)
		//
		// TODO(conflation-batch-5): unknown is being treated as absent here -- a non-NOT_FOUND refusal
		// silently skips the REC-number scan below, yet the
		// `highestUsedAudioRecordingNumberNeedsReChecking[folderID] = false` at the end of this block
		// still clears the sticky recheck flag, and the `highestUsedAudioRecordingNumber[folderID]++`
		// just after it still increments a now-stale counter. Fixing this requires deciding what the
		// scan should do about a stale counter and a cleared cache flag; that's a real design decision,
		// not a mechanical fix.
		auto dir = deluge::io::Directory::open(audioRecordingFolderNames[folderID]);
		if (dir.has_value()) {
			while (true) {
				std::optional<DelugeDirEntry> entry = D_TRY_CATCH(dir->read(), error, {
					return Error::SD_CARD; // error if invalid
				});

				if (!entry.has_value()) {
					break; // Break on end of dir
				}
				if (entry->is_directory) {
					continue;
				}

				// Match REC?????.WAV (5 digits), case-insensitive. The recorder only ever writes
				// exactly this 8.3-clean shape (see the `filePath.append(...)` block below), so the
				// long name always equals what the old code read as the FAT short name.
				const char* name = entry->name;
				size_t len = strlen(name);
				if (len == 12 && strncasecmp(name, "REC", 3) == 0 && strncasecmp(name + 8, ".WAV", 4) == 0) {
					int32_t thisSlot = memToUIntOrError(name + 3, name + 8);
					if (thisSlot == -1) {
						continue;
					}

					if (thisSlot > highestUsedAudioRecordingNumber[folderID]) {
						highestUsedAudioRecordingNumber[folderID] = thisSlot;
					}
				}
			}
		}

		highestUsedAudioRecordingNumberNeedsReChecking[folderID] = false;
	}

	highestUsedAudioRecordingNumber[folderID]++;

	D_PRINTLN("new file: --------------  %d", highestUsedAudioRecordingNumber[folderID]);

	filePath = audioRecordingFolderNames[folderID];

	bool doingTempFolder = (folder == AudioRecordingFolder::CLIPS);
	if (doingTempFolder) {
		(*tempFilePathForRecording) = audioRecordingFolderNames[folderID];
		(*tempFilePathForRecording).append("/TEMP");
	}

	// default to putting it in the main folder if the song isn't named
	if (songName->empty()) {
		filePath.append("/REC");
		filePath.append(deluge::string::fromInt(highestUsedAudioRecordingNumber[folderID], 5));
		filePath.append(".WAV");

		if (doingTempFolder) {
			(*tempFilePathForRecording).append(&filePath.c_str()[strlen(audioRecordingFolderNames[folderID])]);
		}
	}
	// otherwise file it under the song name
	else {
		char namedPath[255]{0};
		char tempPath[255]{0};
		int i = 0;
		bool changed = true;

		// Advances `path` (formatted as "<folder>/<songName>/<channelName>_%03d.wav") past every
		// candidate known to be taken, updating the caller's `i` and `changed` as it goes. Returns
		// once `path` names a candidate that is genuinely free (std::nullopt), or Error::SD_CARD the
		// moment a candidate's existence can't be determined -- guessing "free" there would let a
		// recording clobber an existing take.
		auto advanceUntilFree = [&](char* path, size_t pathSize, const char* folder) -> std::optional<Error> {
			for (;;) {
				switch (deluge::storage::presence_of(StorageManager::fileExists(path))) {
				case deluge::storage::Presence::Absent:
					return std::nullopt; // genuinely free -- stop searching
				case deluge::storage::Presence::Undeterminable:
					return Error::SD_CARD;
				case deluge::storage::Presence::Present:
					break; // taken -- advance and retry
				}
				snprintf(path, pathSize, "%s/%s/%s_%03d.wav", folder, songName->c_str(), channelName, i);
				i++;
				changed = true;
			}
		};

		// iterate through the main and temp folders until we find a path that's free in both
		while (changed) {
			changed = false;
			snprintf(namedPath, sizeof(namedPath), "%s/%s/%s_%03d.wav", filePath.c_str(), songName->c_str(),
			         channelName, i);
			if (auto err = advanceUntilFree(namedPath, sizeof(namedPath), filePath.c_str())) {
				return *err;
			}
			if (doingTempFolder) {
				snprintf(tempPath, sizeof(tempPath), "%s/%s/%s_%03d.wav", tempFilePathForRecording->c_str(),
				         songName->c_str(), channelName, i);

				if (auto err = advanceUntilFree(tempPath, sizeof(tempPath), tempFilePathForRecording->c_str())) {
					return *err;
				}
			}
		}
		filePath = namedPath;
		if (doingTempFolder) {
			(*tempFilePathForRecording) = tempPath;
		}
	}

	*getNumber = highestUsedAudioRecordingNumber[folderID];

	return Error::NONE;
}

// Returns false if exists but can't be deleted
bool AudioFileManager::tryToDeleteAudioFileFromMemoryIfItExists(char const* filePath) {
	bool foundExact;

	for (int32_t t = 0; t < 2; t++) { // Got to do this twice, just in case there's a Sample and a WaveTable.

		int32_t i = audioFiles.search(filePath, &foundExact);
		if (!foundExact) {
			return true; // We're fine, it didn't exist
		}

		// Ok, it's in memory. Can we delete it - is it unused?
		AudioFile* audioFile = audioFiles[i];
		if (audioFile->isProjectReferenced()) {
			return false; // Alert - not only is it in memory, but it also can't be deleted
		}

		// Ok, it's unused. Delete it.
		deleteUnusedAudioFileFromMemory(*audioFile, i);
	}
	return true; // We're fine - it got deleted
}

int32_t AudioFileManager::releaseUnleasedAudioFiles() {
	// Walk downwards: deleting shifts every later index down by one, and going backwards
	// means the indices ahead of us are the ones we have already passed.
	int32_t released = 0;
	for (int32_t i = static_cast<int32_t>(audioFiles.size()) - 1; i >= 0; i--) {
		AudioFile* audioFile = audioFiles[i];
		// leaseCount() > 0 covers everything that would make this unsafe: a playing voice,
		// a recorder, a project reference, and the protect-during-setup lease a sample
		// being loaded right now holds. Zero leases means nobody can reach it.
		if (audioFile->isProjectReferenced()) {
			continue;
		}
		deleteUnusedAudioFileFromMemory(*audioFile, i);
		released++;
	}
	return released;
}

void AudioFileManager::deleteUnusedAudioFileFromMemoryIndexUnknown(AudioFile& audioFile) {
	int32_t i = audioFiles.searchForExactObject(&audioFile);
	if (i < 0) {
#if ALPHA_OR_BETA_VERSION
		FREEZE_WITH_ERROR("E401"); // Leo got. And me! But now I've solved.
#endif
	}
	else {
		deleteUnusedAudioFileFromMemory(audioFile, i);
	}
}

void AudioFileManager::deleteUnusedAudioFileFromMemory(AudioFile& audioFile, int32_t i) {

	// Remove AudioFile from memory
	audioFiles.erase(audioFiles.begin() + i);
	// audioFile->remove(); // Remove from the unused AudioFiles list, where this already must have been. Actually
	// no, the destructor does this anyway.
	destroyAudioFileObject(audioFile); // ~AudioFile + free, routed through the manager if adopted
}

bool AudioFileManager::ensureEnoughMemoryForOneMoreAudioFile() {
	try {
		audioFiles.reserve(audioFiles.size() + 1);
		return true;
	} catch (deluge::exception&) {
		return false;
	}
}

Error AudioFileManager::setupAlternateAudioFileDir(std::string& newPath, char const* rootDir,
                                                   const char* songFilenameWithoutExtension) {

	newPath = rootDir;

	newPath.append("/");

	newPath.append(songFilenameWithoutExtension);

	return Error::NONE;
}

Error AudioFileManager::setupAlternateAudioFilePath(std::string& newPath, int32_t dirPathLength, std::string& oldPath) {
	newPath.resize(dirPathLength);
	newPath.append(&oldPath.c_str()[8]);
	Error error = Error::NONE; // The [8] skips us past "SAMPLES/"
	if (error != Error::NONE) {
		return error;
	}

	int32_t pos = dirPathLength;

	while (true) {
		char const* newPathChars = newPath.c_str();
		char const* slashAddress = strchr(&newPathChars[pos], '/');
		if (!slashAddress) {
			break;
		}
		int32_t slashPos = slashAddress - newPathChars;
		newPath[slashPos] = '_';
		pos = slashPos + 1;
	}

	return Error::NONE;
}

bool AudioFileManager::resolveFileSize(std::string& filePath, bool mayReadCard, std::string& usingAlternateLocation,
                                       uint64_t& sizeBytes, Error* error) {
	// Try and load it in.
	if (!mayReadCard) {
		return false; // NB: leaves *error untouched (caller initialised it to NONE) — a "silently absent" miss.
	}

	if (cardDisabled) {
		*error = Error::SD_CARD;
		return false;
	}

	// Deactivated this because it stuff up the sampleI we already found
	/*
	if (!ensureEnoughMemoryForOneMoreSample()) {
	    *error = Error::INSUFFICIENT_RAM;
	    return false;
	}
	*/

	// Look up one proposed name in the (known-to-exist) alternate load dir by full path; on a hit, fill
	// sizeBytes / usingAlternateLocation (and, for SYNTH/KIT presets, rewrite filePath to the
	// now-non-alternate location).
	const auto tryAlternateName = [&](const std::string& proposedFileName) -> bool {
		std::string candidate = alternateAudioFileLoadPath;
		candidate.append("/");
		candidate.append(proposedFileName);
		auto opened = deluge::io::File::open(candidate, DELUGE_FILE_READ);
		// TODO(conflation-batch-5): unknown is being treated as absent here -- a refusal is reported to
		// the caller the same as a genuine miss (returns false, so tryAlternateDir falls through to
		// tryRegularPath), yielding Error::FILE_UNREADABLE ("not here") where Error::SD_CARD ("could
		// not read the card") would be correct.
		if (!opened.has_value()) {
			return false;
		}
		auto sz = opened->size();
		if (!sz.has_value()) {
			return false;
		}
		sizeBytes = *sz;
		usingAlternateLocation = alternateAudioFileLoadPath;
		usingAlternateLocation.append("/");
		usingAlternateLocation.append(proposedFileName);
		if (thingTypeBeingLoaded == ThingType::SYNTH || thingTypeBeingLoaded == ThingType::KIT) {
			// Special rule for loading presets with files in their dedicated "alternate" folder: point the
			// AudioFile's filePath at that location and then treat it as normal (not alternate).
			filePath = usingAlternateLocation;
			usingAlternateLocation.clear();
		}
		return true;
	};

	enum class AltResult { Found, NotFound, HardError };
	// Search the (known-to-exist) alternate load dir: first the long collect-media name — the full original path,
	// only if it's under "SAMPLES/" — then the bare filename (which lets users drop files into instrument folders).
	const auto tryAlternateDir = [&]() -> AltResult {
		if (memcasecmp(filePath.c_str(), "SAMPLES/", 8) == 0) {
			std::string longName;
			*error = setupAlternateAudioFilePath(longName, 0, filePath);
			if (*error != Error::NONE) {
				return AltResult::HardError;
			}
			if (tryAlternateName(longName)) {
				return AltResult::Found;
			}
		}
		return tryAlternateName(getFileNameFromEndOfPath(filePath.c_str())) ? AltResult::Found : AltResult::NotFound;
	};

	// Open the file at its regular path; on success fill sizeBytes. Returns whether it opened.
	const auto tryRegularPath = [&]() -> bool {
		auto opened = deluge::io::File::open(filePath, DELUGE_FILE_READ);
		// TODO(conflation-batch-5): unknown is being treated as absent here -- a refusal is reported to
		// the caller the same as a genuine miss, yielding Error::FILE_UNREADABLE ("not here") where
		// Error::SD_CARD ("could not read the card") would be correct.
		if (!opened.has_value()) {
			return false;
		}
		auto sz = opened->size();
		if (!sz.has_value()) {
			return false;
		}
		sizeBytes = *sz;
		return true; // File auto-closes on scope exit (RAII)
	};

	// If we already know the alternate dir exists there's a high chance the file is in it, so try that first and
	// fall back to the regular path. Otherwise try the regular path first, and only then discover/search the
	// alternate dir. (`alreadyTriedRegular` in the old goto version just prevented looping between the two.)
	if (alternateLoadDirStatus == AlternateLoadDirStatus::DOES_EXIST) {
		switch (tryAlternateDir()) {
		case AltResult::Found:
			return true;
		case AltResult::HardError:
			return false;
		case AltResult::NotFound:
			if (tryRegularPath()) {
				return true;
			}
		}
	}
	else {
		if (tryRegularPath()) {
			return true;
		}
		// Regular path failed — if an alternate dir might exist, open it and search there.
		if (alternateLoadDirStatus == AlternateLoadDirStatus::MIGHT_EXIST) {
			switch (deluge::storage::presence_of(
			    deluge::io::presence_from_open(deluge::io::Directory::open(alternateAudioFileLoadPath.c_str())))) {
			case deluge::storage::Presence::Absent:
				// Genuinely not there: worth remembering, so later lookups skip it.
				alternateLoadDirStatus = AlternateLoadDirStatus::NOT_FOUND;
				*error = Error::FILE_UNREADABLE;
				return false;
			case deluge::storage::Presence::Undeterminable:
				// Could not tell. Leave the status at MIGHT_EXIST so a later attempt asks again —
				// caching this would turn one transient refusal into a permanent wrong answer.
				*error = Error::SD_CARD;
				return false;
			case deluge::storage::Presence::Present:
				break;
			}
			alternateLoadDirStatus = AlternateLoadDirStatus::DOES_EXIST;
			switch (tryAlternateDir()) {
			case AltResult::Found:
				return true;
			case AltResult::HardError:
				return false;
			case AltResult::NotFound:
				break;
			}
		}
	}

	*error = Error::FILE_UNREADABLE;
	return false;
}

AudioFile* AudioFileManager::convertSampleToWaveTable(Sample& foundSample, bool makeWaveTableWorkAtAllCosts,
                                                      Error* error) {
	// Stereo files can never be WaveTables.
	if (foundSample.numChannels != 1) {
		*error = Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO;
		return nullptr;
	}

	// If the user isn't insisting, some signs show we probably don't want to load this as a WaveTable: an
	// AIFF, or a file that neither specifies itself as a wavetable nor has a wavetable-looking length.
	if (!makeWaveTableWorkAtAllCosts) {
		const bool looksLikeWaveTable =
		    foundSample.fileExplicitlySpecifiesSelfAsWaveTable || (foundSample.lengthInSamples & 2047) == 0;
		if (isAiffFilename(foundSample.filePath.c_str()) || !looksLikeWaveTable) {
			*error = Error::FILE_NOT_LOADABLE_AS_WAVETABLE;
			return nullptr;
		}
	}

	void* waveTableMemory = deluge::memory::alloc_external(sizeof(WaveTable), 16);
	if (waveTableMemory == nullptr) {
		*error = Error::INSUFFICIENT_RAM;
		return nullptr;
	}

	auto* newWaveTable = new (waveTableMemory) WaveTable;
	adoptAudioFileObject(newWaveTable); // resource-manager evictable object (before addReason)
	newWaveTable->addReason();          // So it's protected while setting up.
	foundSample.addReason();

	*error = newWaveTable->setupFromSample(foundSample);
	if (*error != Error::NONE) {
		// Release the source Sample's protect-during-setup reason on this path too. The legacy goto
		// (waveTableCloneError) jumped straight to destroy + return and skipped this, leaking a reason and
		// pinning the Sample as project-referenced / non-evictable after a failed conversion.
		foundSample.removeReason("E400");
		destroyAudioFileObject(*newWaveTable);
		return nullptr;
	}

	const auto inserted = audioFiles.insertElement(newWaveTable);
	*error = inserted ? Error::NONE : inserted.error();

	newWaveTable->removeReason("E397");
	foundSample.removeReason("E398");

	if (!inserted) {
		destroyAudioFileObject(*newWaveTable);
		return nullptr;
	}

	return newWaveTable;
}

AudioFile* AudioFileManager::getAudioFileFromFilename(std::string& filePath, bool mayReadCard, Error* error,
                                                      AudioFileType type, bool makeWaveTableWorkAtAllCosts) {

	*error = Error::NONE;

	std::string backedUpFilePath;

	// See if it's already in memory — first by the file's "normal" path.
	bool foundExact;
	int32_t audioFileI = audioFiles.search(filePath.c_str(), &foundExact);

	// If that didn't find it and we're loading a preset (not a Song, not just browsing), also search in memory
	// for the alternate path.
	if (!foundExact
	    && (alternateLoadDirStatus == AlternateLoadDirStatus::MIGHT_EXIST
	        || alternateLoadDirStatus == AlternateLoadDirStatus::DOES_EXIST)
	    && thingTypeBeingLoaded != ThingType::SONG) {
		std::string searchPath = alternateAudioFileLoadPath;
		searchPath.append("/");
		searchPath.append(getFileNameFromEndOfPath(filePath.c_str()));

		audioFileI = audioFiles.search(searchPath.c_str(), &foundExact);
		if (foundExact) {
			// Tiny bit cheeky, but we're going to update the file path actually stored in the AudioFile to
			// reflect this alternate location, which will no longer be considered alternate.
			backedUpFilePath = filePath; // First back up the original filePath, to restore it if we can't use this.
			filePath = searchPath;
		}
	}

	// If we found it resident (directly or in the alternate location)...
	if (foundExact) {
		AudioFile* foundAudioFile = audioFiles[audioFileI];

		// Correct type at the found index...
		if (foundAudioFile->type == type) {
			return foundAudioFile;
		}

		// ...or at an immediate same-path neighbour (the vector is sorted by path, so a Sample and a WaveTable of
		// the same file sit adjacent). Try the one before, then the one after.
		for (const int32_t neighbourI : {audioFileI - 1, audioFileI + 1}) {
			if (neighbourI < 0 || neighbourI >= static_cast<int32_t>(audioFiles.size())) {
				continue;
			}
			AudioFile* neighbour = audioFiles[neighbourI];
			if (neighbour->type == type && strcasecmp(filePath.c_str(), neighbour->filePath.c_str()) == 0) {
				return neighbour;
			}
		}

		// We found the path but not the wanted type. If we want a WaveTable but have the Sample, we can convert.
		if (type == AudioFileType::WAVETABLE) {
			return convertSampleToWaveTable(static_cast<Sample&>(*foundAudioFile), makeWaveTableWorkAtAllCosts, error);
		}

		// Otherwise (want Sample, have WaveTable) we can't convert, so load from card after all — restoring the
		// original filePath if we'd switched to the alternate one (pretty unlikely scenario).
		if (!backedUpFilePath.empty()) {
			filePath = backedUpFilePath;
		}
	}

	uint64_t sizeBytes;
	std::string usingAlternateLocation;
	if (!resolveFileSize(filePath, mayReadCard, usingAlternateLocation, sizeBytes, error)) {
		return nullptr;
	}

	return buildAudioFileFromCard(filePath, usingAlternateLocation, sizeBytes, type, makeWaveTableWorkAtAllCosts,
	                              error);
}

AudioFile* AudioFileManager::buildAudioFileFromCard(const std::string& filePath,
                                                    const std::string& usingAlternateLocation, uint64_t sizeBytes,
                                                    AudioFileType type, bool makeWaveTableWorkAtAllCosts,
                                                    Error* error) {
	// 0-byte files not allowed.
	if (sizeBytes == 0) {
		*error = Error::FILE_CORRUPTED;
		return nullptr;
	}
	// Files bigger than 1GB not allowed.
	if (sizeBytes > kMaxFileSize) {
		*error = Error::FILE_TOO_BIG;
		return nullptr;
	}

	const uint32_t numClusters = ((sizeBytes - 1) >> Cluster::size_magnitude) + 1;
	const int32_t memorySizeNeeded = (type == AudioFileType::SAMPLE) ? sizeof(Sample) : sizeof(WaveTable);

	void* audioFileMemory = deluge::memory::alloc_external(memorySizeNeeded, 16);
	if (audioFileMemory == nullptr) {
		*error = Error::INSUFFICIENT_RAM;
		return nullptr;
	}

	AudioFile* audioFile;
	if (type == AudioFileType::SAMPLE) {
		audioFile = new (audioFileMemory) Sample;
		adoptAudioFileObject(audioFile); // resource-manager evictable object (before addReason)
		audioFile->addReason(); // So it's protected while setting up. Must do this before calling initialize().
		*error = static_cast<Sample*>(audioFile)->initialize(numClusters);
		if (*error != Error::NONE) { // Very rare, only if not enough RAM
			destroyAudioFileObject(*audioFile);
			return nullptr;
		}

		audioFile->filePath = filePath;
		audioFile->loadedFromAlternatePath = usingAlternateLocation;

		// Open the stream_io.h boundary once for this Sample's lifetime; the per-cluster playback reads
		// (the async streaming-fill task, over this Sample's efatfs handle) go through it.
		//
		// `filePath` is only the file's *actual* on-disk location when it wasn't resolved via the
		// alternate-load-dir mechanism (see resolveFileSize): when `usingAlternateLocation` is
		// non-empty, that's where the bytes backing `sizeBytes` really live (filePath stays
		// the nominal/display path). Must open the same file sizeBytes was resolved from, or
		// numClusters (computed from sizeBytes) mismatches the opened file's real
		// size/cluster layout.
		Sample* sampleFile = static_cast<Sample*>(audioFile);
		const std::string& pathToOpen = usingAlternateLocation.empty() ? filePath : usingAlternateLocation;
		Error openStreamError = sampleFile->stream().open_read_stream(pathToOpen);
		if (openStreamError != Error::NONE) {
			*error = openStreamError;
			destroyAudioFileObject(*audioFile);
			return nullptr;
		}

		// The byte source reads the header raw off the sample's efatfs handle (through the file-io boundary),
		// block by block, taking no manager lease and touching no StreamedChunk.
		FileByteSource source{std::make_unique<ReadSourceBlockReader>(sampleFile->stream().make_read_source()),
		                      static_cast<uint32_t>(sizeBytes)};
		*error = audioFile->loadFile(source, makeWaveTableWorkAtAllCosts);

		// loadFile() parses the WAV header, which is what finally populates the Sample's geometry
		// (byteDepth/numChannels/audioDataStartPosBytes/audioDataLengthBytes). Both earlier
		// registrations of the streaming fill-context -- open_read_stream() above and the header-parse
		// getCluster's define_asset() -- ran while that geometry was still zero, so the context was
		// snapshotted with a zero frame stride. Refresh it now that the geometry is final; otherwise
		// every fill-context consumer (the sample range-reader and its passive peek) resolves a
		// zero-stride geometry and returns nothing for this sample.
		if (*error == Error::NONE) {
			sampleFile->stream().register_fill_context();
		}
	}
	else {
		audioFile = new (audioFileMemory) WaveTable;
		adoptAudioFileObject(audioFile); // resource-manager evictable object (before addReason)
		audioFile->addReason();          // So it's protected while setting up.

		audioFile->filePath = filePath;
		audioFile->loadedFromAlternatePath = usingAlternateLocation;

		// WaveTable reads the file more normally, so open it for the deserializer to stream from. `filePath`
		// is already the correct final resolved path (regular or alternate) at this point, and Tier 2's
		// adapter-side directory cache makes this reopen fast even though the file was just opened once
		// already (in tryRegularPath, to discover sizeBytes).
		auto opened = deluge::io::File::open(filePath, DELUGE_FILE_READ);
		if (!opened) {
			*error = Error::FILE_NOT_FOUND;
			destroyAudioFileObject(*audioFile);
			return nullptr;
		}
		smDeserializer.file = std::move(opened.value());

		// One deserializer-backed source serves both the header parse (via the AudioByteSource surface) and
		// WaveTable::setup's zero-copy band read (via its cluster accessors) — hence passed both ways.
		FileByteSource source{std::make_unique<DeserializerBlockReader>(), static_cast<uint32_t>(sizeBytes)};
		*error = audioFile->loadFile(source, makeWaveTableWorkAtAllCosts, &source);
	}

	if (*error != Error::NONE) {
		// ~AudioFile (for a Sample, via ~SampleStream::release_asset) releases the streaming asset back to
		// the resource manager, freeing any resident clusters; destroyAudioFileObject also un-adopts +
		// frees (through the manager if adopted).
		destroyAudioFileObject(*audioFile);
		return nullptr;
	}

	const auto insertedFile = audioFiles.insertElement(audioFile);
	if (!insertedFile) {
		*error = insertedFile.error();
		destroyAudioFileObject(*audioFile);
		return nullptr;
	}

	audioFile->finalizeAfterLoad(sizeBytes);
	overviewScanAllDone = false; // A newly loaded audio file may need pre-scanning (#4460)

	audioFile->removeReason("E399"); // Setup done; drop the protect-during-setup reason (the caller re-leases).
	return audioFile;
}

// Only needs calling a couple times per second. Must be called outside of the audio / SD-reading routine
// Call this repeatedly so SD card is re-initialized on re-insert before we actually urgently need audio from it
namespace {
// Plain coalesced dispatch of the card re-init onto the storage owner. No lifetime
// coupling (unlike the recorder) → plain Owner::run, not the SD-routine flavor.
// Single-flight so a slow initSD on the fiber can't stack across slowRoutine ticks.
deluge::storage::Coalescer g_card_init_coalescer{/*sd_routine=*/false};

void card_init_fill(void*) {
	audioFileManager.reinitEjectedCard();
}
} // namespace

void AudioFileManager::reinitEjectedCard() {
	if (!cardEjected) {
		return;
	}
	if (!StorageManager::checkSDPresent()) {
		return; // Card still not back - keep retrying on the next slowRoutine tick.
	}

	// Card's back: rebuild efatfs against the (possibly new) card - drop the stale FS mounted
	// against the old card and reset the four handle tables - then explicitly re-validate the
	// resident samples. We can't route this through initSD(): deluge_efatfs_remount() leaves
	// deluge_efatfs_is_mounted() reading true, so initSD()'s !wasMounted->mounted transition gate
	// (which is what fires firstCardRead()) would never trip on a swap.
	if (!deluge_efatfs_remount()) {
		return; // Remount failed (e.g. unreadable card) - stay ejected, retry next tick.
	}
	cardEjected = false;
	firstCardRead(); // cardReadOnce is already true here, so this routes to cardReinserted().
}

void AudioFileManager::slowRoutine() {

	// Drain card-detect events from the BSP (pull-based; the card-detect ISR
	// records them, we poll here). An ejection marks the card unusable until it
	// is re-initialised by the re-insert path below.
	if (deluge_block_poll_card_event(deluge_block_sd_unit()) == DELUGE_CARD_EVENT_EJECTED) {
		setCardEjected();
	}

	// If we know the card's been ejected, re-init via the storage owner (inline on
	// legacy/host → byte-identical; coalesced on the fiber on Embassy). The fill
	// re-checks cardEjected, so a coalesced-away duplicate is a safe no-op.
	if (cardEjected && !isSDRoutineActive() && !deluge_in_interrupt()) {
		if (deluge_storage_on_owner()) {
			card_init_fill(nullptr);
		}
		else {
			g_card_init_coalescer.request(card_init_fill, nullptr);
		}
	}

	// NOTE: (Kate) There was dead code here referencing things that no longer
	// exist (NUM_LOADED_SAMPLE_CHUNK_ALLOCATION_QUEUES, availableClusterQueues)
	// It has been removed.
	// see
	// https://github.com/SynthstromAudible/DelugeFirmware/blob/866a71d0394e259a5b3db9d4fde605511bd1c67d/src/deluge/storage/audio/audio_file_manager.cpp#L1238
	// for a copy if ever needed

	backgroundWaveformOverviewScan();
}

// Background "waveform overview" pre-scan (issue #4460). Walks loaded Samples a little at a time, off the
// render path, caching each cluster's min/max so zoomed-out single-row rendering (song row view) never has
// to load clusters synchronously while the user scrolls. Heavily throttled and round-robined to stay out of
// the way of playback streaming.
void AudioFileManager::backgroundWaveformOverviewScan() {

	// Everything already pre-scanned: stay idle until a load/reset re-arms us, instead of re-walking all
	// files every tick (#4460).
	if (overviewScanAllDone) {
		return;
	}

	// Can we scan at all right now? (The finer-grained "is card I/O safe this instant?" busy-check lives in
	// advanceOverviewScan, which guards each individual cluster load - see waveform_renderer.cpp.)
	if (cardEjected || cardDisabled) {
		return;
	}

	int32_t numFiles = static_cast<int32_t>(audioFiles.size());
	if (numFiles == 0) {
		return;
	}

	// One sample's worth of work per call, round-robined so no single sample starves the others.
	for (int32_t tried = 0; tried < numFiles; tried++) {
		if (overviewScanFileIndex >= numFiles) {
			overviewScanFileIndex = 0;
		}
		AudioFile* audioFile = audioFiles[overviewScanFileIndex];
		overviewScanFileIndex++;

		if (!AudioFile::isSample(audioFile)) {
			continue;
		}

		// Skip samples whose clusters can never load - re-scanning them would spin forever and prevent us
		// from ever going idle. They re-arm the scan if they become loadable again (#4460).
		Sample* sample = (Sample*)audioFile;
		if (sample->unloadable) {
			continue;
		}

		// Advance this sample by a small budget. If there was real work to do, stop here for this call.
		if (waveformRenderer.advanceOverviewScan(sample, kOverviewScanClustersPerCall)) {
			return;
		}
	}

	// A full pass found nothing left to scan: go idle until something re-arms us.
	overviewScanAllDone = true;
}

bool AudioFileManager::cardUnavailableForStreaming() const {
	return cardEjected || cardDisabled || !StorageManager::checkSDInitialized();
}

// Caller must also set alternateAudioFileLoadPath.
void AudioFileManager::thingBeginningLoading(ThingType newThingType) {
	alternateLoadDirStatus = AlternateLoadDirStatus::MIGHT_EXIST;
	thingTypeBeingLoaded = newThingType;
}

void AudioFileManager::thingFinishedLoading() {
	alternateAudioFileLoadPath.clear();
	alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;
	thingTypeBeingLoaded = ThingType::NONE;
}
