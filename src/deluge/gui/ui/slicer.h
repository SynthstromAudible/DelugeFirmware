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

#include "gui/ui/slice_core.h"
#include "gui/ui/ui.h"
#include "hid/button.h"

#define SLICER_MODE_REGION 0
#define SLICER_MODE_MANUAL 1

/// Spec-only driver for the audition path (tests/unit/cpputest/slicer_preview_tests.cpp). Declared here so the specs
/// can reach `auditionLoad_` and `cancelPreview()` without those becoming public API.
class SlicerPreviewProbe;

class Slicer final : public UI {
	friend class SlicerPreviewProbe;

public:
	Slicer() { oledShowsUIUnderneath = true; }

	void focusRegained() override;
	bool canSeeViewUnderneath() override { return false; }
	void selectEncoderAction(int8_t offset) override;
	ActionResult buttonAction(deluge::hid::Button b, bool on) override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;

	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) override;
	void graphicsRoutine() override;
	ActionResult horizontalEncoderAction(int32_t offset) override;
	ActionResult verticalEncoderAction(int32_t offset) override;

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
	[[nodiscard]] int32_t previewGiveUps() const { return core_.audition_give_ups(); }

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

	/// @brief Abandon a held audition load, for the paths that leave the slicer or change what it is slicing.
	///
	/// Not a supersession: those paths ask for no replacement, so nothing else would ever drop the held load.
	void cancelPreview() { core_.cancel_audition(); }

	/// @brief The audition, the slice and the voice walk this UI drives.
	deluge::gui::ui::SliceCore core_{};
};

extern Slicer slicer;
