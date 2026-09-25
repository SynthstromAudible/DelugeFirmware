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
#include "storage/audio/latest_sample_load.h"

#include <array>
#include <cstdint>
#include <string>

class AudioFileHolder;
class Drum;
class InstrumentClip;
class Kit;
class SoundDrum;

namespace deluge::gui {

/// @brief The Drum Randomizer's gesture, snapshotted as it is pressed.
///
/// Which rows get randomized depends on live state -- AFFECT_ENTIRE, the audition pads, UI_MODE_AUDITIONING and the
/// kit's selected drum -- that an audition-pad release can change while the walk is out: a release passes padAction()
/// whatever the UI mode. The walk reads only this.
struct RandomizeRequest {
	Kit* kit = nullptr;
	InstrumentClip* instrumentClip = nullptr;
	bool randomizeAll = false; ///< AFFECT_ENTIRE held: every unmuted row that has notes.
	bool auditioning = false;  ///< Audition pads held: the rows under them.
	std::array<uint8_t, kDisplayHeight> auditionPadIsPressed{};
	Drum* selectedDrum = nullptr; ///< Neither: just this drum.
};

/// @brief The Drum Randomizer: each chosen row's drum lands on a random audio file from the folder its sample sits in.
///
/// A phase machine on the interaction tier, advanced by request() and by routine() from InstrumentClipView's graphics
/// routine. For each row, one minimal operation on the storage owner opens the folder, reads its entries and picks a
/// filename, changing nothing: a directory's entries have no async surface to read through. The row's load goes through
/// the async sample load, and the unbind, the bind, the drum's new name and the kit's edited flag all happen on this
/// tier.
///
/// The gesture closes UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES before requesting, and the walk releases it when it ends,
/// if it is still that mode. A row whose folder cannot be opened, or whose load fails or finds nothing, ends the walk
/// with its error; rows already randomized keep their new samples. A walk cancelled with a scan out drains: nothing
/// touches the row's holder until the scan reports.
class RandomizeDrums {
public:
	/// @brief Start a walk over @p request's rows, and run one pass.
	///
	/// The gesture closes the gate before calling. A running walk keeps it closed, so no second press arrives; a
	/// cancelled walk still draining has released it, so a press then is dropped and the gate it closed released.
	void request(const RandomizeRequest& request);

	/// @brief Abandon a walk left over from a previous visit to the clip view: drain if a scan is out, otherwise end
	///        at once, with no popup and no error.
	void cancelWalk();

	/// @brief Advance the walk as far as it can go without waiting. Called by the clip view's graphics routine.
	void routine();

	/// @name Counts for the sample-claim gate.
	/// @{
	/// Directory scans that ran on the storage owner.
	[[nodiscard]] int32_t scansOnOwner() const { return scansOnOwner_; }

	/// @brief Directory scans that RAN, on whatever tier.
	///
	/// Separate from @ref scansOnOwner because RD-TIER's single counter was doing two jobs: "a scan
	/// happened" and "it happened on the owner". Once the scan leaves the owner the second goes to
	/// zero, and a leg reading only that would quietly stop checking the first.
	[[nodiscard]] int32_t scansRun() const { return scansRun_; }
	/// @brief Walks cancelled with the row's open still outstanding, which therefore had to drain.
	///
	/// Zero here means the cancel-during-open path was never taken, so nothing the drain does was
	/// tested -- which is how that path came to hold a condition that could never be true.
	[[nodiscard]] int32_t drainsEntered() const { return drainsEntered_; }

	/// @brief Drains that collected a handle and discarded it: the handle leak this path exists to prevent.
	[[nodiscard]] int32_t drainsCollected() const { return drainsCollected_; }

