/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "gui/l10n/strings.h"
#include "model/iterance/iterance.h"
#include <array>
#include <cstdint>

extern const uint16_t centAdjustTableSmall[];
extern const uint16_t decayTableSmall4[];
extern const uint16_t decayTableSmall8[];
extern const uint16_t expTableSmall[];
extern const int16_t tanHSmall[];
extern const int16_t tanH2d[][129];
extern const int16_t hanningWindow[];

extern const int16_t sineWaveSmall[];
extern const int16_t sineWaveDiff[];
#define SINE_TABLE_SIZE_MAGNITUDE 8

extern const int16_t triangleWaveAntiAliasing1[];
extern const int16_t triangleWaveAntiAliasing3[];
extern const int16_t triangleWaveAntiAliasing5[];
extern const int16_t triangleWaveAntiAliasing7[];
extern const int16_t triangleWaveAntiAliasing9[];
extern const int16_t triangleWaveAntiAliasing15[];
extern const int16_t triangleWaveAntiAliasing21[];
extern const int16_t triangleWaveAntiAliasing31[];

extern const int16_t sawWave1[];
extern const int16_t sawWave3[];
extern const int16_t sawWave5[];
extern const int16_t sawWave7[];
extern const int16_t sawWave9[];
extern const int16_t sawWave13[];
extern const int16_t sawWave19[];
extern const int16_t sawWave27[];
extern const int16_t sawWave39[];
extern const int16_t sawWave53[];
extern const int16_t sawWave76[];
extern const int16_t sawWave109[];
extern const int16_t sawWave153[];
extern const int16_t sawWave215[];

extern const int16_t squareWave1[];
extern const int16_t squareWave3[];
extern const int16_t squareWave5[];
extern const int16_t squareWave7[];
extern const int16_t squareWave9[];
extern const int16_t squareWave13[];
extern const int16_t squareWave19[];
extern const int16_t squareWave27[];
extern const int16_t squareWave39[];
extern const int16_t squareWave53[];
extern const int16_t squareWave76[];
extern const int16_t squareWave109[];
extern const int16_t squareWave153[];
extern const int16_t squareWave215[];

extern const int16_t mysterySynthASaw_153[];
extern const int16_t mysterySynthASaw_215[];
extern const int16_t mysterySynthASaw_305[];
extern const int16_t mysterySynthASaw_431[];
extern const int16_t mysterySynthASaw_609[];
extern const int16_t mysterySynthASaw_861[];
extern const int16_t mysterySynthASaw_1217[];
extern const int16_t mysterySynthASaw_1722[];

extern const int16_t mysterySynthBSaw_1[];
extern const int16_t mysterySynthBSaw_3[];
extern const int16_t mysterySynthBSaw_5[];
extern const int16_t mysterySynthBSaw_7[];
extern const int16_t mysterySynthBSaw_9[];
extern const int16_t mysterySynthBSaw_13[];
extern const int16_t mysterySynthBSaw_19[];
extern const int16_t mysterySynthBSaw_27[];
extern const int16_t mysterySynthBSaw_39[];
extern const int16_t mysterySynthBSaw_53[];
extern const int16_t mysterySynthBSaw_76[];
extern const int16_t mysterySynthBSaw_109[];

extern const int16_t analogSquare_1[];
extern const int16_t analogSquare_3[];
extern const int16_t analogSquare_5[];
extern const int16_t analogSquare_7[];
extern const int16_t analogSquare_9[];
extern const int16_t analogSquare_13[];
extern const int16_t analogSquare_19[];
extern const int16_t analogSquare_27[];
extern const int16_t analogSquare_39[];
extern const int16_t analogSquare_53[];
extern const int16_t analogSquare_76[];
extern const int16_t analogSquare_109[];
extern const int16_t analogSquare_153[];
extern const int16_t analogSquare_215[];
extern const int16_t analogSquare_305[];
extern const int16_t analogSquare_431[];
extern const int16_t analogSquare_609[];
extern const int16_t analogSquare_861[];
extern const int16_t analogSquare_1217[];
extern const int16_t analogSquare_1722[];

// Begins at E (4 semitones above C). So that this octave contains the largest values (phase increments) possible
// without going over 22.05kHz (2147483648), even when shifted up a semitone (via osc-cents and unison combined)
extern const int32_t noteFrequencyTable[12];
extern const int32_t noteIntervalTable[12];
extern const int32_t timeStretchAdjustTable[193];
extern const int32_t attackRateTable[51];
extern const int32_t releaseRateTable[51];
extern const int32_t releaseRateTable64[65];
extern const int32_t tanTable[65];
extern const int16_t oldResonanceCompensation[];

#define NUM_PRESET_REVERBS 3
const uint8_t presetReverbRoomSize[NUM_PRESET_REVERBS] = {16, 30, 44};
const uint8_t presetReverbDamping[NUM_PRESET_REVERBS] = {29, 36, 45};
extern deluge::l10n::String presetReverbNames[NUM_PRESET_REVERBS];

extern const uint8_t noteCodeToNoteLetter[];
extern const uint8_t noteCodeToNoteLetterFlats[];
extern const bool noteCodeIsSharp[];

extern const std::array<Iterance, 35> iterancePresets;

#define MAX_CHORD_TYPES 9
#define MAX_CHORD_NOTES 4
extern uint8_t chordTypeSemitoneOffsets[MAX_CHORD_TYPES][MAX_CHORD_NOTES];
extern uint8_t chordTypeNoteCount[MAX_CHORD_TYPES];
extern const std::array<deluge::l10n::String, MAX_CHORD_TYPES> chordNames;
