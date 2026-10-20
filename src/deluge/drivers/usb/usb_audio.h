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

#include <stdbool.h>
#include <stdint.h>

// USB audio (UAC1) as a peripheral: a 4-channel stream from the host, and a 4-channel stream to it, each 44.1kHz with
// 24-bit samples. The packets go between memory and the USB FIFOs by DMA (channels 0 and 1); the CPU only kicks off
// each transfer, from the USB and DMA interrupts - which also convert between USB's packed 24-bit samples and ours.
//
// Channels from the host: 1/2 go straight to the Deluge's outputs, 3/4 are an input for audio tracks and the sampler.
// Channels to the host: 1/2 are the line/mic input, exactly as the codec delivers it, 3/4 the main mix.
//
// Both endpoints are "asynchronous" - our sample clock sets the rate. To the host, we send 44 or 45 frames per USB
// frame to match how fast we're producing them. Our in endpoint is marked as implicit feedback for the out one, so the
// host sends us as many frames as we sent it, and they're copied straight through with no resampling. Hosts that don't
// do implicit feedback (like Windows) use the out stream's adaptive alternate setting instead, sending at their own
// clock's rate, which we cope with by dropping or repeating the odd frame.
// Each pair of channels from the host is laid out in a ring buffer exactly like the SSI RX buffer, so channels 3/4 can
// be used anywhere the line/mic input is.

#ifdef __cplusplus
extern "C" {
#endif

/// Called as USB starts up as a peripheral. Takes the user's USB audio setting (FlashStorage::usbAudioEnabled), which
/// then holds until the next start - it decides which configuration descriptor the host gets. Returns it
bool usbAudioStartPeripheral(void);
/// Whether USB audio is on for the current peripheral session
bool usbAudioActive(void);

// Hooks for the Renesas USB peripheral driver. These may be called from the USB interrupt or the USB task.

/// The host selected an alternate setting for one of our interfaces (or is about to - alternateSetting 0 stops
/// streaming before the pipe gets reconfigured). The out stream has two: 1 follows our clock through implicit
/// feedback, 2 is adaptive, following the host's
void usbAudioSetInterface(uint16_t interfaceNumber, uint16_t alternateSetting);
/// Bus reset, detach, or a new configuration - all streaming stops
void usbAudioStopAll(void);
/// From the USB interrupt, with the pipes whose BRDY status was set. Returns the ones it dealt with
uint16_t usbAudioHandleBrdy(uint16_t pipeBits);
/// From the USB interrupt, once per USB frame (1ms) while streaming either way
void usbAudioHandleSof(void);

#ifdef __cplusplus
}

#include "dsp/stereo_sample.h"
#include <cstddef>
#include <span>

namespace deluge::usb_audio {

/// Whether the host is currently sending us audio
bool receivingFromHost();
/// Whether the host is currently collecting our audio
bool sendingToHost();

/// Call before rendering a window. Fills both USB rings (below) from where they left off up to numSamples beyond
/// rxPosition - the SSI RX buffer position the render starts at, AudioEngine::i2sRXBufferPos - with whatever has
/// arrived from the host, or silence if nothing's streaming. txQueued is how many frames are already waiting in the SSI
/// TX buffer in front of this render. The input ring is only filled from the host while inputWanted; otherwise it's
/// left silent, saving the copy when nothing's reading it
void fillRings(uint32_t rxPosition, size_t numSamples, size_t txQueued, bool inputWanted);

/// The rings holding the audio from the host. They're the same size and layout as the SSI RX buffer, and kept in step
/// with it, so the same offset into any of them is the same moment in time. The input ring has channels 3/4, for
/// audio tracks and the sampler; the outputs ring has channels 1/2, to go straight to the outputs
int32_t* getInputRingStart();
int32_t* getInputRingEnd();
int32_t* getOutputsRingEnd();
/// The position in the input ring matching a position in the SSI RX buffer
int32_t* toInputRing(int32_t const* rxPosition);
/// The position in the outputs ring matching a position in the SSI RX buffer
int32_t* toOutputsRing(int32_t const* rxPosition);

/// Queue the main mix to send to the host, as channels 3/4. Does nothing if the host isn't collecting it. txPosition is
/// the SSI TX buffer position just after where these frames went. rxPosition is the SSI RX buffer position of the first
/// of them - channels 1/2 are the line/mic input from there on, read when each packet goes out.
void sendToHost(std::span<const StereoSample> frames, int32_t const* txPosition, int32_t const* rxPosition);

/// Whether to mix channels 1/2 from the host straight into the Deluge's outputs
extern bool directMonitoring;

} // namespace deluge::usb_audio
#endif