	/// @brief Whether a cancelled walk is still waiting for its open to report.
	[[nodiscard]] bool draining() const { return phase_ == Phase::Draining; }
	/// Row loads that began on the storage owner; required 0.
	[[nodiscard]] int32_t loadsOnOwner() const { return loadsOnOwner_; }
	/// Walks that ended on a load that stopped moving (AsyncSampleLoad's stall bound).
	[[nodiscard]] int32_t giveUps() const { return giveUps_; }
	/// @}

private:
	enum class Phase : uint8_t {
		Idle,      ///< No walk.
		Start,     ///< Open the row cursor on the request's criteria.
		ScanRow,   ///< Find the next eligible row, take its folder, and dispatch its scan.
		AwaitScan, ///< Wait for the scan's filename.
		LoadRow,   ///< Stop previewing, kill the drum's voices, unbind, set the path, and request the load.
		AwaitLoad, ///< Tick the load; a bound file renames the drum.
		Finish,    ///< The popup, and the gate released.
		Draining,  ///< A cancelled walk waiting for its scan to report.
	};

	/// @brief The scan's result slot: written by the scan on the owner, read by the next pass.
	struct ScanResult {
		bool done = false;
		Error error = Error::NONE;
		int32_t fileCount = 0;
		std::array<char, 256> filename{};
	};

	/// @brief One step of the transition function.
	/// @return Whether to step again in this pass; false when the walk has to wait.
	bool step();
	bool stepStart();
	bool stepScanRow();
	bool stepAwaitScan();
	bool stepLoadRow();
	bool stepAwaitLoad();
	bool stepFinish();

	/// @brief End the walk: show @p error unless it is Error::NONE, release the gate, and go idle.
	void end(Error error);
	/// @brief The next row to try, advancing the cursor: an eligible drum, or nullptr once the rows are exhausted.
	Drum* nextRow();

	/// @brief Drain an opened row folder, reservoir-sampling one audio file from it.
	///
	/// Was `runScanOp`, an `Owner::run` callback that opened the folder itself. The open now comes
	/// from the directory channel; the loop below it is unchanged, which matters because the
	/// reservoir sample is only uniform if it sees EVERY entry -- and the snapshot guarantees that,
	/// being all-or-error rather than ever partial.
	/// @param dir The adopted directory. Walking it costs no card I/O.
	void drainScan(deluge::io::Directory& dir);

	Phase phase_{Phase::Idle};
	RandomizeRequest current_{};
	/// @brief The next row index to try: a NoteRow index for randomizeAll, a display row for auditioning, and 0 or 1
	/// for
	///        the selected drum.
	int32_t cursor_{0};
	int32_t rowsRandomized_{0};
	/// @brief The last randomized file's name without its extension: the one-row popup.
	std::array<char, 256> lastName_{};

	/// @name The row in hand.
	/// @{
	SoundDrum* rowDrum_{nullptr};
	AudioFileHolder* rowHolder_{nullptr};
	/// The folder the row's sample sits in, fixed before the scan is dispatched and read by it.
	std::string rowFolder_{};
	/// @}

	/// @brief The row's load. Its payload is unused.
	deluge::storage::LatestSampleLoad<uint32_t> load_{};

	/// @name The scan out.
	/// @{
	/// Whether this ROW's folder has been opened and drained. Reset per row in stepScanRow: a flag
	/// left set would make the next row reuse the previous row's scan.
	bool scanCollected_{false};
	/// How long this row's open has gone unanswered, and how long a cancelled walk has waited for
	/// its outstanding one. Elapsed time, never ticks.
	deluge::storage::UnproductiveSpan scanSpan_{};
	/// What this row's drain wrote: the file it picked, or the error that stopped it.
	ScanResult scan_{};
	/// @}

	int32_t scansOnOwner_{0};
	int32_t scansRun_{0};
	int32_t drainsEntered_{0};
	int32_t drainsCollected_{0};
	int32_t loadsOnOwner_{0};
	int32_t giveUps_{0};
};

extern RandomizeDrums randomizeDrums;

} // namespace deluge::gui
