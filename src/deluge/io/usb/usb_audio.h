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

#include "dsp/stereo_sample.h"
#include <cstddef>
#include <cstdint>
#include <span>

/// USB audio, as the audio engine sees it: a stereo stream from a USB host, usable anywhere the line/mic input is, and
/// a stereo stream to the host. The platform carries both over USB (libdeluge/usb_audio.h); this keeps the engine's
/// side of them.
namespace deluge::usb_audio {

/// Put the USB device on the bus, with the USB audio function if FlashStorage::usbAudioEnabled. Call once, after the
/// settings are read
void startDevice();
/// Whether this USB session presents audio - the setting as it was when the device started
bool present();

/// Whether the host is currently sending us audio
bool receivingFromHost();
/// Whether the host is currently collecting our audio
bool sendingToHost();

/// Call before rendering a block, with where the block starts in the USB input ring (see AudioEngine::getInputReadPos)
/// and its length. Fills that much of the ring with what has arrived from the host, or silence if nothing's streaming.
void fillInputRing(int32_t* at, size_t numSamples);

/// The USB input ring buffer. It's the same size and layout as AudioEngine's input ring, and kept in step with it, so
/// the same offset into either is the same moment in time.
int32_t* getInputRingStart();
int32_t* getInputRingEnd();

/// Queue a block's audio for the host. Does nothing if the host isn't collecting it.
void sendToHost(std::span<const StereoSample> frames);

/// The "USB input" setting: mix audio from the host straight into the Deluge's outputs, after the song's FX, when
/// nothing else (an audio track, or the sampler) is monitoring it
extern bool directMonitoring;

/// What we send to the host
enum class OutputSource : uint8_t {
	MAIN_MIX, // The Deluge's final output
	LINE_IN,  // The codec's line/mic input, as it arrives
};
constexpr uint8_t kNumOutputSources = 2;
extern OutputSource outputSource;

} // namespace deluge::usb_audio
