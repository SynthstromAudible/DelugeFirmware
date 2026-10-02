/*
 * Copyright (c) 2024 Sean Ditny
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

#include "processing/stem_export/stem_search_policy.h"

#include "definitions_cxx.hpp"
#include "gui/l10n/strings.h"
#include "io/stall_bound.h"
#include "processing/sound/sound_drum.h"
#include "runtime/app_loop.h"
#include "util/c_string.h"
#include <cstdint>
#include <string>

class Output;
class Clip;
class SoundDrum;

class StemExport {
public:
	StemExport();

	// start & stop process
	void startStemExportProcess(StemExportType stemExportType);
	void stopStemExportProcess();
	void abortStemExportProcess(deluge::l10n::String reason);
	void startOutputRecordingUntilLoopEndAndSilence();
	void stopPlayback();
	void stopOutputRecording();

	/// @brief Advances an in-flight export, then asks for the job's next run. Cheap no-op when none is running.
	///
	/// Run by its own job (@ref jobId). Each step returns here rather than blocking per element, so the UI can keep
	/// servicing a cancellation, and the fill task and the storage channels the render depends on can answer, while
	/// an export is in flight.
	void exportRoutine();

	/// @brief The job exportRoutine() runs on, woken by DELUGE_WAKE_STORAGE. It runs when an export starts or is
	///        aborted, when a storage channel answers or the streaming loader drains, and at the time the phase it is
	///        parked in asks for (see armNextRun()).
	deluge::runtime::JobId jobId{deluge::runtime::kNoJob};

	/// @brief True from @ref startStemExportProcess until the export has torn down.
	[[nodiscard]] bool isExportInFlight() const { return exportPhase != ExportPhase::NONE; }
	bool checkForLoopEnd();
	bool checkForSilence();
	bool processStarted;
	bool stopRecording;
	StemExportType currentStemExportType;
	uint32_t timePlaybackStopped;
	uint32_t timeThereWasLastSomeActivity;

	// export config variables
	bool allowNormalization;
	bool allowNormalizationForDrums;
	bool exportToSilence;
	bool includeSongFX;
	bool includeKitFX;
	bool renderOffline;
	bool exportMixdown;

	/// @brief One element of an in-flight export: whichever of output, clip or note row the current
	///        export type walks.
	///
	/// Produced by @ref fetchElement from an index rather than held across the element's recording,
	/// because the export suspends there and a pointer taken beforehand would have to be revalidated.
	struct StemElement {
		Output* output = nullptr;
		/// @brief The element's own mute flag, written by @ref startElement and @ref finishElement.
		///
		/// Which flag it is depends on the export type: the arranger mute, the clip's active state, or
		/// the note row's mute. Null for MIXDOWN, which records everything together and mutes nothing.
		bool* muteState = nullptr;
		bool exportStem = false;
		SoundDrum* drum = nullptr;
	};

	/// @brief Counts the elements this export type walks and disarms them for recording.
	/// @return The element count, as @ref restoreElements and the caller both expect it.
	int32_t prepareElements();

	/// @brief Fetches element `index`, applying whatever per-type setup it needs (loop lengths, unmutes).
	/// @param index   The element's position, as counted by @ref prepareElements.
	/// @param element Filled in on success; left untouched otherwise.
	/// @return false if that slot is empty, in which case the element is skipped.
	bool fetchElement(int32_t index, StemElement& element);

	/// @brief Starts recording one element.
	/// @param index   The element's position, as counted by @ref prepareElements.
	/// @param element The element fetched by @ref fetchElement.
	/// @return false if the element is skipped, or if the export was aborted while starting it - the
	///         two are told apart by whether UI_MODE_STEM_EXPORT is still active.
	bool startElement(int32_t index, const StemElement& element);

	/// @brief Mutes the element again and counts it. Runs once its recording has finished.
	/// @param element The element that just finished recording.
	void finishElement(const StemElement& element);

	/// @brief The wait every export type shares: true once this element's recording has finished and
	///        playback has stopped.
	/// @return true when the element's wait is over.
	static bool elementFinished();

	/// @brief Restores whatever mutes @ref prepareElements changed.
	/// @param total The element count @ref prepareElements returned.
	void restoreElements(int32_t total);

	/// @brief Where an in-flight export is parked between run-loop passes.
	///
	/// Each phase runs one step per @ref exportRoutine pass, on the tier its caller stands on. Only the
	/// per-element wait spans passes.
	enum class ExportPhase : uint8_t {
		NONE,          ///< No export in flight.
		NEXT_ELEMENT,  ///< Ready to start the next element.
		AWAIT_ELEMENT, ///< The current element is recording; waiting for it to finish.
		FINISH,        ///< Every element done, or the user cancelled: tear down.
	};
	ExportPhase exportPhase{ExportPhase::NONE};

	/// @name The export's cursor: these fields carry state across the per-element wait, so they cannot
	/// be locals of a single call.
	/// @{
	int32_t exportTotal;       ///< Element count from prepareElements(); what the teardown restores.
	int32_t exportIndex;       ///< The element being walked, counting down.
	StemElement exportElement; ///< The element @ref exportIndex resolved to.

	/// @brief Parked at the offline render's drain point, between the audio routine and the slow
	///        routines.
	///
	/// The rendered audio depends on that ordering: the drain decides cluster eviction and underrun
	/// behaviour, so moving it even one call earlier or later changes the output bit-for-bit (see
	/// AudioEngine::routine()'s offline batch comment). Resuming mid-iteration is what keeps it put.
	bool exportAwaitingDrain;
	/// @}

	/// @brief How often an offline render parked at the drain point asks the streaming loader for the drain again,
	///        in µs.
	///
	/// The loader wakes the app when its queue drains, which is what normally ends the wait. A failed read it
	/// re-enqueues at the lowest priority stops the drain without a wake, and only a fresh drain request picks it up
	/// again. The wait never gives up: a read that keeps failing is the loader's to bound, and the render cannot move
	/// past the drain point without it.
	static constexpr uint64_t kDrainRecheckUs = 6'000;

	/// @brief How often a live export looks whether its element has finished, in µs.
	///
	/// The recording ends on the playback and recorder jobs, which raise no wake for this one.
	static constexpr uint64_t kLiveRecheckUs = 6'000;

	/// @brief Ask for the job's next run for the phase the export is parked in: at once for a phase that can move now
	///        or an offline render that has blocks to go; when the render waits on the card, the wait's deadline (see
	///        the body); the live re-check otherwise.
	void armNextRun();

	/// @name The export's steps, each one pass of the routine. Run where the caller stands.
	/// @{
	void startNextElement(); ///< Fetch and start the next element, or fall through to the teardown.
	void driveElement();     ///< One bounded slice of an offline render, then re-check for the finish.
	void finishExport();     ///< Restore, run the completion UI, and clear the export.
	/// @}

	/// @brief How many completed clusters may wait for the card, behind the one whose write is in flight, before an
	///        offline render stops to let the card catch up.
	///
	/// The recorder counts a cluster as written when it issues its write, so this excludes the cluster in flight and
	/// the one being filled: the render is held once more than this many completed clusters have not yet been handed
	/// to the card. An offline render runs far faster than real time, and the recorder hands the card one cluster per
	/// channel answer. Unbounded, the render outruns the card and the unwritten audio grows in RAM until the recorder
	/// runs out of buffers; on a host whose single executor answers the channel only between job runs, a pass renders
	/// about 190 clusters for every 2 it writes. Two keeps the next writes ready behind the one in flight, so the card
	/// is never left idle waiting for the render.
	static constexpr int32_t kMaxOwedClusters = 2;

	/// @brief Why an offline render cannot move on inside this run, if it cannot.
	enum class CardHold : uint8_t {
		/// Not held: render on.
		NotHeld,
		/// Held on an answer from the card: the stem-folder search's, or the recorder's (see
		/// SampleRecorder::waitsOnChannel()). The answer wakes the job.
		WaitsOnCard,
		/// Held, but the recorder's card routine moves on by itself on its next tick: its last answer ended a phase
		/// and the next phase's request is not out yet, so no answer will wake the job for it.
		MovesOn,
	};

	/// @brief Whether the offline render can make no further progress inside this run, because what it waits for is
	///        answered by the card, and if so whether an answer will come to wake the job.
	///
	/// Held while the recorder's stem-folder search is out, while the recorder has finished capturing but still has
	/// card work to do (only that work remains, so rendering on is waste), and while more than
	/// @ref kMaxOwedClusters completed clusters wait for the card. A recorder that has failed its take or has settled
	/// holds nothing: it ends through its own stall bound.
	///
	/// While held, each run still renders one iteration before its slow routines collect the card's answers, so the
	/// render's lead grows by at most one iteration per wake, while a cluster holds about 40 blocks: the backlog still
	/// drains.
	[[nodiscard]] CardHold cardHold() const;

	// export instruments
	int32_t disarmAllInstrumentsForStemExport(StemExportType stemExportType);
	void restoreAllInstrumentMutes(int32_t totalNumOutputs);

	// export clips
	int32_t disarmAllClipsForStemExport();
	void restoreAllClipMutes(int32_t totalNumClips);
	void getLoopLengthOfLongestNotEmptyNoteRow(Clip* clip);
	void getLoopEndPointInSamplesForAudioFile(int32_t loopLength);
	bool writeLoopEndPos();
	int32_t loopLengthToStopStemExport;
	int32_t loopEndPointInSamplesForAudioFile;

	// export drums
	int32_t disarmAllDrumsForStemExport();
	void restoreAllDrumMutes(int32_t totalNumNoteRows);

	/// @brief Clip count captured by a DRUM export's own clip disarm, so @ref restoreElements can undo
	///        it as well as the note-row mutes.
	int32_t drumExportTotalNumClips;

	// start exporting
	bool startCurrentStemExport(StemExportType stemExportType, Output* output, bool* muteState, int32_t fileNumber,
	                            bool exportStem, SoundDrum* drum = nullptr);

	// finish exporting
	void finishCurrentStemExport(StemExportType stemExportType, bool* muteState);
	void finishStemExportProcess(StemExportType stemExportType, int32_t elementsProcessed);
	void updateScrollPosition(StemExportType stemExportType, int32_t indexNumber);

	// export status
	void displayStemExportProgress(StemExportType stemExportType);
	int32_t numStemsExported;
	int32_t totalNumStemsToExport;

	// audio file management
	Error getUnusedStemRecordingFilePath(std::string* filePath, AudioRecordingFolder folder);
	Error getUnusedStemRecordingFolderPath(std::string* filePath, AudioRecordingFolder folder);
	int32_t highestUsedStemFolderNumber;

	/// @name Folder-search machine state
	/// Cross-tick state for the free-folder search, which issues one filesystem operation per tick and
	/// returns rather than blocking until the search is done.
	/// @{
	deluge::storage::StemSearchPhase stemSearchPhase_ = deluge::storage::StemSearchPhase::MkdirBase;
	bool stemSearchIssued_ = false;
	bool stemSearchActive_ = false;
	std::string stemSearchBasePath_{};
	std::string stemSearchCandidatePath_{};
	int32_t stemSearchCandidatesTried_ = 0;
	/// How long the current candidate's channel operation has gone unanswered. After boot there is
	/// nowhere legal to redo the operation inline, so a channel that stopped answering ends the search as Failed --
	/// the refusal every other unanswerable question here already gets.
	deluge::io::StallBound stemSearchStall_{};
	/// How many times, within one run of the "stem export" job, the current candidate's channel operation may be
	/// found unanswered before the search gives up, whatever the clock says.
	///
	/// The offline export runs the recorder, and so this search, many times within one job run. Inside a run the
	/// channel cannot answer on the host, whose single executor runs the channel's tier only between runs, and on a
	/// virtual clock no time passes. (On the device the storage tier is pre-emptive and can answer inside a run.)
	/// So the time bound alone would never end that loop. Across runs the time bound decides. 600 is the length of the
	/// time bound in ticks of the "audio slow" job, at 6 ms.
	static constexpr int32_t kMaxInRunPumps = 600;
	/// Unanswered finds of the current candidate's operation in this run of the "stem export" job. See
	/// @ref kMaxInRunPumps.
	int32_t stemSearchPumps_ = 0;
	/// @brief Note one unanswered find of the current candidate's operation.
	/// @return Whether the search should give up on it: the time bound has passed, or the in-run guard has.
	bool stemSearchStalled();
	/// @}
	std::string lastFolderNameForStemExport{};
	/// @brief The folder the most recent export wrote into: `.../TRACKS`, or `.../TRACKS-NN` once that song has
	///        been exported before.
	///
	/// Not `lastFolderNameForStemExport`, which is only the base name the folder search compares against to
	/// tell a repeat export of the same song from a new one, and never carries the number.
	std::string lastExportFolderPath{};
	/// returns false if no valid file name could be built (e.g. the output / drum names are too long
	/// for the file system), in which case the stem must not be exported
	[[nodiscard]] bool setWavFileNameForStemExport(StemExportType type, Output* output, int32_t fileNumber,
	                                               SoundDrum* drum = nullptr);
	std::string wavFileNameForStemExport{};
	bool wavFileNameForStemExportSet;

	// check if we're in context menu
	bool inContextMenu();
	bool renderingOffline() { return processStarted && renderOffline; }
};

extern StemExport stemExport;
