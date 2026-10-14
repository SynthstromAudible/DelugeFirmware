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

/// Level 3's copies of the app's own button-matrix and encoder-order tables (spec G §4.2), split
/// into their own dependency-free translation unit (`host_script_tables.c`) so BOTH the host BSP
/// (`sim/CMakeLists.txt`'s `deluge_host`/`deluge_loadcheck`) and the test suite
/// (`tests/spec/CMakeLists.txt`'s `deluge_spec`) can compile it. `host_script.c` proper pulls in
/// BSP statics (the shadows, `host_rendered_frames`, …) and so cannot be linked into `deluge_spec`
/// -- this header/TU pair carries only the two lookups, nothing else.
///
/// `tests/spec/harness/tables_spec.cpp` cross-checks the tables here against
/// `deluge_profile()` (`src/shell/profiles/deluge.cpp`), so the two copies cannot silently drift.
#ifndef HOST_SCRIPT_TABLES_H
#define HOST_SCRIPT_TABLES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Where a button name sits in the Deluge's button matrix.
///
/// The table is `src/shell/profiles/deluge.cpp`'s `kMatrix` (itself from
/// `src/definitions_cxx.hpp:59-84`), copied into C because the BSP may not depend on the app.
/// `tests/spec/harness/tables_spec.cpp` cross-checks the copy against `deluge_profile()` so the
/// two cannot drift.
/// @param name Button name, spelled as `shell::button_name()` spells it.
/// @param x    Set to the matrix column on success.
/// @param y    Set to the matrix row on success.
/// @return 1 when \p name is a known button, 0 otherwise.
int ui_script_button_xy(const char* name, uint8_t* x, uint8_t* y);

/// @brief Which firmware encoder index an `EncoderRole` name is wired to.
///
/// `0=scrollY(Vertical) 1=scrollX(Horizontal) 2=tempo 3=select 4=mod1(GoldUpper) 5=mod0(GoldLower)`
/// — `hid/encoder_input.cpp:56-63` for the order, `hid/encoders.h:80-81` for which gold knob each
/// `mod` is. This is the index `deluge_encoder_take_edges()` is called with.
/// @param role Role name, spelled as the level-2 runner spells it.
/// @return The index, or -1 when \p role names no encoder.
int ui_script_encoder_index(const char* role);

/// @brief The button name at matrix coordinate (\p x, \p y), or NULL if none sits there.
///
/// The reverse of `ui_script_button_xy`, over the same table. `host_script.c`'s `.state` writer
/// uses this to name an indicator LED by the button at its legacy `x + y*9` coordinate
/// (`indicator_leds.h:30-64` builds every `LED` value from its button's own coordinates, so the
/// matrix already *is* the LED table) — kept here rather than re-scanned from a second copy of
/// the table, which is exactly the drift this split exists to prevent.
/// @param x Matrix column.
/// @param y Matrix row.
/// @return The button's name, or NULL when no button sits at (\p x, \p y).
const char* ui_script_button_name_at(uint8_t x, uint8_t y);

#ifdef __cplusplus
}
#endif

#endif // HOST_SCRIPT_TABLES_H
