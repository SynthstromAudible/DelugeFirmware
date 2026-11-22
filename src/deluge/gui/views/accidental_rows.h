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
#include "gui/colour/rgb.h"
#include <array>
#include <cstdint>
#include <optional>
#include <span>

class InstrumentClip;
class NoteRow;
class ModelStackWithTimelineCounter;
class ModelStackWithNoteRow;
class TimelineView;
struct EditPadPress;

/// An inclusive range of pitches.
struct YNoteRange {
	int32_t first;
	int32_t last;
};

/// How the instrument clip view shows accidentals. In a scale mode clip, the pads only have rows for the notes of the
/// scale, so a NoteRow whose pitch is outside it (an accidental) has no row of its own: it is "hidden", and drawn in a
/// washed-out colour on the neighbouring in-scale row (the one below it in a sharp key, above it in a flat key).
/// Pad presses and auditioning on that row may then be for the hidden NoteRow rather than the row's own.
class AccidentalRows {
public:
	/// Which pitches are hidden behind which pad rows, for the current clip in the song's key. Finding the key's
	/// spelling takes some work, so make one of these per render or pad press and ask it about each row.
	class Layout {
	public:
		/// For the current clip.
		Layout();

		/// False if the current clip can't have hidden rows at all (a kit, or not in scale mode).
		[[nodiscard]] bool mayHaveHiddenRows() const { return clip_ != nullptr; }

		/// The pitches drawn on the in-scale row at yDisplay: the semitones just below the next in-scale pitch in a
		/// sharp key, or just above the previous in-scale pitch in a flat key. Empty if there are none.
		[[nodiscard]] std::optional<YNoteRange> hiddenYNotes(int32_t yDisplay) const;

	private:
		InstrumentClip* clip_ = nullptr; ///< Null if the clip can't have hidden rows
		bool flats_ = false;
	};

	/// The NoteRow of the current clip a held press is on: the hidden one it was routed to (EditPadPress::
	/// yNoteOverride), else the one shown at its yDisplay. Null (or a model stack with no NoteRow) if there isn't one.
	static NoteRow* noteRowForPress(const EditPadPress& press, int32_t* getIndex = nullptr);
	static ModelStackWithNoteRow* noteRowForPress(const EditPadPress& press, ModelStackWithTimelineCounter* modelStack);

	/// The pitch to audition for the pad row at yDisplay, given the pitch of the row itself. For a note-on, that is
	/// the hidden pitch if one of the presses on the row is on one. It is remembered so that the note-off goes to the
	/// same pitch, even if the press has ended or moved by then.
	int32_t auditionYNote(bool on, uint8_t yDisplay, int32_t yNoteOfRow, std::span<const EditPadPress> presses);

	/// Draws the notes of the current clip's hidden rows onto the image of the pad row they are shown on, in a
	/// washed-out version of rowColour. The row's own notes, already drawn and marked in occupancyMask, keep any square
	/// they share. occupancyMask must not be null.
	static void render(YNoteRange hidden, RGB rowColour, bool allowNoteTails, TimelineView* view, RGB* image,
	                   uint8_t* occupancyMask, int32_t renderWidth, int32_t xScroll, uint32_t xZoom);

private:
	/// Per pad row, the hidden pitch being auditioned on it, if any
	std::array<std::optional<int16_t>, kDisplayHeight> auditionedHiddenYNote_{};
};
