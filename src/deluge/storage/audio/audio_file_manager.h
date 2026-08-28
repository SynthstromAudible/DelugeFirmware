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
#include <optional>
#include <string_view>

class Sample;
class SampleCache;
#include <string>
class SampleRecorder;
class Output;

/// A finished async header parse (libdeluge/audio_load.h). Forward-declared rather than included: the
/// C ABI header is only needed where the collector is actually implemented (audio_file_manager.cpp),
/// not by every translation unit that talks to the manager.
struct DelugeAudioLoadResult;

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
	/// @brief Get the AudioFile for @p fileName: from memory if it is already resident, otherwise from
	///        the card.
	///
	/// @param fileName                    The path to load. May be rewritten to the alternate location
	///                                    the file was actually found at (see resolveFileSize).
	/// @param mayReadCard                 false makes this an in-memory lookup only: a miss returns
	///                                    nullptr and leaves @p error untouched.
	/// @param error                       Set on failure. Also set to Error::LOAD_IN_PROGRESS -- which
	///                                    is NOT a failure -- when the async loader has been asked for
	///                                    this file and has not finished yet; see @p mayLoadAsync.
	/// @param type                        Which concrete AudioFile subclass is wanted.
	/// @param makeWaveTableWorkAtAllCosts Force wavetable interpretation even without the tag/length
	///                                    hints that normally identify one.
	/// @param mayLoadAsync                Opt in to the async loader (libdeluge/audio_load.h) for a
	///                                    card miss, instead of the blocking load. Only honoured for
	///                                    AudioFileType::SAMPLE, and only where the async backing is
	///                                    actually active. A caller opting in MUST handle
	///                                    Error::LOAD_IN_PROGRESS -- returning to its own loop and
	///                                    asking again on a later tick, without re-requesting or
	///                                    reporting a failure. Defaulted off: every existing caller
	///                                    keeps the synchronous behaviour it was written against.
	/// @return The AudioFile, held by no reason (the caller leases it), or nullptr with @p error set.
	AudioFile* getAudioFileFromFilename(std::string& fileName, bool mayReadCard, Error* error, AudioFileType type,
	                                    bool makeWaveTableWorkAtAllCosts = false, bool mayLoadAsync = false);

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
	/// missing — see SampleStream::open_read_stream(), and AudioFileManager::answerFailedLoadResult()
	/// which runs the same reclaim when the ASYNC loader's own open hits the full table (it cannot
	/// call this itself: it runs on an executor that must make no synchronous C++ call).
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

	/// @brief Mark the card gone, and discard everything the async loader parsed from it.
	///
	/// Out of line, and not merely for the flush's sake: everything the loader holds is keyed by the
	/// nominal path with no record of which card produced it, so a header parsed from one card and
	/// collected after another is inserted binds the NEW card's file to the OLD card's geometry --
	/// a load that reports no error and plays the wrong thing.
	///
	/// This runs on the interaction tier, the same tier as the collect, so the flush is correctly
	/// ordered against any collect that could otherwise consume a stale result. The re-insert path
	/// (reinitEjectedCard) is deliberately NOT the hook: flushing there would leave the window open
	/// for as long as the slot stayed empty.
	void setCardEjected();

	/// @brief Re-initialise the SD card if it is currently marked ejected, clearing the flag on success.
	///
	/// Touches FatFS (initSD), so it runs on the storage owner — the fill dispatched by slowRoutine().
	/// Re-checks the ejected flag itself, so a coalesced duplicate that arrives after re-init is a safe
	/// no-op.
	void reinitEjectedCard();

	std::string alternateAudioFileLoadPath{};
	AlternateLoadDirStatus alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;
	ThingType thingTypeBeingLoaded = ThingType::NONE;

	/// Set when the card came back with a cluster size larger than this boot sized its buffers for:
	/// the card cannot be used again until reboot, and resolveFileSize refuses every load with
	/// Error::SD_CARD before touching it. Public alongside this class's other card/load state (rather
	/// than private like `cardEjected`, which has its own setter) so the async-load gate that must
	/// honour it can be exercised.
	bool cardDisabled = false;

	std::array<int32_t, kNumAudioRecordingFolders> highestUsedAudioRecordingNumber{};
	std::bitset<kNumAudioRecordingFolders> highestUsedAudioRecordingNumberNeedsReChecking{};
	void firstCardRead();

	// The no-I/O halves of buildAudioFileFromCard's SAMPLE path, public because the C ABI (a later
	// migration step) calls them directly around an async open + header parse. Each validates/finalizes
	// on its own -- a direct caller gets the same guarantees buildAudioFileFromCard's callers always did.
	Sample* beginSampleFromCard(const std::string& filePath, const std::string& usingAlternateLocation,
	                            uint64_t sizeBytes, Error* error);
	AudioFile* finishSampleFromCard(Sample* sampleFile, uint64_t sizeBytes, Error* error);

	/// @brief Resolve `alternateLoadDirStatus` from `MIGHT_EXIST` to `DOES_EXIST` or `NOT_FOUND`. I/O:
	///        opens `alternateAudioFileLoadPath`. Public, and split out of resolveFileSize (its only
	///        other caller), so the C ABI (a later migration step) can run this probe on its own before
	///        proposeLoadCandidates() -- that function only proposes the alternate-dir candidates once
	///        this status is DOES_EXIST, so calling it while still MIGHT_EXIST silently yields an
	///        incomplete list (see proposeLoadCandidates's doc comment).
	///
	/// A no-op when the status is already anything other than MIGHT_EXIST: returns immediately, true
	/// only for DOES_EXIST, without touching the card.
	///
	/// @post Returning true implies `alternateLoadDirStatus == DOES_EXIST`, i.e. never MIGHT_EXIST.
	///       The async collector's one-shot retry rests on that: it asks again only when this both
	///       returned true AND left MIGHT_EXIST behind, so the same load cannot keep probing. An edit
	///       that returned true without assigning the status would turn that into a per-tick probe.
	///
	/// @param error Set to Error::FILE_UNREADABLE if the probe finds the directory genuinely absent, or
	///              Error::SD_CARD if presence couldn't be determined (left untouched otherwise).
	/// @return true once the alternate dir is known to exist (already, or newly resolved); false
	///         otherwise.
	bool resolveAlternateLoadDirStatus(Error* error);

	/// @brief Whether @p path is one the async loader can accept at all.
	///
	/// Its two permanent refusals -- a path that is not valid UTF-8, and one that does not fit its
	/// fixed-capacity buffer -- are checked before a request is made, so such a path is never
	/// requested. Unlike a full queue, re-requesting one of those cannot ever succeed, so reporting
	/// Error::LOAD_IN_PROGRESS for it would leave the load neither completing nor failing. The
	/// synchronous path has neither limit.
	///
	/// The rule this applies is the Rust side's (`cstr_to_path`/`decode_candidates`, audio_loader.rs)
	/// and the two must not drift apart -- a LOOSER rule here is the wait described above coming back.
	/// Public so the shared corpus in path_admission_corpus.inc, which the Rust side asserts against
	/// too, can be run against it directly rather than inferred through a load.
	///
	/// @param path A nominal path, or one candidate slot.
	/// @return true if the loader would accept it; false if the caller must take the synchronous path.
	[[nodiscard]] static bool admissibleAsyncLoadPath(std::string_view path);

	/// Upper bound on how many candidate paths proposeLoadCandidates() can propose: the alternate dir's
	/// "collect media" flattened name, its bare-filename fallback, and the regular path.
	static constexpr size_t kMaxLoadCandidates = 3;

	/// @brief Decide, in priority order and without touching the card, which path(s) a load of
	///        @p filePath should try.
	///
	/// The single source of candidate policy for BOTH backings, which is what keeps them in step: the
	/// synchronous path calls this from resolveFileSize, and the async path calls it from
	/// requestAsyncSampleLoad and hands the result to the Rust loader AS DATA (the loader never calls
	/// this -- it cannot; it runs on an executor that must make no synchronous C++ call at all, and
	/// this allocates). Whichever backing runs, the try order came from here.
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
	/// Incomplete on its own when `alternateLoadDirStatus == AlternateLoadDirStatus::MIGHT_EXIST` --
	/// the state at the start of every song/preset load, before anything has probed whether the
	/// alternate dir actually exists. In that state this proposes only the regular path, silently
	/// rather than as an error. Reaching the alternate-dir candidates needs resolveAlternateLoadDirStatus()
	/// -- an I/O probe (`deluge::io::Directory::open` on `alternateAudioFileLoadPath`) that cannot live
	/// in this I/O-free function -- to have promoted the status first, and then a second call here.
	///
	/// **Where that probe belongs is AFTER a miss, not before the first call.** resolveFileSize runs
	/// it only once every proposed candidate has actually failed to open, and requestAsyncSampleLoad
	/// deliberately does the same rather than resolving up front: `alternateLoadDirStatus` is per-LOAD
	/// state, so promoting it early flips the try order for every later sample of the load INCLUDING
	/// the synchronous ones, which still promote only after a real miss. See requestAsyncSampleLoad's
	/// own comment and answerFailedLoadResult, which is where the async side's post-miss probe lives.
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

	/// @brief The resident AudioFile of @p type at @p filePath, given the index audioFiles.search()
	///        returned for that path (with foundExact).
	///
	/// A Sample and a WaveTable of the same file share a path, and audioFiles is sorted by path, so
	/// they sit adjacent and a search for the path can land on either -- hence the neighbour check
	/// rather than just a type test on the hit itself.
	///
	/// @param audioFileI A search() index that matched @p filePath exactly.
	/// @return The AudioFile of the wanted type, or nullptr if only the other type is resident.
	AudioFile* matchingResidentAudioFile(int32_t audioFileI, const std::string& filePath, AudioFileType type) const;

	/// @brief Answer a card miss through the async loader (libdeluge/audio_load.h) instead of the
	///        blocking card path: collect a finished parse and build the Sample from it, or ask for
	///        one.
	///
	/// Deliberately keeps NO in-flight table of its own. The loader already keys its queue, its
	/// in-flight set and its results by path, and reports DELUGE_AUDIO_LOAD_PENDING for a path it is
	/// already working on -- a second table here would be a second thing that has to agree with that
	/// one.
	///
	/// @param filePath The nominal path being loaded; the key the loader reports results under.
	/// @param error    Set only when this answers the call (see the return value).
	/// @return std::nullopt if this load is not the loader's to answer, and the caller must fall
	///         through to the synchronous path -- which includes a result that asked for a re-request
	///         no request could be formed for. Otherwise the call is answered: the published Sample,
	///         or nullptr with @p error set -- to the loader's verdict on the file, to a build
	///         failure, or to Error::LOAD_IN_PROGRESS (not a failure: ask again on a later tick).
	std::optional<AudioFile*> collectAsyncSampleLoad(const std::string& filePath, Error* error);

	/// @brief Answer a collected result that carries a failure verdict: either ask again with the
	///        alternate dir resolved, or report the verdict.
	///
	/// "No candidate opened" (Error::FILE_UNREADABLE) with the alternate load dir still unresolved is
	/// the one case that earns another attempt -- it is precisely where resolveFileSize runs its own
	/// probe. A parse verdict (FILE_UNSUPPORTED, FILE_CORRUPTED) is the file's own answer and is
	/// reported as-is: probing there would load a DIFFERENT file and call it success where the
	/// synchronous path fails, and promote the alternate-dir status for the rest of the load with it.
	///
	/// @param filePath    The nominal path being loaded.
	/// @param resultError The result's error byte (never Error::NONE).
	/// @param error       Set to the verdict, or to Error::LOAD_IN_PROGRESS if this asks again.
	/// @return std::nullopt if the load must fall through to the synchronous path (a re-request that
	///         could not be formed); otherwise nullptr with @p error set.
	std::optional<AudioFile*> answerFailedLoadResult(const std::string& filePath, uint8_t resultError, Error* error);

	/// @brief Build and publish a Sample from a collected parse result. No I/O beyond the one
	///        read-stream open (see deluge_audio_load_bind_read_stream).
	///
	/// Runs the whole build the loader is not allowed to run itself -- begin, bind, apply the parsed
	/// geometry, publish -- checking every step, and abandoning the build on any failure so nothing is
	/// left adopted-but-unpublished. See libdeluge/audio_load.h's ownership note.
	///
	/// @param filePath The nominal path (the AudioFile's own path; @p result carries the alternate
	///                 location the bytes were actually found at, if any).
	/// @param result   A DELUGE_AUDIO_LOAD_READY result whose own error is Error::NONE -- a verdict is
	///                 answered by answerFailedLoadResult instead.
	/// @param error    Set on failure (never left at Error::NONE when this returns nullptr).
	/// @return The published Sample, or nullptr with @p error set.
	AudioFile* buildSampleFromLoadResult(const std::string& filePath, const DelugeAudioLoadResult& result,
	                                     Error* error);

	/// @brief admissibleAsyncLoadPath() over every slot of a candidate buffer.
	/// @param candidates    `numCandidates * DELUGE_MAX_PATH` bytes, one NUL-terminated path per slot.
	/// @param numCandidates How many slots to check.
	/// @return true if every candidate is one the loader would accept.
	[[nodiscard]] static bool admissibleAsyncCandidates(const char* candidates, uint32_t numCandidates);

	/// @brief Ask the async loader to load @p filePath, with the candidates the current alternate-dir
	///        status proposes.
	///
	/// Deliberately does NOT resolve that status first, even though resolving is what unlocks the
	/// alternate-dir candidates. `alternateLoadDirStatus` is per-LOAD state, not per-sample: promoting
	/// it here would make every later sample of the same load try the alternate dir first, including
	/// the ones that never come near this function, while the synchronous path still promotes only
	/// after a regular-path miss. The probe therefore happens where the synchronous path puts it --
	/// after every proposed candidate has missed, in buildSampleFromLoadResult's verdict branch, which
	/// then asks again with the candidates the probe unlocked. It stays on this tier either way: the
	/// loader task runs where it may only await, and may not call into C++ at all.
	///
	/// @param filePath The nominal path to load.
	/// @param error    Set to Error::LOAD_IN_PROGRESS when a request was made (or dropped -- see the
	///                 implementation).
	/// @return std::nullopt if no request could be formed at all, and the caller must fall through to
	///         the synchronous path; otherwise nullptr with @p error set.
	std::optional<AudioFile*> requestAsyncSampleLoad(const std::string& filePath, Error* error);

	bool cardReadOnce{false};
	bool cardEjected{};

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
