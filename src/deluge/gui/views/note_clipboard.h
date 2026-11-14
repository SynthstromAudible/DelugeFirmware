/*
 * Copyright © 2026 Sean Ditny
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
#include "model/note/note.h"
#include <cstdint>

class InstrumentClip;
class Song;
struct EditPadPress;

namespace deluge::gui {

// A selected-note clipboard, independent of the existing whole-screen pattern clipboard.
class NoteClipboard {
public:
	NoteClipboard() = default;
	NoteClipboard(const NoteClipboard&) = delete;
	NoteClipboard& operator=(const NoteClipboard&) = delete;
	~NoteClipboard();
	bool copySelection(bool cut);
	void clearClipboard();
	bool clipboardNoteVisible();
	uint8_t iconNoteCount() const;
	void blinkClipboardNote();
	bool deleteSelection();
	bool hasNotes() const { return entries_ != nullptr; }
	bool previewAt(int32_t x, int32_t y);
	bool paste(bool firmly);
	void setPasteMode(bool firmly);
	bool isPlacing() const { return placing_; }
	void cancelPreview();
	void exitEditor();
	void learnButton(bool on);
	bool shortcutPadAction(int32_t x, int32_t y, int32_t velocity);
	bool consumePadRelease(int32_t x, int32_t y, int32_t velocity);
	void renderPreview(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth]);

private:
	struct Entry {
		Note note;
		int32_t rowId;
		int32_t visual;
		Entry* next = nullptr;
	};
	Entry* entries_ = nullptr;
	InstrumentClip* sourceClip_ = nullptr;
	Song* sourceSong_ = nullptr;
	InstrumentClip* previewClip_ = nullptr;
	bool kit_ = false;
	bool cut_ = false;
	bool preview_ = false;
	bool placing_ = false;
	bool firmly_ = false;
	bool learnCopyStarted_ = false;
	bool notePulseOff_ = false;
	int32_t originPos_ = 0;
	int32_t originVisual_ = 0; // Highest displayed row: the top edge of the copied pattern.
	int32_t pastePos_ = 0;
	int32_t pasteVisual_ = 0;
	uint32_t consumedPads_[kDisplayHeight]{};

	void clear();
	bool captureSelection();
	bool appendTarget(const EditPadPress& target);
	bool appendNote(const Note& note, int32_t rowId, int32_t visual);
	bool copySquare(int32_t x, int32_t y);
	bool sourceStillMatches();
	bool isSourceNote(int32_t rowId, int32_t pos) const;
	bool destination(const Entry& entry, int32_t& rowId, int32_t& pos);
	bool canPaste();
	int32_t pastedLength(const Entry& entry, int32_t rowId, int32_t pos);
	bool commit(bool deleteOnly);
	void finishEdit();
};

extern NoteClipboard noteClipboard;
} // namespace deluge::gui
