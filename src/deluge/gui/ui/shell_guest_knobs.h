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
/// @brief The menu shell's gold knobs, for the root views to share.
///
/// Its own header, not `shell_guest_services.h`'s, because it names a port type: `shell_guest_services.h` names none,
/// so `shell_host_ui.cpp` and the host spec suite include it without the `deluge_scenes` closure.

namespace deluge::scenes::services {
class ModKnobs;
}

namespace deluge::gui::ui {

/// @brief The one `ModKnobs`: the guest's service, which the root views drive too (`root_knobs`). Built over
///        `shellHostUI`'s `Shell` on first use.
/// @return The knobs. Call `refreshKnobsForRoot()` before a root gesture reads them.
deluge::scenes::services::ModKnobs& sharedModKnobs();

} // namespace deluge::gui::ui
