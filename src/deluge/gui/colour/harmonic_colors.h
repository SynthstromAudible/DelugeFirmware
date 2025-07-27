/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "gui/colour/colour.h"
#include "model/settings/runtime_feature_settings.h"

/**
 * Note Color Mapping (Chromatic / Harmonic)
 *
 * WHY:
 *  - Provide a consistent, musical way to color notes so the grid communicates theory at a glance.
 *  - The "Harmonic" mode follows circle‑of‑fifths relationships rather than repeating a hue per octave.
 *  - Improves readability and workflow when composing/performing, especially for players who think in
 *    functional harmony (key centers, closely related keys, secondary dominants).
 *
 * WHAT:
 *  - Adds a runtime‑configurable setting: Off / Chromatic / Harmonic.
 *  - "Chromatic" maps pitch class → color evenly around the 12‑tone wheel.
 *  - "Harmonic" remaps those hues so adjacent colors reflect the circle of fifths
 *    (…C–G–D–A–E–B–F♯–C♯–G♯–D♯–A♯–F…), aiding harmonic navigation.
 *
 * DESIGN NOTES:
 *  - Keeps defaults unchanged; users must opt in via Community Features.
 *  - Localization covered (OLED labels and 7‑segment abbreviations).
 *  - Backwards‑compatible: existing songs/clips render as before when the feature is Off.
 *
 * ATTRIBUTION:
 *  - Contributed by Myles de Bastion.
 *  - Intended to align with an emerging, open, standardized approach for mapping music theory to color.
 */

namespace deluge::gui::colour {

/**
 * @brief Harmonic color mapping system that assigns consistent colors to notes
 *
 * This system maps each note to a specific color that repeats every octave:
 * C = Red, C# = Orange, D = Orange-Yellow, D# = Yellow, E = Yellow-Green,
 * F = Green, F# = Green-Cyan, G = Cyan, G# = Blue, A = Blue-Magenta,
 * A# = Magenta, B = Violet
 */
class HarmonicColors {
public:
	/**
	 * @brief Get the harmonic color for a specific note
	 *
	 * @param note The MIDI note number (0-127)
	 * @return RGB The color for this note
	 */
	static RGB getNoteColor(uint8_t note);

	/**
	 * @brief Get the harmonic color for a note within an octave (0-11)
	 *
	 * @param noteInOctave The note within the octave (0-11, where 0=C, 1=C#, etc.)
	 * @return RGB The color for this note
	 */
	static RGB getNoteInOctaveColor(uint8_t noteInOctave);

private:
	// Pre-calculated harmonic colors for each note in an octave
	static constexpr RGB harmonicNoteColors[12] = {
	    RGB(255, 0, 0),   // C  - Red
	    RGB(255, 128, 0), // C# - Orange
	    RGB(255, 192, 0), // D  - Orange-Yellow
	    RGB(255, 255, 0), // D# - Yellow
	    RGB(192, 255, 0), // E  - Yellow-Green
	    RGB(0, 255, 0),   // F  - Green
	    RGB(0, 255, 128), // F# - Green-Cyan
	    RGB(0, 255, 255), // G  - Cyan
	    RGB(0, 128, 255), // G# - Blue
	    RGB(128, 0, 255), // A  - Blue-Magenta
	    RGB(255, 0, 255), // A# - Magenta
	    RGB(255, 0, 128)  // B  - Violet
	};
};

class NoteColorMapping {
public:
	/// Get the color for a specific note using the current mapping mode
	static RGB getNoteColor(uint8_t note);

	/// Get the color for a note within an octave (0-11) using chromatic mapping
	static RGB getChromaticNoteColor(uint8_t noteInOctave);

	/// Get the color for a note within an octave (0-11) using harmonic (circle of 5ths) mapping
	static RGB getHarmonicNoteColor(uint8_t noteInOctave);

private:
	/// Chromatic color mapping: C=Red, C#=Orange, D=Orange-Yellow, etc.
	static const RGB chromaticNoteColors[12];

	/// Harmonic color mapping (circle of 5ths): C=Red, G=Orange, D=Yellow, etc.
	static const RGB harmonicNoteColors[12];
};

} // namespace deluge::gui::colour
