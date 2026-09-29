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
/// @brief A host-only trace of the sample-load path.
///
/// Exists so a restructuring of how samples are loaded can be shown to make the same calls, in the same order, as
/// the code it replaces. The hooks sit in leaf functions that the restructuring leaves in place:
/// `AudioFileManager::getAudioFileFromFilename`, `AudioEngine::routineWithClusterLoading`, `shouldAbortLoading` and
/// `AudioFileHolder::setAudioFile`. Each hook emits one line to the sink, when one is installed.
///
/// Compiled only under `DELUGE_HOST`. Everywhere else @ref DELUGE_LOAD_TRACE expands to nothing and its arguments
/// are never evaluated, so no device image carries any of this.

#ifdef DELUGE_HOST

namespace deluge::storage::load_trace {

/// @brief Receives one formatted trace line, without a trailing newline.
using Sink = void (*)(const char* line);

/// @brief Install @p newSink, or turn tracing off with nullptr (the default).
/// @return The sink it replaces.
Sink setSink(Sink newSink);

/// @brief Format one line and hand it to the installed sink. Does nothing while no sink is installed.
/// @param format printf-style format.
void emit(const char* format, ...) __attribute__((format(printf, 1, 2)));

} // namespace deluge::storage::load_trace

#define DELUGE_LOAD_TRACE(...) ::deluge::storage::load_trace::emit(__VA_ARGS__)

#else

#define DELUGE_LOAD_TRACE(...) ((void)0)

#endif // DELUGE_HOST
