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

#include "dsp/core/stereo_sample.h"
#include "libdeluge/audio_ports.h"
#include "util/functions.h"
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

/// The render block's USB port (libdeluge/audio_ports.h) and its `frames`, for the rest of the block: what the host
/// sent, where our audio for it goes, and whether the host is there each way. A port with no pairs (USB audio off),
/// none, or an `offline` block (an export, which renders at its own pace) reads silence and takes nothing.
void beginBlock(DelugeAudioPort* port, size_t frames, bool offline);

/// Whether the host is sending us audio this block
bool receivingFromHost();
/// Whether the host is collecting our audio this block
bool sendingToHost();

/// Call before rendering a block, with where the block starts in the USB input ring (see AudioEngine::getInputReadPos)
/// and its length. Fills that much of the ring with the block's USB input, or silence if nothing's streaming.
void fillInputRing(int32_t* at, size_t numSamples);

/// The USB input ring buffer. It's the same size and layout as AudioEngine's input ring, and kept in step with it, so
/// the same offset into either is the same moment in time.
int32_t* getInputRingStart();
int32_t* getInputRingEnd();

/// The block's audio for the host, into the USB port after what this block has sent already (the render may split a
/// block), up to the block's end. Does nothing if the port has no output pair; otherwise the pair is no longer silent.
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
