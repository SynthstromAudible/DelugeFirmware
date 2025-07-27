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

#include "harmonic_colors.h"
#include "model/settings/runtime_feature_settings.h"

namespace deluge::gui::colour {

// Chromatic color mapping (smoother hue steps; fewer yellows & blues bunched; no grays)
const RGB NoteColorMapping::chromaticNoteColors[12] = {
    RGB(255, 0, 0),   // C   - Red
    RGB(255, 64, 0),  // C#/Db - Red-Orange (more red-weighted vs prior Orange)
    RGB(255, 112, 0), // D   - Orange (pulled a bit redward)
    RGB(255, 176, 0), // D#/Eb - Amber (between orange & yellow)
    RGB(128, 255, 0), // E   - Yellow-Green (more green to reduce E→F jump)
    RGB(0, 255, 0),   // F   - Green
    RGB(0, 255, 64),  // F#/Gb - Green-Cyan (more green; less blue-heavy)
    RGB(0, 255, 200), // G   - Cyan (slight blue reduction to smooth to G#)
    RGB(0, 128, 255), // G#/Ab - Blue-Cyan (smoother bridge to A=Blue)
    RGB(0, 0, 255),   // A   - Blue
    RGB(64, 0, 192),  // A#/Bb - Indigo (kept distinct from A and B)
    RGB(255, 0, 192)  // B   - Red-Violet (more red-weighted; less like Bb)
};

// Harmonic mapping = circle-of-fifths reorder of the chromatic palette above
// Order (by fifths): C, G, D, A, E, B, F#, C#, G#, D#, A#, F
const RGB NoteColorMapping::harmonicNoteColors[12] = {
    chromaticNoteColors[0],  // C
    chromaticNoteColors[7],  // G
    chromaticNoteColors[2],  // D
    chromaticNoteColors[9],  // A
    chromaticNoteColors[4],  // E
    chromaticNoteColors[11], // B
    chromaticNoteColors[6],  // F#
    chromaticNoteColors[1],  // C#
    chromaticNoteColors[8],  // G#
    chromaticNoteColors[3],  // D#
    chromaticNoteColors[10], // A#
    chromaticNoteColors[5]   // F
};

RGB NoteColorMapping::getNoteColor(uint8_t note) {
	uint8_t noteInOctave = note % 12;

	// Get the current mapping mode from runtime settings
	auto mappingMode =
	    static_cast<RuntimeFeatureStateNoteColorMapping>(runtimeFeatureSettings.get(RuntimeFeatureSettingType::NoteColorMapping));

	switch (mappingMode) {
	case RuntimeFeatureStateNoteColorMapping::NoteColorMappingChromatic:
		return getChromaticNoteColor(noteInOctave);
	case RuntimeFeatureStateNoteColorMapping::NoteColorMappingHarmonic:
		return getHarmonicNoteColor(noteInOctave);
	default:
		// Return default color (this shouldn't happen when feature is enabled)
		return RGB::fromHue(note * -8 / 3);
	}
}

RGB NoteColorMapping::getChromaticNoteColor(uint8_t noteInOctave) {
	if (noteInOctave >= 12) {
		noteInOctave = noteInOctave % 12;
	}
	return chromaticNoteColors[noteInOctave];
}

RGB NoteColorMapping::getHarmonicNoteColor(uint8_t noteInOctave) {
	if (noteInOctave >= 12) {
		noteInOctave = noteInOctave % 12;
	}
	return harmonicNoteColors[noteInOctave];
}

} // namespace deluge::gui::colour
