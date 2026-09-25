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

#pragma once

#include "definitions_cxx.hpp"
#include "io/file.hpp"
#include "model/sample/recorder_write_policy.h" // UnproductiveSpan
#include "model/sample/sample_holder.h"
#include "storage/audio/latest_sample_load.h"

#include <cstdint>
#include <string>
#include <vector>

class Sample;

namespace deluge::gui {

/// @brief Which context menu option asked for a folder import.
enum class FolderImportKind : uint8_t {
	Kit,          ///< The kit menu's Load All: one drum per file.
	Multisamples, ///< The synth menu's Multisamples: one range per detected pitch.
};

/// @brief A folder import, snapshotted when its option is accepted.
struct FolderImportRequest {
	FolderImportKind kind = FolderImportKind::Kit;
	std::string folder;                   ///< The folder to import: the browser's item if a folder, else its folder.
	std::string previouslyViewedFilename; ///< The file the browser was on, which seeds the common-prefix search.
	void (*completion)(bool success) = nullptr; ///< Told how the import ended. A cancelled import tells it nothing.
};

/// @brief The sample browser's folder import: every audio file in a folder, as a kit's drums or a synth's multisample
///        ranges.
///
/// A phase machine on the interaction tier, advanced by request() and by routine() from its own repeating task. One
/// minimal operation on the storage owner lists the folder's audio filenames; each file then loads in turn through
/// LatestSampleLoad on a scratch holder, and the machine keeps the sample with a reason of its own. For Multisamples,
/// every pitch detection, including the correction pass's re-detections, is one operation on the owner. Sorting, the
/// corrections' arithmetic and every model change happen on this tier; the finishes are the importers' own bodies.
///
/// request() closes UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES with the "Working" animation; the import releases it, if
/// it is still that mode, when it ends. A file whose load fails, finds nothing or stops moving aborts the import: every
/// sample loaded so far loses its reason, the error is shown, and the completion is told. A cancel with an operation
/// out drains: the samples are left alone until the operation reports.
class FolderImport {
public:
	/// @brief Start an import, and run one pass. Ignored while an import is already in flight: its gate refuses the
	///        presses that would ask for another.
	void request(FolderImportRequest request);

	/// @brief Abandon an import: drain if an operation is out, otherwise release its samples, its gate and go idle.
	void cancel();

	/// @brief Advance the import as far as it can go without waiting. Called by the "folder import" repeating task.
	void routine();

	/// @brief Whether an import is in flight (not idle, not draining).
	[[nodiscard]] bool inFlight() const { return phase_ != Phase::Idle && phase_ != Phase::Draining; }

	/// @name Counts for the sample-claim gate.
	/// @{
	/// Folder listings opened on the storage owner.
	[[nodiscard]] int32_t listingsOnOwner() const { return listingsOnOwner_; }
	/// File loads begun on the storage owner; required 0.
	[[nodiscard]] int32_t loadsOnOwner() const { return loadsOnOwner_; }
	/// Pitch detections run on the storage owner.
	[[nodiscard]] int32_t pitchOnOwner() const { return pitchOnOwner_; }
	/// Imports that aborted on a load that stopped moving (AsyncSampleLoad's stall bound).
	[[nodiscard]] int32_t giveUps() const { return giveUps_; }
	/// @}

private:
	enum class Phase : uint8_t {
		Idle,          ///< No import.
		List,          ///< Dispatch the listing.
		AwaitList,     ///< Wait for the folder's audio filenames.
		LoadFile,      ///< Request the next file's load, or move on once every file has landed.
		AwaitLoad,     ///< Tick the load; a bound sample is kept with a reason.
		Prepare,       ///< Allocate the sort area and apply the common-note rule.
		Detect,        ///< Dispatch the next sample's pitch detection (Multisamples).
		AwaitDetect,   ///< Wait for it.
		Sort,          ///< Sort by filename; for Multisamples, mark the pitches out of filename order.
		Redetect,      ///< The correction pass: the next marked sample's detection within narrowed limits.
		AwaitRedetect, ///< Wait for it; a failure tries an octave up.
		AwaitOctaveUp, ///< Wait for the octave-up detection.
		FinishKit,     ///< The kit importer's body.
		FinishMulti,   ///< The multisample importer's body.
		Draining,      ///< A cancelled import waiting for its operation to report.
	};

