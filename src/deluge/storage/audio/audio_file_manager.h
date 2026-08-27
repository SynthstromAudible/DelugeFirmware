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

#pragma once
#include "definitions_cxx.hpp"
#include "storage/audio/audio_file_vector.h"
#include "storage/cluster/cluster.h"
#include "util/c_string.h"
#include <array>
#include <cstddef>
#include <cstdint>

class Sample;
class SampleCache;
#include <string>
class SampleRecorder;
class Output;

enum class AlternateLoadDirStatus {
	NONE_SET,
	NOT_FOUND,
	MIGHT_EXIST,
	DOES_EXIST,
};

char const* const audioRecordingFolderNames[] = {
    "SAMPLES/CLIPS",
    "SAMPLES/RECORD",
    "SAMPLES/RESAMPLE",
    "SAMPLES/EXPORTS",
};

/*
 * ===================== SD card audio streaming ==================
 *
 * Audio streaming (for Samples and AudioClips) from the SD card functions by loading
 * and caching Clusters of audio data from the SD card. A formatted card will have a
 * cluster size for the filesystem - often 32kB, but it could be as small as 4kB, or even smaller maybe?
 * The Deluge deals in these Clusters, whatever size they may be for the card, which makes
 * sense because one Cluster always exists in one physical place on the SD card (or any disk),
 * so may be easily loaded in one operation by DMA. Whereas consecutive clusters making up an
 * (audio) file are often placed in completely different physical locations.
 *
 * For a Sample associated with a Sound or AudioClip, the Deluge keeps the first two Clusters of that file
 * (from its set start-point and subject to reversing) permanently loaded in RAM, so playback of the
 * Sample may begin instantly when the Sound or AudioClip is played. And if the Sample has a loop-start point,
 * it keeps the first two Clusters from that point permanently loaded too.
 *
 * Then as the Sample plays, the currently-playing Cluster and the next one are kept loaded in RAM.
 * Or rather, as soon as the “play-head” enters a new Cluster, the Deluge immediately enqueues
 * the following Cluster to be loaded from the card ASAP.
 *
 * And then also, loaded Clusters remain loaded/cached in RAM for as long as possible while that RAM
 * isn’t needed for something more important, so they may be played again without having to reload
 * them from the card. Details on that process below.
 *
 * Quick note - Cluster objects are also used (in RAM) to store SampleCache data (which caches
 * Sample data post-repitching or post-pitch-shifting), and “percussive” audio data (“perc” for short)
 * which is condensed data for use by the time-stretching algorithm. The reason for these types
 * of data being housed in Cluster objects is largely legacy, but it also is handy because all
 * Cluster objects are made to be the same size in RAM, so “stealing” one will always make the
 * right amount of space for another (see below to see what “stealing” means).
 */

class AudioFileManager {
public:
	AudioFileManager();

	AudioFileVector audioFiles;

	void init();
	AudioFile* getAudioFileFromFilename(std::string& fileName, bool mayReadCard, Error* error, AudioFileType type,
	                                    bool makeWaveTableWorkAtAllCosts = false);

	bool ensureEnoughMemoryForOneMoreAudioFile();

	void slowRoutine();

	// Background "waveform overview" pre-scan (issue #4460): see implementation for details.
	void backgroundWaveformOverviewScan();
	// How many clusters to investigate per slowRoutine call. Kept small to avoid card/audio contention.
	// One cluster per idle tick: each investigate can trigger a synchronous card read, so keep this at 1
	// to avoid chaining reads against playback streaming (#4460).
	static constexpr int32_t kOverviewScanClustersPerCall = 1;
	int32_t overviewScanFileIndex = 0; // Round-robin cursor over audioFiles for the overview pre-scan
	bool overviewScanAllDone =
	    false; // Set once every loaded sample is fully pre-scanned; re-armed on load/reset (#4460)

	Error setupAlternateAudioFilePath(std::string& newPath, int32_t dirPathLength, const std::string& oldPath) const;
	Error setupAlternateAudioFileDir(std::string& newPath, char const* rootDir,
	                                 const char* songFilenameWithoutExtension);
	/// @brief Whether the SD card is currently unusable for streaming (ejected, disabled, or not
	///        initialized) — the loader pump's card-down gate.
	/// @return `true` if the card cannot be read right now.
	[[nodiscard]] bool cardUnavailableForStreaming() const;
	/// If songname isn't supplied the file is placed in the main recording folder and named as samples/folder/REC###.
	/// If song and channel are supplied then it's placed in samples/folder/song/channel_###
	Error getUnusedAudioRecordingFilePath(std::string& filePath, std::string* tempFilePathForRecording,
	                                      AudioRecordingFolder folder, uint32_t* getNumber, const char* channelName,
	                                      std::string* songName);
	void deleteAnyTempRecordedSamplesFromMemory();
	void deleteUnusedAudioFileFromMemory(AudioFile& audioFile, int32_t i);

