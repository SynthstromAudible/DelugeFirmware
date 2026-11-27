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

#include <cstdint>
#include <span>

struct StereoSample;

/// Reads the internal mic as it arrives, for mic velocity.
///
/// The codec's input ring (SSI_RX_BUFFER_NUM_SAMPLES frames, ~46 ms) is written by DMA all the time. The audio engine
/// reads it at i2sRXBufferPos, which trails the DMA by 3-6 ms and jumps in 128-frame steps. This reader keeps its own
/// position and reads right up to the DMA, so a pad press handled now sees the mic up to now.
///
/// Frames are counted on a clock that starts at boot and never wraps in practice (27 hours at 44.1 kHz). Each frame
/// also carries the mono output that the audio engine sent to the codec at the same moment (for echo cancellation).
namespace deluge::mic_velocity::input {

using Frame = uint32_t;

/// What a block of frames is handed to. Up to kMaxBlock frames at a time, in order.
using Consumer = void (*)(Frame first, std::span<int32_t const> mic, std::span<int16_t const> ref);
constexpr int32_t kMaxBlock = 64;

/// The frame the DMA is writing now
Frame now();

/// Reads all new frames and passes them to the consumer, if there is one. Called from the audio routine and when a pad
/// press is handled. Cheap when there's no consumer.
void service();

/// Start or stop passing frames to a consumer (nullptr: stop). Reading starts from now.
void setConsumer(Consumer consumer);
[[nodiscard]] bool hasConsumer();

/// From AudioEngine::doSomeOutputting(): the frames just written to the codec, and how many frames are queued in its
/// output ring after them.
void feedOutput(std::span<StereoSample const> written, int32_t framesQueuedAfter);

/// Frames lost because the reader fell more than a ring behind (since boot)
[[nodiscard]] uint32_t framesDropped();

} // namespace deluge::mic_velocity::input
