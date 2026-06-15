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

#pragma once

#include "gui/l10n/strings.h"               // deluge::l10n::String
#include "model/iterance/iterance.h"        // Iterance
#include "util/lookuptables/lookuptables.h" // NUM_PRESET_REVERBS, MAX_CHORD_TYPES
#include <array>

// Preset name / iterance tables, split out of lookuptables.h. These need app-layer types (l10n::String,
// Iterance) and so would otherwise drag gui/l10n + model/iterance — and, via l10n/strings.h -> midi_device.h,
// model/model_stack.h — into every lookuptables.h consumer, including the synth DSP core (which needs only
// the numeric tables). Consumers of these specific tables include this header instead.
extern deluge::l10n::String presetReverbNames[NUM_PRESET_REVERBS];
extern const std::array<Iterance, 35> iterancePresets;
extern const std::array<deluge::l10n::String, MAX_CHORD_TYPES> chordNames;