	/// @brief Drop every resident AudioFile nothing holds a lease on. Returns how many went.
	///
	/// Residency is normally ended by memory pressure, but a streamed Sample also pins a
	/// scarce *file handle* for its whole residency, and handles run out first: previewing
	/// samples in the browser leaves each one cached, so the handle table fills while the
	/// heap is still nearly empty and opening the next stream fails with
	/// Error::TOO_MANY_OPEN_STREAMS. This gives that resource the reclaim path it was
	/// missing — see SampleStream::open_read_stream(), its only caller.
	///
	/// Safe because `leaseCount() > 0` is what it skips on, which covers a playing voice, a
	/// recorder, a project reference and the protect-during-setup lease held by a sample
	/// mid-load. Cheap to over-call: with nothing reclaimable it just walks the list.
	int32_t releaseUnleasedAudioFiles();
	void deleteUnusedAudioFileFromMemoryIndexUnknown(AudioFile& audioFile);
	bool tryToDeleteAudioFileFromMemoryIfItExists(char const* filePath);

	// Resource-manager (adopt-mode) object lifecycle: register a freshly placement-new'd AudioFile
	// as a manager-evictable block (call before its first addReason); destruct + free one (routed
	// through the manager if adopted, else the legacy heap). Also used by SampleRecorder.
	void adoptAudioFileObject(AudioFile* audioFile);
	void destroyAudioFileObject(AudioFile& audioFile);

	void thingBeginningLoading(ThingType newThingType);
	void thingFinishedLoading();

	void setCardRead() { cardReadOnce = true; }
	void setCardEjected() { cardEjected = true; }

	/// @brief Re-initialise the SD card if it is currently marked ejected, clearing the flag on success.
	///
	/// Touches FatFS (initSD), so it runs on the storage owner — the fill dispatched by slowRoutine().
	/// Re-checks the ejected flag itself, so a coalesced duplicate that arrives after re-init is a safe
	/// no-op.
	void reinitEjectedCard();

	std::string alternateAudioFileLoadPath{};
	AlternateLoadDirStatus alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;
	ThingType thingTypeBeingLoaded = ThingType::NONE;

	std::array<int32_t, kNumAudioRecordingFolders> highestUsedAudioRecordingNumber{};
	std::bitset<kNumAudioRecordingFolders> highestUsedAudioRecordingNumberNeedsReChecking{};
	void firstCardRead();

	// The no-I/O halves of buildAudioFileFromCard's SAMPLE path, public because the C ABI (a later
	// migration step) calls them directly around an async open + header parse. Each validates/finalizes
	// on its own -- a direct caller gets the same guarantees buildAudioFileFromCard's callers always did.
	Sample* beginSampleFromCard(const std::string& filePath, const std::string& usingAlternateLocation,
	                            uint64_t sizeBytes, Error* error);
	AudioFile* finishSampleFromCard(Sample* sampleFile, uint64_t sizeBytes, Error* error);

	/// Upper bound on how many candidate paths proposeLoadCandidates() can propose: the alternate dir's
	/// "collect media" flattened name, its bare-filename fallback, and the regular path.
	static constexpr size_t kMaxLoadCandidates = 3;

