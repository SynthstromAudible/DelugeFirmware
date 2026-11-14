/*
 * Copyright © 2026 Synthstrom Audible Limited
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

/// @file
/// @brief Where MIDI follow reaches the controls library's facts and sink: the app's, which the app's binding host
///        (gui/ui/shell_bind_host.cpp) holds. Defined there, as io/ may not reach the scenes that declare them.

#include "controls/knob_focus.h"

class Clip;

namespace deluge::controls {
class Facts;
class Sink;
} // namespace deluge::controls

namespace deluge::midi {

/// @brief The facts MIDI follow reads params and the takeover's inputs through: the app's.
[[nodiscard]] const controls::Facts& followFacts();

/// @brief The sink MIDI follow's writes go to: the app's.
[[nodiscard]] controls::Sink& followSink();

/// @brief What @p clip's owner is, as the focus MIDI follow's CCs move params of (gui::knobFocusOfClip).
[[nodiscard]] controls::KnobFocus followFocus(Clip& clip);

} // namespace deluge::midi
