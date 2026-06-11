/*
 * Copyright (c) 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#include "io/midi/device_specific/launchpad_note_mode.h"

#include "definitions_cxx.hpp"
#include "gui/colour/palette.h"
#include "gui/ui/keyboard/chords.h"
#include "gui/ui/keyboard/expressive_chord_sets.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/keyboard/layout/column_controls.h"
#include "gui/ui/keyboard/layout/expressive_chords.h"
#include "gui/ui/keyboard/notes_state.h"
#include "gui/ui/keyboard/state_data.h"
#include "io/midi/device_specific/launchpad_extension.h"
#include "io/midi/midi_engine.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/scale/note_set.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"

#include <algorithm>

using deluge::gui::colours::black;
using deluge::gui::colours::cyan;
using deluge::gui::colours::enabled;
using deluge::gui::colours::green;
using deluge::gui::colours::grey;
using deluge::gui::colours::magenta;
using deluge::gui::colours::white;
using deluge::gui::ui::keyboard::getExpressiveChordsLayout;
using deluge::gui::ui::keyboard::kExpressiveChordsPerSet;
using deluge::gui::ui::keyboard::KeyboardStateExpressiveChords;
using deluge::gui::ui::keyboard::kHighestKeyboardNote;
using deluge::gui::ui::keyboard::kLowestKeyboardNote;
using deluge::gui::ui::keyboard::layout::kMaxIsomorphicRowInterval;

namespace launchpad_note_mode {

namespace {

constexpr int32_t kLaunchpadWidth = 8;
constexpr int32_t kKitDisplayRows = 4;
constexpr int32_t kKitRepeatToggleX = 7;
constexpr int32_t kKitRepeatToggleY = 4;
constexpr int32_t kKitRepeatDivisionRowY = 5;
constexpr int32_t kKitRepeatDivisionCount = 8;
constexpr int32_t kExpressiveLpChordRows = 4;
constexpr int32_t kExpressiveLpInversionRowY = 4;
constexpr int32_t kExpressiveLpSpreadRowY = 5;
constexpr int32_t kExpressiveLpPrevSetY = 6;
constexpr int32_t kExpressiveLpNextSetY = 7;
constexpr int32_t kExpressiveLpSetControlX = 7;
constexpr int32_t kExpressiveLpOctaveX = 6;

enum class LayoutKind : uint8_t {
	Melodic,
	Kit,
};

enum class MelodicSubMode : uint8_t {
	Isomorphic,
	ExpressiveChords,
};

enum class KitRepeatDivision : uint8_t {
	Bar,
	Quarter,
	QuarterTriplet,
	Eighth,
	EighthTriplet,
	Sixteenth,
	SixteenthTriplet,
	ThirtySecond,
};

struct HeldPad {
	int8_t x = -1;
	int8_t y = -1;
	bool active = false;
	uint8_t velocity = 0;
	uint32_t nextRepeatSampleTime = 0;
	int8_t chordNoteCount = 0;
	int32_t chordNotes[kMaxChordKeyboardSize] = {};
};

HeldPad heldPads[10];

MelodicSubMode melodicSubMode = MelodicSubMode::Isomorphic;

bool kitRepeatEnabled = false;
KitRepeatDivision kitRepeatDivision = KitRepeatDivision::Sixteenth;
MIDICable* noteModeCable = nullptr;
int32_t noteModeMidiChannel = 0;

bool songHasKitOutput() {
	if (currentSong == nullptr) {
		return false;
	}

	for (Output* output = currentSong->firstOutput; output != nullptr; output = output->next) {
		if (output->type == OutputType::KIT) {
			return true;
		}
	}
	return false;
}

InstrumentClip* noteModeClip() {
	if (currentSong == nullptr) {
		return nullptr;
	}

	Clip* currentClip = currentSong->getCurrentClip();
	if (currentClip == nullptr || currentClip->type != ClipType::INSTRUMENT) {
		return nullptr;
	}

	InstrumentClip* clip = static_cast<InstrumentClip*>(currentClip);
	if (clip->output == nullptr) {
		return nullptr;
	}

	auto outputType = clip->output->type;
	if (outputType == OutputType::AUDIO || outputType == OutputType::NONE) {
		return nullptr;
	}

	return clip;
}

LayoutKind layoutFor(InstrumentClip* clip) {
	if (clip->output->type == OutputType::KIT && songHasKitOutput()) {
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
	if (y < 0 || y >= kKitDisplayRows) {
		return -1;
	}
	return clip->keyboardState.drums.scroll_offset + x + (y * kLaunchpadWidth);
}

int32_t highestKitDrumIndex(InstrumentClip* clip) {
	return clip->noteRows.getNumElements() - 1;
}

int32_t highestKitScrollOffset(InstrumentClip* clip) {
	int32_t visiblePads = kKitDisplayRows * kLaunchpadWidth;
	return std::max<int32_t>(0, highestKitDrumIndex(clip) - visiblePads + 1);
}

bool isKitRepeatTogglePad(int32_t x, int32_t y) {
	return x == kKitRepeatToggleX && y == kKitRepeatToggleY;
}

bool isKitRepeatDivisionPad(int32_t x, int32_t y, int32_t& outIndex) {
	if (y == kKitRepeatDivisionRowY && x >= 0 && x < kKitRepeatDivisionCount) {
		outIndex = x;
		return true;
	}
	return false;
}

bool isKitRepeatControlPad(int32_t x, int32_t y) {
	return y >= kKitRepeatToggleY;
}

uint32_t kitRepeatIntervalSamples() {
	if (currentSong == nullptr) {
		return kSampleRate / 4;
	}

	uint32_t tickSamples = playbackHandler.getTimePerInternalTick();
	uint32_t sixteenthTicks = currentSong->getSixteenthNoteLength();

	switch (kitRepeatDivision) {
	case KitRepeatDivision::Bar:
		return currentSong->getBarLength() * tickSamples;
	case KitRepeatDivision::Quarter:
		return currentSong->getQuarterNoteLength() * tickSamples;
	case KitRepeatDivision::QuarterTriplet:
		return std::max<uint32_t>(1, (currentSong->getBarLength() * tickSamples) / 12);
	case KitRepeatDivision::Eighth:
		return sixteenthTicks * 2 * tickSamples;
	case KitRepeatDivision::EighthTriplet:
		return std::max<uint32_t>(1, (currentSong->getBarLength() * tickSamples) / 24);
	case KitRepeatDivision::Sixteenth:
		return sixteenthTicks * tickSamples;
	case KitRepeatDivision::SixteenthTriplet:
		return std::max<uint32_t>(1, (currentSong->getBarLength() * tickSamples) / 48);
	case KitRepeatDivision::ThirtySecond:
		return std::max<uint32_t>(1, sixteenthTicks * tickSamples / 2);
	default:
		return sixteenthTicks * tickSamples;
	}
}

RGB colourForRepeatDivision(int32_t index) {
	static constexpr RGB divisionColours[kKitRepeatDivisionCount] = {
	    RGB{255, 80, 80},  RGB{255, 140, 60}, RGB{255, 200, 60}, RGB{255, 255, 80},
	    RGB{180, 255, 80}, RGB{80, 255, 120}, RGB{80, 200, 255}, RGB{120, 120, 255},
	};
	if (index < 0 || index >= kKitRepeatDivisionCount) {
		return grey;
	}
	return divisionColours[index];
}

// Match session grid inactive clip brightness (~10/255) before Launchpad scaleRgb.
RGB dimForLaunchpadInactive(RGB colour) {
	return colour.transform(
	    [](uint8_t channel) { return static_cast<uint8_t>((static_cast<uint32_t>(channel) * 10) / 255); });
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
	if (isKitRepeatControlPad(x, y)) {
		return false;
	}
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

void updateHeldPadVelocity(int32_t x, int32_t y, uint8_t velocity) {
	for (HeldPad& pad : heldPads) {
		if (pad.active && pad.x == x && pad.y == y) {
			pad.velocity = velocity;
			return;
		}
	}
}

void clearHeldPadChordData(HeldPad& pad) {
	pad.chordNoteCount = 0;
	for (int32_t i = 0; i < kMaxChordKeyboardSize; i++) {
		pad.chordNotes[i] = 0;
	}
}

void injectNote(MIDICable& cable, bool on, int32_t midiChannel, int32_t note, uint8_t velocity) {
	bool doingMidiThru = false;
	int32_t liftVelocity = on ? velocity : kDefaultLiftValue;
	playbackHandler.noteMessageReceived(cable, on, midiChannel, note, liftVelocity, &doingMidiThru);
}

void injectAftertouch(MIDICable& cable, int32_t midiChannel, int32_t noteCode, uint8_t pressure) {
	bool doingMidiThru = false;
	playbackHandler.aftertouchReceived(cable, midiChannel, pressure, noteCode, &doingMidiThru);
}

void releaseChordPadNotes(MIDICable& cable, int32_t midiChannel, HeldPad& pad) {
	for (int32_t i = 0; i < pad.chordNoteCount; i++) {
		injectNote(cable, false, midiChannel, pad.chordNotes[i], 0);
	}
	clearHeldPadChordData(pad);
}

void setPadHeld(int32_t x, int32_t y, bool held, uint8_t velocity = 0, uint32_t nextRepeatSampleTime = 0) {
	if (held) {
		for (HeldPad& pad : heldPads) {
			if (!pad.active) {
				pad.x = static_cast<int8_t>(x);
				pad.y = static_cast<int8_t>(y);
				pad.active = true;
				pad.velocity = velocity;
				pad.nextRepeatSampleTime = nextRepeatSampleTime;
				clearHeldPadChordData(pad);
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
			pad.velocity = 0;
			pad.nextRepeatSampleTime = 0;
			clearHeldPadChordData(pad);
			return;
		}
	}
}

int32_t launchpadChordIndexFromPad(int32_t x, int32_t y) {
	if (x < 0 || x >= kLaunchpadWidth || y < 0 || y >= kExpressiveLpChordRows) {
		return -1;
	}
	return y * kLaunchpadWidth + x;
}

bool playExpressiveChordPad(MIDICable& cable, int32_t midiChannel, int32_t x, int32_t y, uint8_t velocity,
                            HeldPad& pad) {
	int32_t chordIndex = launchpadChordIndexFromPad(x, y);
	if (chordIndex < 0) {
		return false;
	}

	auto& layout = getExpressiveChordsLayout();
	layout.refreshSlotColours();
	int32_t notes[kMaxChordKeyboardSize];
	int32_t count = layout.buildChordNotes(chordIndex, notes);
	if (count <= 0) {
		return false;
	}

	for (int32_t i = 0; i < count; i++) {
		injectNote(cable, true, midiChannel, notes[i], velocity);
	}

	pad.chordNoteCount = static_cast<int8_t>(count);
	for (int32_t i = 0; i < count; i++) {
		pad.chordNotes[i] = notes[i];
	}
	return true;
}

void injectKitDrum(MIDICable& cable, bool on, int32_t midiChannel, int32_t drumIndex, uint8_t velocity) {
	injectNote(cable, on, midiChannel, drumIndex + midiEngine.midiFollowKitRootNote, velocity);
}

void retriggerKitDrum(MIDICable& cable, int32_t midiChannel, int32_t drumIndex, uint8_t velocity) {
	injectKitDrum(cable, false, midiChannel, drumIndex, 0);
	injectKitDrum(cable, true, midiChannel, drumIndex, velocity);
}

void clearHeldPads() {
	for (HeldPad& pad : heldPads) {
		pad.active = false;
		pad.x = -1;
		pad.y = -1;
		pad.velocity = 0;
		pad.nextRepeatSampleTime = 0;
		clearHeldPadChordData(pad);
	}
	noteModeCable = nullptr;
}

bool anyMelodicPadHeld(InstrumentClip* clip) {
	for (HeldPad const& pad : heldPads) {
		if (!pad.active) {
			continue;
		}
		if (melodicSubMode == MelodicSubMode::ExpressiveChords && pad.chordNoteCount > 0) {
			return true;
		}
		int32_t note = 0;
		if (isMelodicPadPlayable(pad.x, pad.y, clip, note)) {
			return true;
		}
	}
	return false;
}

void syncExpressiveLeds(InstrumentClip* clip, RGB launchpadImage[][kDisplayWidth + kSideBarWidth],
                        uint8_t launchpadOccupancyMask[][kDisplayWidth + kSideBarWidth]) {
	(void)clip;
	auto& layout = getExpressiveChordsLayout();
	layout.refreshSlotColours();
	KeyboardStateExpressiveChords& state = clip->keyboardState.expressiveChords;

	for (int32_t y = 0; y < kDisplayHeight; y++) {
		for (int32_t x = 0; x < kLaunchpadWidth; x++) {
			RGB colour = black;
			bool occupied = false;

			int32_t chordIndex = launchpadChordIndexFromPad(x, y);
			if (chordIndex >= 0) {
				colour = layout.slotColour(chordIndex);
				occupied = true;
				if (padIsHeld(x, y)) {
					colour = colour.transform([](uint8_t chan) { return chan / 2; });
				}
			}
			else if (y == kExpressiveLpInversionRowY) {
				colour = (x == state.inversion) ? cyan : cyan.dim(3 + x);
				occupied = true;
			}
			else if (y == kExpressiveLpSpreadRowY) {
				colour = (x == state.spread) ? green : green.dim(3 + x);
				occupied = true;
			}
			else if (y == kExpressiveLpNextSetY && x == kExpressiveLpSetControlX) {
				colour = magenta;
				occupied = true;
			}
			else if (y == kExpressiveLpPrevSetY && x == kExpressiveLpSetControlX) {
				colour = magenta.forTail();
				occupied = true;
			}
			else if (y == kExpressiveLpPrevSetY && x == kExpressiveLpOctaveX) {
				colour = white.dim(4);
				occupied = true;
			}
			else if (y == kExpressiveLpNextSetY && x == kExpressiveLpOctaveX) {
				colour = white.dim(3);
				occupied = true;
			}

			launchpadImage[y][x] = colour;
			launchpadOccupancyMask[y][x] = occupied ? 64 : 0;
		}
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

void syncKitRepeatLeds(RGB launchpadImage[][kDisplayWidth + kSideBarWidth],
                       uint8_t launchpadOccupancyMask[][kDisplayWidth + kSideBarWidth]) {
	for (int32_t y = kKitRepeatToggleY; y < kDisplayHeight; y++) {
		for (int32_t x = 0; x < kLaunchpadWidth; x++) {
			launchpadImage[y][x] = black;
			launchpadOccupancyMask[y][x] = 0;
		}
	}

	launchpadImage[kKitRepeatToggleY][kKitRepeatToggleX] = kitRepeatEnabled ? enabled : dimForLaunchpadInactive(green);
	launchpadOccupancyMask[kKitRepeatToggleY][kKitRepeatToggleX] = 64;

	for (int32_t x = 0; x < kKitRepeatDivisionCount; x++) {
		RGB colour = colourForRepeatDivision(x);
		if (static_cast<int32_t>(kitRepeatDivision) != x) {
			colour = dimForLaunchpadInactive(colour);
		}
		launchpadImage[kKitRepeatDivisionRowY][x] = colour;
		launchpadOccupancyMask[kKitRepeatDivisionRowY][x] = 64;
	}
}

void syncKitLeds(InstrumentClip* clip, RGB launchpadImage[][kDisplayWidth + kSideBarWidth],
                 uint8_t launchpadOccupancyMask[][kDisplayWidth + kSideBarWidth]) {
	for (int32_t y = 0; y < kKitDisplayRows; y++) {
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

	syncKitRepeatLeds(launchpadImage, launchpadOccupancyMask);
}

} // namespace

bool canEnterNoteMode() {
	return noteModeClip() != nullptr;
}

bool handleExpressiveControlPad(int32_t x, int32_t y) {
	auto& layout = getExpressiveChordsLayout();

	if (y == kExpressiveLpInversionRowY && x >= 0 && x < kLaunchpadWidth) {
		layout.setInversion(static_cast<uint8_t>(x));
		layout.refreshSlotColours();
		return true;
	}
	if (y == kExpressiveLpSpreadRowY && x >= 0 && x < kLaunchpadWidth) {
		layout.setSpread(static_cast<uint8_t>(x));
		layout.refreshSlotColours();
		return true;
	}
	if (y == kExpressiveLpNextSetY && x == kExpressiveLpSetControlX) {
		layout.changeChordSetByOffset(1);
		return true;
	}
	if (y == kExpressiveLpPrevSetY && x == kExpressiveLpSetControlX) {
		layout.changeChordSetByOffset(-1);
		return true;
	}
	if (y == kExpressiveLpPrevSetY && x == kExpressiveLpOctaveX) {
		layout.adjustTranspose(-kOctaveSize);
		return true;
	}
	if (y == kExpressiveLpNextSetY && x == kExpressiveLpOctaveX) {
		layout.adjustTranspose(kOctaveSize);
		return true;
	}
	return false;
}

bool handleExpressivePad(MIDICable& cable, int32_t x, int32_t y, uint8_t velocity, bool on, int32_t midiChannel) {
	if (on && handleExpressiveControlPad(x, y)) {
		return true;
	}

	int32_t chordIndex = launchpadChordIndexFromPad(x, y);
	if (chordIndex < 0) {
		return false;
	}

	noteModeCable = &cable;
	noteModeMidiChannel = midiChannel;

	if (on) {
		if (velocity == 0) {
			InstrumentClip* clip = noteModeClip();
			if (clip != nullptr) {
				velocity = static_cast<Instrument*>(clip->output)->defaultVelocity;
			}
		}
		if (padIsHeld(x, y)) {
			for (HeldPad& pad : heldPads) {
				if (pad.active && pad.x == x && pad.y == y) {
					releaseChordPadNotes(cable, midiChannel, pad);
					updateHeldPadVelocity(x, y, velocity);
					playExpressiveChordPad(cable, midiChannel, x, y, velocity, pad);
					return true;
				}
			}
		}
		for (HeldPad& pad : heldPads) {
			if (!pad.active) {
				pad.x = static_cast<int8_t>(x);
				pad.y = static_cast<int8_t>(y);
				pad.active = true;
				pad.velocity = velocity;
				pad.nextRepeatSampleTime = 0;
				playExpressiveChordPad(cable, midiChannel, x, y, velocity, pad);
				return true;
			}
		}
		return true;
	}

	for (HeldPad& pad : heldPads) {
		if (pad.active && pad.x == x && pad.y == y) {
			releaseChordPadNotes(cable, midiChannel, pad);
			pad.active = false;
			pad.x = -1;
			pad.y = -1;
			pad.velocity = 0;
			return true;
		}
	}
	return false;
}

bool handleScroll(int32_t offsetX, int32_t offsetY) {
	InstrumentClip* clip = noteModeClip();
	if (clip == nullptr) {
		return false;
	}

	if (layoutFor(clip) == LayoutKind::Melodic && melodicSubMode == MelodicSubMode::ExpressiveChords) {
		auto& layout = getExpressiveChordsLayout();
		if (offsetY != 0) {
			layout.changeChordSetByOffset(offsetY);
			return true;
		}
		if (offsetX != 0) {
			layout.adjustTranspose(offsetX);
			return true;
		}
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
		if (isKitRepeatTogglePad(x, y)) {
			if (on) {
				kitRepeatEnabled = !kitRepeatEnabled;
			}
			return true;
		}

		int32_t divisionIndex = 0;
		if (isKitRepeatDivisionPad(x, y, divisionIndex)) {
			if (on) {
				kitRepeatDivision = static_cast<KitRepeatDivision>(divisionIndex);
			}
			return true;
		}

		if (isKitRepeatControlPad(x, y)) {
			return true;
		}

		int32_t drumIndex = 0;
		if (!isKitPadPlayable(x, y, clip, drumIndex)) {
			return false;
		}

		noteModeCable = &cable;
		noteModeMidiChannel = midiChannel;

		if (on) {
			if (velocity == 0) {
				velocity = static_cast<Kit*>(clip->output)->defaultVelocity;
			}
			if (padIsHeld(x, y)) {
				updateHeldPadVelocity(x, y, velocity);
				return true;
			}
			uint32_t nextRepeat = 0;
			if (kitRepeatEnabled) {
				nextRepeat = AudioEngine::audioSampleTimer + kitRepeatIntervalSamples();
			}
			setPadHeld(x, y, true, velocity, nextRepeat);
			injectKitDrum(cable, true, midiChannel, drumIndex, velocity);
		}
		else {
			setPadHeld(x, y, false);
			injectKitDrum(cable, false, midiChannel, drumIndex, 0);
		}

		return true;
	}

	if (melodicSubMode == MelodicSubMode::ExpressiveChords) {
		return handleExpressivePad(cable, x, y, velocity, on, midiChannel);
	}

	int32_t note = 0;
	if (!isMelodicPadPlayable(x, y, clip, note)) {
		return false;
	}

	noteModeCable = &cable;
	noteModeMidiChannel = midiChannel;

	if (on) {
		if (velocity == 0) {
			velocity = static_cast<Instrument*>(clip->output)->defaultVelocity;
		}
		if (padIsHeld(x, y)) {
			updateHeldPadVelocity(x, y, velocity);
			injectNote(cable, false, midiChannel, note, 0);
			injectNote(cable, true, midiChannel, note, velocity);
			return true;
		}
		setPadHeld(x, y, true, velocity);
		injectNote(cable, true, midiChannel, note, velocity);
	}
	else {
		setPadHeld(x, y, false);
		injectNote(cable, false, midiChannel, note, 0);
	}

	return true;
}

bool handlePadPressure(int32_t x, int32_t y, uint8_t pressure) {
	if (x < 0 || x >= kLaunchpadWidth || y < 0 || y >= kDisplayHeight || pressure == 0) {
		return false;
	}

	InstrumentClip* clip = noteModeClip();
	if (clip == nullptr || noteModeCable == nullptr || !padIsHeld(x, y)) {
		return false;
	}

	if (layoutFor(clip) == LayoutKind::Melodic && melodicSubMode == MelodicSubMode::ExpressiveChords) {
		for (HeldPad& pad : heldPads) {
			if (pad.active && pad.x == x && pad.y == y) {
				updateHeldPadVelocity(x, y, pressure);
				for (int32_t i = 0; i < pad.chordNoteCount; i++) {
					injectAftertouch(*noteModeCable, noteModeMidiChannel, pad.chordNotes[i], pressure);
				}
				return true;
			}
		}
		return false;
	}

	if (layoutFor(clip) == LayoutKind::Kit) {
		if (y >= kKitDisplayRows) {
			return false;
		}
		updateHeldPadVelocity(x, y, pressure);
		return true;
	}

	int32_t note = 0;
	if (!isMelodicPadPlayable(x, y, clip, note)) {
		return false;
	}

	updateHeldPadVelocity(x, y, pressure);
	injectAftertouch(*noteModeCable, noteModeMidiChannel, note, pressure);
	return true;
}

bool handleChannelPressure(uint8_t pressure) {
	if (pressure == 0) {
		return false;
	}

	InstrumentClip* clip = noteModeClip();
	if (clip == nullptr || noteModeCable == nullptr) {
		return false;
	}

	if (layoutFor(clip) == LayoutKind::Kit) {
		bool updated = false;
		for (HeldPad& pad : heldPads) {
			if (pad.active && pad.y < kKitDisplayRows) {
				pad.velocity = pressure;
				updated = true;
			}
		}
		return updated;
	}

	if (layoutFor(clip) == LayoutKind::Melodic && anyMelodicPadHeld(clip)) {
		for (HeldPad& pad : heldPads) {
			if (pad.active) {
				pad.velocity = pressure;
			}
		}
		injectAftertouch(*noteModeCable, noteModeMidiChannel, -1, pressure);
		return true;
	}

	return false;
}

void serviceRepeats() {
	if (!kitRepeatEnabled || noteModeCable == nullptr) {
		return;
	}

	InstrumentClip* clip = noteModeClip();
	if (clip == nullptr || layoutFor(clip) != LayoutKind::Kit) {
		return;
	}

	uint32_t now = AudioEngine::audioSampleTimer;
	uint32_t interval = kitRepeatIntervalSamples();
	bool retriggered = false;

	for (HeldPad& pad : heldPads) {
		if (!pad.active || pad.y >= kKitDisplayRows) {
			continue;
		}

		int32_t drumIndex = kitDrumIndexFromPad(pad.x, pad.y, clip);
		if (drumIndex < 0 || drumIndex > highestKitDrumIndex(clip)) {
			continue;
		}

		if (pad.nextRepeatSampleTime == 0) {
			continue;
		}

		while (now >= pad.nextRepeatSampleTime) {
			retriggerKitDrum(*noteModeCable, noteModeMidiChannel, drumIndex, pad.velocity);
			pad.nextRepeatSampleTime += interval;
			retriggered = true;
		}
	}

	if (retriggered) {
		launchpad_extension::requestSync();
	}
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
		else if (melodicSubMode == MelodicSubMode::ExpressiveChords) {
			syncExpressiveLeds(clip, launchpadImage, launchpadOccupancyMask);
		}
		else {
			syncMelodicLeds(clip, launchpadImage, launchpadOccupancyMask);
		}
	}

	launchpad_extension::syncNoteView(launchpadImage, launchpadOccupancyMask);
}

void resetState() {
	clearHeldPads();
	kitRepeatEnabled = false;
	kitRepeatDivision = KitRepeatDivision::Sixteenth;
	melodicSubMode = MelodicSubMode::Isomorphic;
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
		else if (melodicSubMode == MelodicSubMode::ExpressiveChords) {
			releaseChordPadNotes(cable, midiChannel, pad);
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

bool toggleExpressiveChordsSubMode(MIDICable& cable, int32_t midiChannel) {
	InstrumentClip* clip = noteModeClip();
	if (clip == nullptr || layoutFor(clip) != LayoutKind::Melodic) {
		return false;
	}

	releaseAllHeldPads(cable, midiChannel);
	melodicSubMode = (melodicSubMode == MelodicSubMode::ExpressiveChords) ? MelodicSubMode::Isomorphic
	                                                                      : MelodicSubMode::ExpressiveChords;
	if (melodicSubMode == MelodicSubMode::ExpressiveChords) {
		getExpressiveChordsLayout().refreshSlotColours();
	}
	return true;
}

} // namespace launchpad_note_mode
