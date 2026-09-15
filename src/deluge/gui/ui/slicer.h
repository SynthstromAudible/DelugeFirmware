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

#include "gui/ui/ui.h"
#include "hid/button.h"
#include "storage/audio/latest_sample_load.h"

#define SLICER_MODE_REGION 0
#define SLICER_MODE_MANUAL 1
#define MAX_MANUAL_SLICES 64

struct SliceItem {
	int32_t startPos;
	int32_t transpose;
};

/// Spec-only driver for the audition path (tests/unit/slicer_preview_tests.cpp). Declared here so the specs can reach
/// `auditionLoad_` and `cancelPreview()` without those becoming public API.
class SlicerPreviewProbe;

class Slicer final : public UI {
	friend class SlicerPreviewProbe;

public:
	Slicer() { oledShowsUIUnderneath = true; }

	void focusRegained() override;
	bool canSeeViewUnderneath() override { return false; }
	void selectEncoderAction(int8_t offset) override;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;

	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) override;
	void graphicsRoutine() override;
	ActionResult horizontalEncoderAction(int32_t offset) override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;

	void stopAnyPreviewing();
	/// @brief Audition a slice while its pad is held, or silence it when the pad is lifted.
	///
	/// A press names the first drum "1", plays it once, and points it at the sample being sliced. With that sample
	/// bound, the slice's params and the note-on happen at once. Without it, the press holds the sample's load and the
	/// params and the note wait for graphicsRoutine() to see it land; a newer press replaces the held load. A release
	/// cancels any held load and sends the note-off at once, so a note-on whose load never landed never sounds.
	/// @param startPoint Sample-frame position where the audition slice starts.
	/// @param endPoint   Sample-frame position where the audition slice ends; -1 leaves the current end position
	///                   unchanged.
	/// @param transpose  Transpose to apply to the audition note.
	/// @param on         Nonzero to start the audition, zero to silence it.
	void preview(int64_t startPoint, int64_t endPoint, int32_t transpose, int32_t on);

	/// @brief How many auditions gave up on a load that stopped moving (AsyncSampleLoad's stall bound).
	[[nodiscard]] int32_t previewGiveUps() const { return previewGiveUps_; }

	/// @brief How many slices began their load on the storage owner.
	///
	/// A slice's load belongs on the interaction tier; a slice that begins on the owner was dispatched there. Exposed
	/// for the sample-claim gate's SC-TIER check, which requires none.
	[[nodiscard]] int32_t sliceLoadsOnOwner() const { return sliceLoadsOnOwner_; }

	int32_t numManualSlice{};
	int32_t currentSlice{};
	int32_t slicerMode{};
	SliceItem manualSlicePoints[MAX_MANUAL_SLICES]{};

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;

	int16_t numClips{};

	// ui
	UIType getUIType() override { return UIType::SLICER; }

private:
	// 7SEG Only
	void redraw();

	/// @brief The slice, once its sample is bound to the first drum: the other drums, their NoteRows, and the browser's
	///        exit.
	///
	/// @param sliceCount How many slices/drums to create -- the commit's snapshot, not the live `numClips` or
	///                   `numManualSlice`, so an encoder turn while the slice's load was out can't desync the slice
	///                   math from the paint loop. See `SliceCommitTarget`.
	/// @return Error::NONE, or why a drum could not be made.
	Error doSlice(int32_t sliceCount);

	/// @brief A pad-hold/tap audition request, snapshotted at trigger time.
	///
	/// All fields are plain values (no pointers into live UI state): `startPoint`/`endPoint`/`transpose` are already
	/// read out of `manualSlicePoints[]` by `padAction` before this is built, and `on` is the press/release flag for
	/// this event.
	struct PreviewTarget {
		int64_t startPoint;
		int64_t endPoint;
		int32_t transpose;
		int32_t on;
	};

	/// @brief Set the audition's slice params on the first drum when @p target is a note-on, then send its note.
	/// @param target The audition request.
	void soundAudition(const PreviewTarget& target);

	/// @brief Tick the held audition load: a load that lands sounds its target, and one that stopped moving gives up.
	void tickAudition();

	/// @brief Abandon a held audition load, for the paths that leave the slicer or change what it is slicing.
	///
	/// Not a supersession: those paths ask for no replacement, so nothing else would ever drop the held load.
	void cancelPreview() { auditionLoad_.cancel(); }

	/// @brief The audition's held load, with the target it sounds when the load lands.
	deluge::storage::LatestSampleLoad<PreviewTarget> auditionLoad_{};

	/// @brief Auditions that gave up on a load that stopped moving. See previewGiveUps().
	int32_t previewGiveUps_{0};

	/// @brief Slices that began their load on the storage owner. See sliceLoadsOnOwner().
	int32_t sliceLoadsOnOwner_{0};

	/// @brief Snapshot of the Select "commit slice" gesture, taken as it is pressed.
	///
	/// `sliceCount` is `numClips` (REGION mode) or `numManualSlice` (MANUAL mode) at press time; `isManual` and
	/// `manualPoints` are what the MANUAL-mode per-drum start/end/transpose paint loop needs. All plain data, so an
	/// encoder turn or pad press while the slice's load is out can't desync what gets sliced from what gets painted.
	struct SliceCommitTarget {
		bool isManual;
		int32_t sliceCount;
		SliceItem manualPoints[MAX_MANUAL_SLICES];
	};

	/// @brief Where the slice is.
	enum class SlicePhase : uint8_t {
		Idle,    ///< No slice.
		Loading, ///< The sliced sample's load is out; graphicsRoutine() ticks it.
	};

	/// @brief Tick the slice's load, and end the slice once the load has ended: slice and paint if it landed, show
	///        the error if not, and release the gate either way.
	void tickSlice();

	/// @brief The slice's load of the sliced sample into the first drum. Its payload is unused.
	deluge::storage::LatestSampleLoad<uint32_t> sliceLoad_{};
	SlicePhase slicePhase_{SlicePhase::Idle};
	/// @brief The gesture the slice in progress was committed with.
	SliceCommitTarget pendingSliceTarget_{};
};

extern Slicer slicer;
