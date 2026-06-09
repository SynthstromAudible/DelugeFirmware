/*
 * Copyright (c) 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#include "io/midi/device_specific/launchpad_note_mode.h"

#include "definitions_cxx.hpp"
#include "gui/colour/palette.h"
#include "gui/ui/keyboard/layout/column_controls.h"
#include "gui/ui/keyboard/notes_state.h"
#include "io/midi/device_specific/launchpad_extension.h"
#include "io/midi/midi_engine.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "model/scale/note_set.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"

#include <algorithm>

using deluge::gui::colours::black;
using deluge::gui::ui::keyboard::kHighestKeyboardNote;
using deluge::gui::ui::keyboard::kLowestKeyboardNote;
using deluge::gui::ui::keyboard::layout::kMaxIsomorphicRowInterval;

namespace launchpad_note_mode {

namespace {

constexpr int32_t kLaunchpadWidth = 8;

enum class LayoutKind : uint8_t {
	Melodic,
	Kit,
};

struct HeldPad {
	int8_t x = -1;
	int8_t y = -1;
	bool active = false;
};

HeldPad heldPads[10];

InstrumentClip* noteModeClip() {
	InstrumentClip* clip = getCurrentInstrumentClip();
	if (clip == nullptr) {
		return nullptr;
	}
	if (clip->output->type == OutputType::AUDIO) {
		return nullptr;
	}
	return clip;
}

LayoutKind layoutFor(InstrumentClip* clip) {
	if (clip->output->type == OutputType::KIT) {
		return LayoutKind::Kit;
	}
	return LayoutKind::Melodic;
}

int16_t rootNote() {
	return static_cast<int16_t>(currentSong->key.rootNote % kOctaveSize);
}

int32_t melodicNoteFromPad(int32_t x, int32_t y, InstrumentClip* clip) {
	auto& iso = clip->keyboardState.isomorphic;
	return iso.scrollOffset + x + y * iso.rowInterval;
}

int32_t kitDrumIndexFromPad(int32_t x, int32_t y, InstrumentClip* clip) {
	return clip->keyboardState.drums.scroll_offset + x + (y * kLaunchpadWidth);
}

int32_t highestKitDrumIndex(InstrumentClip* clip) {
	return clip->noteRows.getNumElements() - 1;
}

int32_t highestKitScrollOffset(InstrumentClip* clip) {
	int32_t visiblePads = ((kDisplayHeight - 1) * kLaunchpadWidth) + (kLaunchpadWidth - 1);
	return std::max<int32_t>(0, highestKitDrumIndex(clip) - visiblePads);
}

bool isMelodicNoteInRange(int32_t note) {
	return note >= kLowestKeyboardNote && note <= kHighestKeyboardNote;
}

bool isMelodicNoteInScale(int32_t note, InstrumentClip* clip) {
	if (!clip->inScaleMode) {
		return true;
	}

	int32_t noteWithinOctave = static_cast<uint16_t>((note + kOctaveSize) - rootNote()) % kOctaveSize;
	return currentSong->key.modeNotes.has(static_cast<int8_t>(noteWithinOctave));
}

bool isMelodicPadPlayable(int32_t x, int32_t y, InstrumentClip* clip, int32_t& outNote) {
	outNote = melodicNoteFromPad(x, y, clip);
	return isMelodicNoteInRange(outNote) && isMelodicNoteInScale(outNote, clip);
}

bool isKitPadPlayable(int32_t x, int32_t y, InstrumentClip* clip, int32_t& outDrumIndex) {
	outDrumIndex = kitDrumIndexFromPad(x, y, clip);
	return outDrumIndex >= 0 && outDrumIndex <= highestKitDrumIndex(clip);
}

RGB colourForKitDrum(InstrumentClip* clip, int32_t drumIndex) {
	int8_t colourOffset = 0;
	if (drumIndex >= 0 && drumIndex < clip->noteRows.getNumElements()) {
		NoteRow* noteRow = clip->noteRows.getElement(drumIndex);
		if (noteRow != nullptr) {
			colourOffset = noteRow->getColourOffset(clip);
		}
	}
	return clip->getMainColourFromY(static_cast<uint8_t>(drumIndex), colourOffset);
}

void precalculateMelodicColours(InstrumentClip* clip, RGB* noteColours, int32_t colourCount) {
	for (int32_t i = 0; i < colourCount; i++) {
		noteColours[i] =
		    clip->getMainColourFromY(static_cast<uint8_t>(clip->keyboardState.isomorphic.scrollOffset + i), 0);
	}
}

bool padIsHeld(int32_t x, int32_t y) {
	for (HeldPad const& pad : heldPads) {
		if (pad.active && pad.x == x && pad.y == y) {
			return true;
		}
	}
	return false;
}

void setPadHeld(int32_t x, int32_t y, bool held) {
	if (held) {
		for (HeldPad& pad : heldPads) {
			if (!pad.active) {
				pad.x = static_cast<int8_t>(x);
				pad.y = static_cast<int8_t>(y);
				pad.active = true;
				return;
			}
		}
		return;
	}

	for (HeldPad& pad : heldPads) {
		if (pad.active && pad.x == x && pad.y == y) {
			pad.active = false;
			pad.x = -1;
			pad.y = -1;
			return;
		}
	}
}

void injectNote(MIDICable& cable, bool on, int32_t midiChannel, int32_t note, uint8_t velocity) {
	bool doingMidiThru = false;
	int32_t liftVelocity = on ? velocity : kDefaultLiftValue;
	playbackHandler.noteMessageReceived(cable, on, midiChannel, note, liftVelocity, &doingMidiThru);
}

void injectKitDrum(MIDICable& cable, bool on, int32_t midiChannel, int32_t drumIndex, uint8_t velocity) {
	injectNote(cable, on, midiChannel, drumIndex + midiEngine.midiFollowKitRootNote, velocity);
}

void clearHeldPads() {
	for (HeldPad& pad : heldPads) {
		pad.active = false;
		pad.x = -1;
		pad.y = -1;
	}
}

void syncMelodicLeds(InstrumentClip* clip, RGB launchpadImage[][kDisplayWidth + kSideBarWidth],
                     uint8_t launchpadOccupancyMask[][kDisplayWidth + kSideBarWidth]) {
	auto& iso = clip->keyboardState.isomorphic;
	int32_t colourCount = (kDisplayHeight * iso.rowInterval) + kLaunchpadWidth;
	RGB noteColours[kDisplayHeight * kMaxIsomorphicRowInterval + kDisplayWidth];
	if (colourCount > static_cast<int32_t>(sizeof(noteColours) / sizeof(noteColours[0]))) {
		colourCount = sizeof(noteColours) / sizeof(noteColours[0]);
	}
	precalculateMelodicColours(clip, noteColours, colourCount);

	NoteSet octaveScaleNotes;
	if (clip->inScaleMode) {
		NoteSet& scaleNotes = currentSong->key.modeNotes;
		for (uint8_t idx = 0; idx < currentSong->key.modeNotes.count(); idx++) {
			octaveScaleNotes.add(scaleNotes[idx]);
		}
	}

	for (int32_t y = 0; y < kDisplayHeight; y++) {
		int32_t noteCode = melodicNoteFromPad(0, y, clip);
		int32_t normalizedPadOffset = noteCode - iso.scrollOffset;
		int32_t noteWithinOctave = static_cast<uint16_t>((noteCode + kOctaveSize) - rootNote()) % kOctaveSize;

		for (int32_t x = 0; x < kLaunchpadWidth; x++) {
			RGB colour = black;
			bool occupied = false;

			if (isMelodicNoteInRange(noteCode)) {
				if (padIsHeld(x, y)) {
					colour = noteColours[normalizedPadOffset];
					occupied = true;
				}
				else if (!clip->inScaleMode || noteWithinOctave == 0) {
					colour = noteColours[normalizedPadOffset];
					occupied = true;
				}
				else if (octaveScaleNotes.has(static_cast<int8_t>(noteWithinOctave))) {
					colour = noteColours[normalizedPadOffset].forTail();
					occupied = true;
				}
			}

			launchpadImage[y][x] = colour;
			launchpadOccupancyMask[y][x] = occupied ? 64 : 0;
			noteCode++;
			normalizedPadOffset++;
			noteWithinOctave = (noteWithinOctave + 1) % kOctaveSize;
		}
	}
}

void syncKitLeds(InstrumentClip* clip, RGB launchpadImage[][kDisplayWidth + kSideBarWidth],
                 uint8_t launchpadOccupancyMask[][kDisplayWidth + kSideBarWidth]) {
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		for (int32_t x = 0; x < kLaunchpadWidth; x++) {
			int32_t drumIndex = kitDrumIndexFromPad(x, y, clip);
			RGB colour = black;
			bool occupied = false;

			if (drumIndex >= 0 && drumIndex <= highestKitDrumIndex(clip)) {
				colour = colourForKitDrum(clip, drumIndex);
				occupied = true;
				if (padIsHeld(x, y)) {
					colour = colour.transform([](uint8_t chan) { return chan / 2; });
				}
			}

			launchpadImage[y][x] = colour;
			launchpadOccupancyMask[y][x] = occupied ? 64 : 0;
		}
	}
}

} // namespace

bool handleScroll(int32_t offsetX, int32_t offsetY) {
	InstrumentClip* clip = noteModeClip();
	if (clip == nullptr) {
		return false;
	}

	if (layoutFor(clip) == LayoutKind::Kit) {
		auto& state = clip->keyboardState.drums;
		int32_t offset = offsetX + (offsetY * kLaunchpadWidth);
		int32_t newOffset = state.scroll_offset + offset;
		if (newOffset < 0 || newOffset > highestKitScrollOffset(clip)) {
			return false;
		}
		state.scroll_offset = newOffset;
		return true;
	}

	auto& state = clip->keyboardState.isomorphic;
	int32_t offset = offsetX + (offsetY * state.rowInterval);

	int32_t highestScrolledNote =
	    kHighestKeyboardNote - (((kDisplayHeight - 1) * state.rowInterval) + (kLaunchpadWidth - 1));

	int32_t newOffset = state.scrollOffset + offset;
	if (newOffset < kLowestKeyboardNote || newOffset > highestScrolledNote) {
		return false;
	}

	state.scrollOffset = newOffset;
	return true;
}

bool handlePad(MIDICable& cable, int32_t x, int32_t y, uint8_t velocity, bool on, int32_t midiChannel) {
	if (x < 0 || x >= kLaunchpadWidth || y < 0 || y >= kDisplayHeight) {
		return false;
	}

	InstrumentClip* clip = noteModeClip();
	if (clip == nullptr) {
		return false;
	}

	if (layoutFor(clip) == LayoutKind::Kit) {
		int32_t drumIndex = 0;
		if (!isKitPadPlayable(x, y, clip, drumIndex)) {
			return false;
		}

		if (on) {
			if (velocity == 0) {
				velocity = static_cast<Kit*>(clip->output)->defaultVelocity;
			}
			setPadHeld(x, y, true);
			injectKitDrum(cable, true, midiChannel, drumIndex, velocity);
		}
		else {
			setPadHeld(x, y, false);
			injectKitDrum(cable, false, midiChannel, drumIndex, 0);
		}

		return true;
	}

	int32_t note = 0;
	if (!isMelodicPadPlayable(x, y, clip, note)) {
		return false;
	}

	if (on) {
		if (velocity == 0) {
			velocity = static_cast<Instrument*>(clip->output)->defaultVelocity;
		}
		setPadHeld(x, y, true);
		injectNote(cable, true, midiChannel, note, velocity);
	}
	else {
		setPadHeld(x, y, false);
		injectNote(cable, false, midiChannel, note, 0);
	}

	return true;
}

void syncLeds() {
	static RGB launchpadImage[kDisplayHeight][kDisplayWidth + kSideBarWidth];
	static uint8_t launchpadOccupancyMask[kDisplayHeight][kDisplayWidth + kSideBarWidth];

	for (int32_t y = 0; y < kDisplayHeight; y++) {
		for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
			launchpadImage[y][x] = black;
			launchpadOccupancyMask[y][x] = 0;
		}
	}

	InstrumentClip* clip = noteModeClip();
	if (clip != nullptr) {
		if (layoutFor(clip) == LayoutKind::Kit) {
			syncKitLeds(clip, launchpadImage, launchpadOccupancyMask);
		}
		else {
			syncMelodicLeds(clip, launchpadImage, launchpadOccupancyMask);
		}
	}

	launchpad_extension::syncNoteView(launchpadImage, launchpadOccupancyMask);
}

void releaseAllHeldPads(MIDICable& cable, int32_t midiChannel) {
	InstrumentClip* clip = noteModeClip();
	if (clip == nullptr) {
		clearHeldPads();
		return;
	}

	for (HeldPad& pad : heldPads) {
		if (!pad.active) {
			continue;
		}

		if (layoutFor(clip) == LayoutKind::Kit) {
			int32_t drumIndex = 0;
			if (isKitPadPlayable(pad.x, pad.y, clip, drumIndex)) {
				injectKitDrum(cable, false, midiChannel, drumIndex, 0);
			}
		}
		else {
			int32_t note = 0;
			if (isMelodicPadPlayable(pad.x, pad.y, clip, note)) {
				injectNote(cable, false, midiChannel, note, 0);
			}
		}

		pad.active = false;
		pad.x = -1;
		pad.y = -1;
	}
}

} // namespace launchpad_note_mode
