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

/* Which paths the async sample loader will accept -- the one rule that has to hold identically on
 * both sides of libdeluge/audio_load.h.
 *
 * C++ (AudioFileManager::admissibleAsyncLoadPath) checks it BEFORE requesting, so a path the loader
 * would refuse permanently never becomes a request that is retried forever. Rust
 * (cstr_to_path/decode_candidates, audio_loader.rs) applies it when the request arrives. A LOOSER
 * C++ rule is that forever-retry coming back; a stricter one silently costs synchronous loads. So
 * neither side owns a private copy of the corpus: src/bsp/host/host_loadcheck_main.cpp #includes
 * this file, and src/bsp/rust/tests/audio_loader_host.rs include_str!()s and parses it, and either
 * side drifting fails a test.
 *
 * Each line is one case:
 *
 *   DELUGE_PATH_ADMISSION_CASE(accepted, bytes, repeat, label)
 *
 *     accepted  `true` if the loader must accept it, `false` if it must refuse it.
 *     bytes     A C string literal. ONLY plain printable ASCII and \xNN escapes -- the Rust side
 *               parses this file with a deliberately simple reader and understands nothing else.
 *     repeat    How many times `bytes` is repeated to form the case (1 for a literal case). This is
 *               how the length cases stay readable rather than 256 characters wide.
 *     label     What the case is about, for a failure message.
 */

/* Ordinary paths, and the boundary the fixed-capacity buffer imposes: 255 bytes fits, 256 does not
 * (DELUGE_MAX_PATH counts the NUL). */
DELUGE_PATH_ADMISSION_CASE(true, "SAMPLES/KICK.WAV", 1, "an ordinary path")
DELUGE_PATH_ADMISSION_CASE(true, "", 1, "the empty path")
DELUGE_PATH_ADMISSION_CASE(true, "A", 255, "255 bytes, the longest that fits")
DELUGE_PATH_ADMISSION_CASE(false, "A", 256, "256 bytes, one past what fits")
DELUGE_PATH_ADMISSION_CASE(false, "A", 300, "well past what fits")

/* Well-formed UTF-8 of every length, including a path that is only just short enough once its
 * multi-byte characters are counted in BYTES rather than characters. */
DELUGE_PATH_ADMISSION_CASE(true, "SAMPLES/CAF\xC3\x89.WAV", 1, "2-byte UTF-8 (U+00C9)")
DELUGE_PATH_ADMISSION_CASE(true, "SAMPLES/\xE2\x99\xAA.WAV", 1, "3-byte UTF-8 (U+266A)")
DELUGE_PATH_ADMISSION_CASE(true, "SAMPLES/\xF0\x9F\xA5\x81.WAV", 1, "4-byte UTF-8 (U+1F941)")
DELUGE_PATH_ADMISSION_CASE(false, "\xC3\xA9", 128, "256 bytes of well-formed UTF-8")

/* Ill-formed UTF-8. A FAT long filename is UCS-2 on the card, so these are not hypothetical: any of
 * them can arrive as a real path. */
DELUGE_PATH_ADMISSION_CASE(false, "SAMPLES/\xFF.WAV", 1, "0xFF, which no sequence may contain")
DELUGE_PATH_ADMISSION_CASE(false, "SAMPLES/\xC0\xAF.WAV", 1, "an overlong '/' (2-byte)")
DELUGE_PATH_ADMISSION_CASE(false, "SAMPLES/\xE0\x80\xAF.WAV", 1, "an overlong '/' (3-byte)")
DELUGE_PATH_ADMISSION_CASE(false, "SAMPLES/\xF0\x80\x80\xAF.WAV", 1, "an overlong '/' (4-byte)")
DELUGE_PATH_ADMISSION_CASE(false, "SAMPLES/\xED\xA0\x80.WAV", 1, "a surrogate (U+D800)")
DELUGE_PATH_ADMISSION_CASE(false, "SAMPLES/\xF4\x90\x80\x80.WAV", 1, "past U+10FFFF")
DELUGE_PATH_ADMISSION_CASE(false, "SAMPLES/\xE2\x99", 1, "a truncated 3-byte sequence at the end")
DELUGE_PATH_ADMISSION_CASE(false, "SAMPLES/\xE2\x99.WAV", 1, "a truncated 3-byte sequence mid-path")
DELUGE_PATH_ADMISSION_CASE(false, "SAMPLES/\xA9.WAV", 1, "a continuation byte with no lead")
