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
#include "util/lookuptables/lookuptables.h" // MAX_CHORD_TYPES
#include <array>

// The preset name tables, split out of lookuptables.h. They need an app-layer type (l10n::String), and so would
// otherwise drag gui/l10n -- and, via l10n/strings.h -> midi_device.h, model/model_stack.h -- into every lookuptables.h
// consumer, including the synth DSP core (which needs only the numeric tables). Consumers of these tables include this
// header instead. The iterance presets are the project's (project/iterance.h).
extern const std::array<deluge::l10n::String, MAX_CHORD_TYPES> chordNames;
