/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

// The app-typed preset tables (l10n::String names, Iterance presets), split out of lookuptables.cpp so that
// TU stays a gui-free leaf of numeric DSP tables. See util/lookuptables/preset_tables.h.

#include "util/lookuptables/preset_tables.h"
#include "gui/l10n/l10n.h"
#include "sequence/iterance.h"
#include <array>

deluge::l10n::String presetReverbNames[] = {
    deluge::l10n::String::STRING_FOR_SMALL_ROOM_REVERB,  //<
    deluge::l10n::String::STRING_FOR_MEDIUM_ROOM_REVERB, //<
    deluge::l10n::String::STRING_FOR_LARGE_ROOM_REVERB,  //<
};

const std::array<Iterance, 35> iterancePresets = {
    Iterance{2, 0b1},      Iterance{2, 0b10},      Iterance{3, 0b1},       Iterance{3, 0b10},
    Iterance{3, 0b100},    Iterance{4, 0b1},       Iterance{4, 0b10},      Iterance{4, 0b100},
    Iterance{4, 0b1000},   Iterance{5, 0b1},       Iterance{5, 0b10},      Iterance{5, 0b100},
    Iterance{5, 0b1000},   Iterance{5, 0b10000},   Iterance{6, 0b1},       Iterance{6, 0b10},
    Iterance{6, 0b100},    Iterance{6, 0b1000},    Iterance{6, 0b10000},   Iterance{6, 0b100000},
    Iterance{7, 0b1},      Iterance{7, 0b10},      Iterance{7, 0b100},     Iterance{7, 0b1000},
    Iterance{7, 0b10000},  Iterance{7, 0b100000},  Iterance{7, 0b1000000}, Iterance{8, 0b1},
    Iterance{8, 0b10},     Iterance{8, 0b100},     Iterance{8, 0b1000},    Iterance{8, 0b10000},
    Iterance{8, 0b100000}, Iterance{8, 0b1000000}, Iterance{8, 0b10000000}};

const std::array<deluge::l10n::String, MAX_CHORD_TYPES> chordNames = {
    deluge::l10n::String::STRING_FOR_NONE,   deluge::l10n::String::STRING_FOR_FIFTH,
    deluge::l10n::String::STRING_FOR_SUS2,   deluge::l10n::String::STRING_FOR_MINOR,
    deluge::l10n::String::STRING_FOR_MAJOR,  deluge::l10n::String::STRING_FOR_SUS4,
    deluge::l10n::String::STRING_FOR_MINOR7, deluge::l10n::String::STRING_FOR_DOMINANT7,
    deluge::l10n::String::STRING_FOR_MAJOR7,
};