	bool step();
	bool stepAwaitList();
	bool stepLoadFile();
	bool stepAwaitLoad();
	bool stepPrepare();
	bool stepSort();
	bool stepRedetect();

	/// @brief The kit importer's body: drums, names, NoteRows. @return Whether it succeeded.
	bool finishKit();
	/// @brief The multisample importer's body: ranges, top notes, repeat mode. @return Whether it succeeded.
	bool finishMulti();

	/// @brief Copy the sorted pointers back to the start of the sort area if they ended in its second half.
	void settleSortArea();
	/// @brief Remove this import's reason from every sample it kept, and free the sort area.
	void releaseSamples();
	/// @brief End with @p error: release the samples, show the error, and end unsuccessfully.
	void abort(Error error);
	/// @brief Release the gate and the animation, go idle, and tell the completion @p success.
	void end(bool success);

	/// @brief Queue @p fn on the storage owner, or leave it for the next pass if the queue is full.
	void dispatch(void (*fn)(void*));
	/// @brief Whether the operation out has reported; re-dispatches one the queue refused.
	bool opReported();

	/// @brief Drain an opened folder's entries into @ref names_.
	///
	/// Was `runListOp`, an `Owner::run` callback that opened the folder itself. The open now comes
	/// from the directory channel; everything below the loop's first line is unchanged, which is
	/// what keeps the audio-filename filter's behaviour identical.
	/// @param dir The adopted directory. Walking it costs no card I/O: it is an in-memory snapshot.
	void drainListing(deluge::io::Directory& dir);
	static void runPitchOp(void* self);

	Phase phase_{Phase::Idle};
	FolderImportRequest request_{};

	/// @name The listing's result, written by the listing on the owner.
	/// @{
	Error listError_{Error::NONE};
	/// Whether this import's folder has been opened and drained yet. Reset when a listing starts.
	bool listCollected_{false};
	/// How long the folder's open has gone unanswered. Elapsed time, never ticks.
	deluge::storage::UnproductiveSpan listSpan_{};
	std::vector<std::string> names_{};
	/// @}

	/// @name The loads.
	/// @{
	SampleHolder scratch_{};
	deluge::storage::LatestSampleLoad<uint32_t> load_{};
	std::string dirWithSlash_{};
	std::vector<Sample*> samples_{};
	int32_t cursor_{0};
	int32_t numCharsInPrefix_{65535};
	bool doingSingleCycle_{true};
	float commonMIDINote_{-2}; ///< -2: no data yet. -3: several different ones.
	/// @}

	/// @name The sort.
	/// @{
	Sample** sortArea_{nullptr};
	Sample** sortAreas_[2]{};
	int32_t numSamples_{0};
	int32_t readArea_{0};
	int32_t writeArea_{1};
	float prevNote_{0};
	int32_t prefixAndDirLength_{0};
	/// @}

	/// @name The operation out.
	/// @{
	void (*opFn_)(void*){nullptr};
	bool opDispatched_{false};
	/// Set as an operation is queued, and cleared by the operation as it ends: what a drain waits for.
	bool opRunning_{false};
	Sample* opSample_{nullptr};
	bool opLimited_{false};
	float opMinFreqHz_{0};
	float opMaxFreqHz_{0};
	/// @}

	int32_t listingsOnOwner_{0};
	int32_t loadsOnOwner_{0};
	int32_t pitchOnOwner_{0};
	int32_t giveUps_{0};
};

extern FolderImport folderImport;

} // namespace deluge::gui
