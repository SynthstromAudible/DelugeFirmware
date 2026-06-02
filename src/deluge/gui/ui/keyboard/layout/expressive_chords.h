/*
 * Copyright © 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#pragma once

#include "gui/ui/keyboard/expressive_chord_sets.h"
#include "gui/ui/keyboard/layout/column_controls.h"

namespace deluge::gui::ui::keyboard::layout {

/// Ableton Expressive Chords–style layout: one pad triggers a full pre-voiced chord from a feel-based set.
class KeyboardLayoutExpressiveChords : public ColumnControlsKeyboard {
public:
	KeyboardLayoutExpressiveChords() = default;
	~KeyboardLayoutExpressiveChords() override = default;

	void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) override;
	void handleVerticalEncoder(int32_t offset) override;
	void handleHorizontalEncoder(int32_t offset, bool shiftEnabled, PressedPad presses[kMaxNumKeyboardPadPresses],
	                             bool encoderPressed = false) override;
	void precalculate() override;
	void renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) override;

	l10n::String name() override { return l10n::String::STRING_FOR_KEYBOARD_LAYOUT_EXPRESSIVE_CHORDS; }
	bool supportsInstrument() override { return true; }
	bool supportsKit() override { return false; }
	RequiredScaleMode requiredScaleMode() override { return RequiredScaleMode::Disabled; }

	/// Merge externally held MIDI chord keys into the active note set (called from KeyboardScreen).
	[[gnu::noinline]] void appendMidiHeldSlots();

protected:
	bool allowSidebarType(ColumnControlFunction sidebarType) override;

private:
	int32_t padToChordIndex(int32_t x, int32_t y);
	bool isSetControlPad(int32_t x, int32_t y, int32_t* setOffset);
	bool isInversionPad(int32_t x, int32_t y, int32_t* inversionOut);
	bool isSpreadPad(int32_t x, int32_t y, int32_t* spreadOut);
	void changeChordSet(int32_t offset);
	int32_t computeSlotRoot(int32_t chordIndex, const ExpressiveChordSet& set);
	bool slotRootInHarmonicScale(int32_t chordIndex, const ExpressiveChordSet& set);
	Voicing voicingForSlot(const ExpressiveChordSlot& slot);
	void playVoicing(int32_t root, const Voicing& voicing, uint8_t vel);
	void playSlot(int32_t chordIndex, uint8_t vel);
	[[gnu::noinline]] void playSlotIntoNotesState(int32_t chordIndex, uint8_t vel);
	void drawChordName(int16_t noteCode, const char* chordName, const char* voicingName);
	std::array<RGB, kExpressiveChordsPerSet> slotColours{};
};

} // namespace deluge::gui::ui::keyboard::layout
