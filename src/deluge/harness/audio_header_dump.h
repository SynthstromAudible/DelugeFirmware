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

/// @brief Host-only C++ oracle for the corpus differential over `crates/deluge_audio_format`.
///
/// The C++ entry points (`parseSampleHeader()`/`parseWaveTableHeader()`) now call straight into this
/// crate themselves, so both sides of the differential are the same decoder — it no longer compares
/// this crate against an independent C++ implementation, but compares two different entry
/// points/walks through it (see `corpus_differential.rs`'s module doc for what that still catches).
///
/// `dumpAudioHeaderAsJson()` reads one WAV/AIFF file straight off the host filesystem (no efatfs, no
/// `DELUGE_SD_ROOT` — this is deliberately independent of the SD-card abstraction the rest of the app
/// uses, since all it needs is `AudioByteSource`'s four-call surface over an in-memory buffer), then
/// runs `parseSampleHeader()` and `parseWaveTableHeader()` (at both `makeWaveTableWorkAtAllCosts`
/// settings) against it, and renders every field plus the error/reject outcome of each as one JSON
/// line. `crates/deluge_audio_format/tests/corpus_differential.rs` runs the `deluge_audio_header_dump`
/// binary (see `src/bsp/host/host_audio_header_dump_main.cpp`) as a subprocess and parses this output
/// to compare field-by-field against the new Rust parser.
///
/// `DELUGE_HOST`-only, same reach as `harness/recorder_readback_probe.h`: compiled into
/// deluge_host/deluge_loadcheck/deluge_audio_header_dump, never the ARM device firmware.
#ifdef DELUGE_HOST

#include <string>

/// @brief Parse `path` as both a Sample and a WaveTable (at both `makeWaveTableWorkAtAllCosts`
///        settings) and render the result as one JSON object (no trailing newline).
///
/// Never throws. A file that can't be opened/read off the host filesystem is reported via the
/// top-level `"readError"` field rather than by throwing.
/// @param path Host filesystem path (NOT a Deluge SD-relative path) to a `.wav`/`.aif`/`.aiff` file.
/// @return A single-line JSON object describing the parse outcome.
std::string dumpAudioHeaderAsJson(const std::string& path);

#endif // DELUGE_HOST
