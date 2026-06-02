/*
 * Copyright © 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "gui/ui/keyboard/chords.h"
#include <array>
#include <cstdint>

namespace deluge::gui::ui::keyboard {

/// Chord pad grid: 8 wide × 4 tall (32 pads). Deluge pads: x = column, y = row, (0,0) = bottom-left.
/// Bottom-left chord pad is column 2, row 0 → firmware (x=2, y=0).
constexpr int32_t kExpressiveChordOriginX = 2;
constexpr int32_t kExpressiveChordOriginY = 0;
constexpr int32_t kExpressiveChordColumns = 8;
constexpr int32_t kExpressiveChordRows = 4;
constexpr int32_t kExpressiveChordsPerSet = kExpressiveChordColumns * kExpressiveChordRows;
/// Pad (x=2, y=0) — bottom-left chord; defines the scale for the whole grid.
constexpr int32_t kExpressiveRootChordIndex = 0;

/// Column 12 / 13: inversion & spread; row y=0 (bottom) .. y=7 (top) selects amount 0–7.
constexpr int32_t kExpressiveInversionColumnX = 12;
constexpr int32_t kExpressiveSpreadColumnX = 13;
constexpr int32_t kExpressiveControlSteps = kDisplayHeight;
constexpr size_t kExpressiveChordSetCount = 4;

/// One pad in the expressive grid: a pre-voiced chord at a fixed root (Ableton-style "feel" slot).
struct ExpressiveChordSlot {
	const Chord* chord;
	uint8_t voicingIndex;
	/// Semitones above middle C (60) before clip transpose / noteOffset.
	int8_t rootMidi;
};

struct ExpressiveChordSet {
	const char* name;
	std::array<ExpressiveChordSlot, kExpressiveChordsPerSet> slots;
};

extern const std::array<ExpressiveChordSet, kExpressiveChordSetCount> expressiveChordSets;

} // namespace deluge::gui::ui::keyboard
