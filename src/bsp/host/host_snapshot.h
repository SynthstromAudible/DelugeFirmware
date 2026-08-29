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

/// Level 3's C port of `tests/harness/snapshot.{h,cpp}`'s `.pbm`/`.state` writers (spec G §3),
/// split into their own dependency-free translation unit (final review I-3) so BOTH the host BSP
/// (`sim/CMakeLists.txt`'s `deluge_host`/`deluge_loadcheck`) and the test suite
/// (`tests/spec/CMakeLists.txt`'s `deluge_spec`, for `tests/spec/harness/identity_spec.cpp`'s
/// cross-check) can compile it -- exactly the split `host_script_tables.c` already does for the
/// name tables (see that file's header comment). `host_script.c` proper pulls in BSP statics (the
/// shadows, `host_rendered_frames`, ...) and so cannot be linked into `deluge_spec`; this
/// header/TU pair carries only the writers, nothing that can see the app or the BSP.
///
/// Before I-3, the only thing holding this C port byte-identical to `tests/harness/snapshot.cpp`
/// was a comment ("MUST stay byte-identical"). `identity_spec.cpp` now drives one fact set
/// through both writers and asserts the bytes match -- read it, not just this file, before
/// touching either side.
#ifndef HOST_SNAPSHOT_H
#define HOST_SNAPSHOT_H

#include "harness/ui_script.h" // UI_SCRIPT_STATE_LINE_BYTES -- the one shared line-width constant

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/// How many `.state` lines one snapshot can hold before `host_snapshot_add*` start reporting
/// overflow (`HostSnapshotLines::overflowed`). Deliberately roomier than level 2's `StateLines`
/// cap of 256: level 3 can legitimately produce more lines (up to 18 x 8 = 144 pads, 36 LEDs and
/// 2 indicators is already 182, and the pad shadow is 32 columns wide) -- see `host_script.c`'s
/// `HOST_SCRIPT_PAD_COLS` note.
#define HOST_SNAPSHOT_MAX_LINES 512

/// @brief The lines of one `.state` snapshot, formatted and sorted on demand -- the C mirror of
/// `deluge::harness::StateLines`.
typedef struct HostSnapshotLines {
	char lines[HOST_SNAPSHOT_MAX_LINES][UI_SCRIPT_STATE_LINE_BYTES];
	size_t count;
	/// True once a line was dropped entirely because `lines` was already at
	/// `HOST_SNAPSHOT_MAX_LINES` capacity -- mirrors `StateLines::overflowed` (final review I-1).
	/// A caller that sees this set must fail the run rather than write a truncated `.state`.
	int overflowed;
} HostSnapshotLines;

/// @brief Reset \p s to empty.
void host_snapshot_reset(HostSnapshotLines* s);

/// @brief Append one already-formatted line (no trailing newline), printf-style.
///
/// The line's own text is truncated (not rejected) at `UI_SCRIPT_STATE_LINE_BYTES - 1` characters
/// -- mirrors `StateLines::add`'s per-line truncation. Dropped entirely (and `s->overflowed` set)
/// when \p s is already at `HOST_SNAPSHOT_MAX_LINES` -- mirrors `StateLines::add`'s `bool` return.
void host_snapshot_addf(HostSnapshotLines* s, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

/// @brief Append a `pad x y r g b` line, unless the colour is black (skipped -- only non-black
/// pads are ever shown). Mirrors `deluge::harness::add_pad_line`.
void host_snapshot_add_pad(HostSnapshotLines* s, uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b);

/// @brief Append a `led <name> on` or `led <name> blink <period_ms>` line. Mirrors
/// `deluge::harness::add_led_line`.
void host_snapshot_add_led(HostSnapshotLines* s, const char* name, int blink, uint16_t period_ms);

/// @brief Append an `indicator upper|lower a b c d` line. \p levels holds exactly 4 levels.
/// Mirrors `deluge::harness::add_indicator_line`.
void host_snapshot_add_indicator(HostSnapshotLines* s, int upper, const uint8_t levels[4]);

/// @brief Write \p page_major as a P1 (plain-text) PBM to \p f. Byte-for-byte
/// `deluge::harness::oled_pbm` -- the header `P1\n<w> <h>\n`, then one line per pixel row of
/// `'1'`/`'0'` separated by single spaces with no trailing space, each newline-terminated.
/// @return 1 on success (`ferror(f) == 0`), 0 otherwise.
int host_snapshot_write_pbm(FILE* f, const uint8_t* page_major, uint16_t w, uint16_t h);

/// @brief Sort \p s's lines into spec G §3's fixed category order (`scene`, `led`, `pad`,
/// `indicator`, `lock`, `notice`, then anything unrecognised; ASCII-sorted within each category)
/// and write them to \p f, one per line. Byte-for-byte `StateLines::sorted_text`.
/// @return 1 on success (`ferror(f) == 0`), 0 otherwise.
int host_snapshot_write_state(FILE* f, HostSnapshotLines* s);

#ifdef __cplusplus
}
#endif

#endif // HOST_SNAPSHOT_H
