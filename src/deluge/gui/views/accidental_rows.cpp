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

#include "gui/views/accidental_rows.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/note/note_row.h"
#include "model/song/song.h"

namespace {
/// Washed-out version of a row colour, for notes outside the song's scale
RGB accidentalColour(RGB c) {
	RGB grey = RGB::monochrome((c.r + c.g + c.b) / 3);
	return RGB::average(c, grey).adjustFractional(3, 4);
}
} // namespace

AccidentalRows::Layout::Layout() {
	InstrumentClip* clip = getCurrentInstrumentClip();
	if (clip->isScaleModeClip() && getCurrentOutputType() != OutputType::KIT) {
		clip_ = clip;
		flats_ = currentSong->key.prefersFlats();
	}
}

std::optional<YNoteRange> AccidentalRows::Layout::hiddenYNotes(int32_t yDisplay) const {
	if (!clip_) {
		return std::nullopt;
	}
	int32_t yNote = clip_->getYNoteFromYDisplay(yDisplay, currentSong);
	YNoteRange range = flats_ ? YNoteRange{clip_->getYNoteFromYDisplay(yDisplay - 1, currentSong) + 1, yNote - 1}
	                          : YNoteRange{yNote + 1, clip_->getYNoteFromYDisplay(yDisplay + 1, currentSong) - 1};
	if (range.first > range.last) {
		return std::nullopt;
	}
	return range;
}

ModelStackWithNoteRow* AccidentalRows::noteRowForPad(int32_t x, int32_t y, ModelStackWithTimelineCounter* modelStack) {
	auto* clip = getCurrentInstrumentClip();
	auto* rowStack = clip->getNoteRowOnScreen(y, modelStack);
	auto hidden = Layout().hiddenYNotes(y);
	if (!hidden) {
		return rowStack;
	}
	SquareInfo square{};
	auto* row = rowStack->getNoteRowAllowNull();
	if (row) {
		row->getSquareInfo(x, rowStack->getLoopLength(), square);
	}
	if (!square.numNotes) {
		for (int32_t pitch = hidden->first; pitch <= hidden->last; ++pitch) {
			auto* hiddenRow = clip->getNoteRowForYNote(pitch);
			if (!hiddenRow) {
				continue;
			}
			hiddenRow->getSquareInfo(
			    x, hiddenRow->loopLengthIfIndependent ? hiddenRow->loopLengthIfIndependent : clip->loopLength, square);
			if (square.numNotes) {
				return clip->getNoteRowForYNote(pitch, modelStack);
			}
		}
	}
	return rowStack;
}

NoteRow* AccidentalRows::noteRowForPress(const EditPadPress& press, int32_t* getIndex) {
	if (press.yNoteOverride) {
		return getCurrentInstrumentClip()->getNoteRowForYNote(*press.yNoteOverride, getIndex);
	}
	return getCurrentInstrumentClip()->getNoteRowOnScreen(press.yDisplay, currentSong, getIndex);
}

ModelStackWithNoteRow* AccidentalRows::noteRowForPress(const EditPadPress& press,
                                                       ModelStackWithTimelineCounter* modelStack) {
	if (press.yNoteOverride) {
		return getCurrentInstrumentClip()->getNoteRowForYNote(*press.yNoteOverride, modelStack);
	}
	return getCurrentInstrumentClip()->getNoteRowOnScreen(press.yDisplay, modelStack);
}

int32_t AccidentalRows::auditionYNote(bool on, uint8_t yDisplay, int32_t yNoteOfRow,
                                      std::span<const EditPadPress> presses) {
	std::optional<int16_t>& auditioned = auditionedHiddenYNote_[yDisplay];
	if (on) {
		auditioned.reset();
		for (const EditPadPress& press : presses) {
			if (press.isActive && press.yDisplay == yDisplay && press.yNoteOverride) {
				auditioned = press.yNoteOverride;
				break;
			}
		}
	}
	int32_t yNote = auditioned.value_or(yNoteOfRow);
	if (!on) {
		auditioned.reset();
	}
	return yNote;
}

void AccidentalRows::render(YNoteRange hidden, RGB rowColour, bool allowNoteTails, TimelineView* view, RGB* image,
                            uint8_t* occupancyMask, int32_t renderWidth, int32_t xScroll, uint32_t xZoom) {
	InstrumentClip* clip = getCurrentInstrumentClip();
	RGB colour = accidentalColour(rowColour);

	for (int32_t hiddenY = hidden.first; hiddenY <= hidden.last; hiddenY++) {
		NoteRow* hiddenRow = clip->getNoteRowForYNote(hiddenY);
		if (!hiddenRow || !hiddenRow->notes.getNumElements()) {
			continue;
		}
		RGB hiddenImage[kDisplayWidth + kSideBarWidth];
		uint8_t hiddenMask[kDisplayWidth + kSideBarWidth];
		uint32_t loopLength =
		    hiddenRow->loopLengthIfIndependent ? hiddenRow->loopLengthIfIndependent : clip->loopLength;
		hiddenRow->renderRow(view, colour, colour.forTail(), colour.forBlur(), hiddenImage, hiddenMask, true,
		                     loopLength, allowNoteTails, renderWidth, xScroll, xZoom, 0, renderWidth, false);
		// The in-scale note wins any shared square
		for (int32_t x = 0; x < renderWidth; x++) {
			if (hiddenMask[x] && !occupancyMask[x]) {
				image[x] = hiddenImage[x];
				occupancyMask[x] = hiddenMask[x];
			}
		}
	}
}