	/// @brief Decide, in priority order and without touching the card, which path(s) a load of
	///        @p filePath should try. The async loader (a later migration step) calls this directly as
	///        its only source of truth for candidate paths, so it and resolveFileSize must stay in sync.
	///
	/// When the alternate load dir is known to exist (`alternateLoadDirStatus ==
	/// AlternateLoadDirStatus::DOES_EXIST`), up to three candidates are proposed, in try order: (1) only
	/// for a @p filePath under "SAMPLES/", the "collect media" flattened form (the original path, with
	/// each '/' after "SAMPLES/" replaced by '_'); (2) unconditionally, the bare filename in the
	/// alternate dir -- tried even when (1) applied and would miss, since it also lets users drop files
	/// straight into an instrument's alternate folder; (3) the regular path. Otherwise only the regular
	/// path is proposed (one candidate).
	///
	/// If the flattened-name construction (1) fails, no candidates are proposed at all (matching the
	/// original code's hard-stop on that failure, rather than silently falling back to the weaker
	/// candidates) -- see resolveFileSize's caller-side note on the error code this produces.
	///
	/// NOT a sufficient "only source of truth" on its own when `alternateLoadDirStatus ==
	/// AlternateLoadDirStatus::MIGHT_EXIST` -- the state at the start of every song/preset load, before
	/// anything has probed whether the alternate dir actually exists. In that state this proposes only
	/// the regular path; a caller that needs the alternate-dir candidates too (as resolveFileSize does)
	/// must first resolve `MIGHT_EXIST` to `DOES_EXIST` or `NOT_FOUND` itself -- an I/O probe
	/// (`deluge::io::Directory::open` on `alternateAudioFileLoadPath`) that cannot live in this
	/// I/O-free function -- and only then call this again. Calling this while still `MIGHT_EXIST`
	/// silently yields an incomplete candidate list, not an error.
	///
	/// `const` here describes the signature, not thread-safety: this reads the mutable
	/// `alternateLoadDirStatus` / `alternateAudioFileLoadPath` members, so a caller running it
	/// concurrently with something that writes them (e.g. `thingBeginningLoading`, off a different
	/// task/fiber) can race.
	///
	/// @param filePath The nominal path a load was asked for.
	/// @param out      Array of at least @p max entries to fill with candidates, in try order. Entries
	///                 at or past the returned count (when it exceeds @p max) are left untouched --
	///                 check the return value against @p max before reading @p out, the same as
	///                 `snprintf`'s return-vs-buffer-size contract.
	/// @param max      Capacity of @p out (pass kMaxLoadCandidates for the full list).
	/// @return How many candidates were proposed in total. This can exceed @p max, in which case only
	///         the first @p max were written to @p out -- the return value, not @p max, is how a caller
	///         detects that. Never silently drops the tail.
	[[nodiscard]] size_t proposeLoadCandidates(const std::string& filePath, std::string* out, size_t max) const;

private:
	/// The bare-filename alternate-dir candidate shared by proposeLoadCandidates() and
	/// getAudioFileFromFilename()'s in-memory search. Deliberately just the string construction, not a
	/// routed call to proposeLoadCandidates(): that function's alternate candidates only exist when
	/// `alternateLoadDirStatus == DOES_EXIST` (about to do I/O, so it's worth being sure first), while
	/// the in-memory search is a free sorted-vector lookup that also checks under `MIGHT_EXIST` (no I/O
	/// cost to being speculative) and skips `ThingType::SONG` entirely -- a genuinely different subset,
	/// not a duplicate to collapse. If this naming rule ever changes, change it here once.
	std::string alternateDirBareNameCandidate(const std::string& filePath) const;
	bool cardReadOnce{false};
	bool cardEjected{};
	bool cardDisabled = false;

	uint32_t clusterSizeAtBoot{0};

	void cardReinserted();
	// Resolve a not-already-resident audio file's size by path: opens proposeLoadCandidates()'s
	// candidates, in order, until one exists. Sets `sizeBytes` (+ `usingAlternateLocation`, and may
	// rewrite `filePath` for preset alternates) and returns true on success; returns false on not-found /
	// card-unavailable (with `*error` set, except the !mayReadCard case which leaves it untouched, as
	// before). NB: if proposeLoadCandidates() produced zero candidates (its flattened-name construction
	// failed), this reports the generic Error::FILE_UNREADABLE rather than that specific error code --
	// proposeLoadCandidates has no error channel to carry it. Currently unreachable in practice: the
	// construction it calls (setupAlternateAudioFilePath) can't actually fail.
	bool resolveFileSize(std::string& filePath, bool mayReadCard, std::string& usingAlternateLocation,
	                     uint64_t& sizeBytes, Error* error);
	// Convert an already-in-memory Sample into a WaveTable (the caller wanted a wavetable but only the Sample
	// form is resident). Returns the new WaveTable (held by no reason), or nullptr with `*error` set if it
	// can't be a wavetable (stereo, or not wavetable-looking unless insisted) or alloc/setup fails.
	AudioFile* convertSampleToWaveTable(Sample& foundSample, bool makeWaveTableWorkAtAllCosts, Error* error);
	/// @brief Construct an AudioFile from a resolved file: alloc + adopt the object, build it, insert it
	///        into audioFiles, and finalize.
	///
	/// Sample: FAT-walk the cluster table and raw header parse via FileByteSource. WaveTable: parse + setup
	/// via FileByteSource. The file-resolution that produced @p sizeBytes / @p
	/// usingAlternateLocation is the caller's job.
	/// @param filePath                   The file's (nominal/display) path.
	/// @param usingAlternateLocation     Alternate-load-dir path segment the bytes actually live at, or
	///                                   empty if resolved at `filePath` directly.
	/// @param sizeBytes                  The resolved on-card file's size, in bytes.
	/// @param type                       Which concrete AudioFile subclass to build.
	/// @param makeWaveTableWorkAtAllCosts Force wavetable interpretation even without the tag/length hints
	///                                    that normally identify one.
	/// @param error                      Set on failure.
	/// @return The loaded object, held by no reason (the caller leases it), or nullptr with @p error set.
	AudioFile* buildAudioFileFromCard(const std::string& filePath, const std::string& usingAlternateLocation,
	                                  uint64_t sizeBytes, AudioFileType type, bool makeWaveTableWorkAtAllCosts,
	                                  Error* error);
};

extern AudioFileManager audioFileManager;
