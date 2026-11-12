/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include <cstdint>

/// @brief The app's one foreground recording: resampling, Shift+RECORD output recording, stem export and the
///        audio-input take. Its recorder and channel are published through `AudioEngine::setForegroundRecording`
///        and read back with `AudioEngine::foregroundRecorder()` / `AudioEngine::foregroundRecordingSource()`.
namespace deluge::recording {

/// @brief Starts recording the app's output (or @p channel) to a new file in @p folder, and publishes it.
/// @return false, with nothing published, while a recording is already live or when no recorder could be made.
bool beginOutputRecording(AudioRecordingFolder folder = AudioRecordingFolder::RESAMPLE,
                          AudioInputChannel channel = AudioInputChannel::OUTPUT, bool writeLoopPoints = false,
                          bool shouldNormalize = true);
/// @brief Stops a capturing recording @p buttonLatency samples back and shows "Working" until it is finished.
void endRecordingSoon(int32_t buttonLatency = 0);
/// @brief Finishes a settled internal-source take. Never touches an input take, which the recorder UI finishes.
void slowRoutine();
/// Whether an internal-source recording (resampling, stem export) is capturing.
bool isCurrentlyResampling();
/// Whether an audio-input take is in progress: a recorder published on `LEFT` or `STEREO`.
bool inputTakeInProgress();

/// @brief Starts the recorder UI's audio-input take on @p channel, @p numChannels wide, into the RECORD folder, and
///        publishes it.
/// @return false, with nothing published, while a recording is already live or when no recorder could be made.
bool beginInputRecording(AudioInputChannel channel, int32_t numChannels);
/// @brief Unpublishes and frees the recorder UI's settled input take.
void finishInputRecording();

} // namespace deluge::recording
