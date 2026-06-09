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
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/scale/note_set.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"

#include <algorithm>
#include <limits>

using deluge::gui::colours::black;
using deluge::gui::ui::keyboard::kHighestKeyboardNote;
using deluge::gui::ui::keyboard::kLowestKeyboardNote;
using deluge::gui::ui::keyboard::layout::kMaxIsomorphicRowInterval;

namespace launchpad_note_mode {

namespace {

constexpr int32_t kLaunchpadWidth = 8;

struct HeldPad {
	int8_t x = -1;
	int8_t y = -1;
	bool active = false;
};

HeldPad heldPads[10];

InstrumentClip* melodicClip() {
	InstrumentClip* clip = getCurrentInstrumentClip();
	if (clip == nullptr) {
		return nullptr;
	}
	if (clip->output->type == OutputType::KIT || clip->output->type == OutputType::AUDIO) {
		return nullptr;
	}
	return clip;
}

int16_t rootNote() {
	return static_cast<int16_t>(currentSong->key.rootNote % kOctaveSize);
}

int32_t noteFromPad(int32_t x, int32_t y, InstrumentClip* clip) {
	auto& iso = clip->keyboardState.isomorphic;
	return iso.scrollOffset + x + y * iso.rowInterval;
}

bool isNoteInRange(int32_t note) {
	return note >= kLowestKeyboardNote && note <= kHighestKeyboardNote;
}

bool isNoteInScale(int32_t note, InstrumentClip* clip) {
	if (!clip->inScaleMode) {
		return true;
	}

	int32_t noteWithinOctave = static_cast<uint16_t>((note + kOctaveSize) - rootNote()) % kOctaveSize;
	return currentSong->key.modeNotes.has(static_cast<int8_t>(noteWithinOctave));
}

bool isPadPlayable(int32_t x, int32_t y, InstrumentClip* clip, int32_t& outNote) {
	outNote = noteFromPad(x, y, clip);
	return isNoteInRange(outNote) && isNoteInScale(outNote, clip);
}

void precalculateColours(InstrumentClip* clip, RGB* noteColours, int32_t colourCount) {
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

} // namespace

bool handleScroll(int32_t offsetX, int32_t offsetY) {
	InstrumentClip* clip = melodicClip();
	if (clip == nullptr) {
		return false;
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

	InstrumentClip* clip = melodicClip();
	if (clip == nullptr) {
		return false;
	}

	int32_t note = 0;
	if (!isPadPlayable(x, y, clip, note)) {
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

	InstrumentClip* clip = melodicClip();
	if (clip != nullptr) {
		auto& iso = clip->keyboardState.isomorphic;
		int32_t colourCount = (kDisplayHeight * iso.rowInterval) + kLaunchpadWidth;
		RGB noteColours[kDisplayHeight * kMaxIsomorphicRowInterval + kDisplayWidth];
		if (colourCount > static_cast<int32_t>(sizeof(noteColours) / sizeof(noteColours[0]))) {
			colourCount = sizeof(noteColours) / sizeof(noteColours[0]);
		}
		precalculateColours(clip, noteColours, colourCount);

		NoteSet octaveScaleNotes;
		if (clip->inScaleMode) {
			NoteSet& scaleNotes = currentSong->key.modeNotes;
			for (uint8_t idx = 0; idx < currentSong->key.modeNotes.count(); idx++) {
				octaveScaleNotes.add(scaleNotes[idx]);
			}
		}

		for (int32_t y = 0; y < kDisplayHeight; y++) {
			int32_t noteCode = noteFromPad(0, y, clip);
			int32_t normalizedPadOffset = noteCode - iso.scrollOffset;
			int32_t noteWithinOctave = static_cast<uint16_t>((noteCode + kOctaveSize) - rootNote()) % kOctaveSize;

			for (int32_t x = 0; x < kLaunchpadWidth; x++) {
				RGB colour = black;
				bool occupied = false;

				if (isNoteInRange(noteCode)) {
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

	launchpad_extension::syncNoteView(launchpadImage, launchpadOccupancyMask);
}

void releaseAllHeldPads(MIDICable& cable, int32_t midiChannel) {
	InstrumentClip* clip = melodicClip();
	if (clip == nullptr) {
		for (HeldPad& pad : heldPads) {
			pad.active = false;
			pad.x = -1;
			pad.y = -1;
		}
		return;
	}

	for (HeldPad& pad : heldPads) {
		if (!pad.active) {
			continue;
		}

		int32_t note = 0;
		if (isPadPlayable(pad.x, pad.y, clip, note)) {
			injectNote(cable, false, midiChannel, note, 0);
		}

		pad.active = false;
		pad.x = -1;
		pad.y = -1;
	}
}

} // namespace launchpad_note_mode
