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

// USB audio (UAC1) as a peripheral: a stereo stream from the host, and a stereo stream to it, each 44.1kHz with 24-bit
// samples. The packets go between memory and the USB FIFOs by DMA (channels 0 and 1); the CPU only kicks off each
// transfer, from the USB and DMA interrupts.
//
// Both endpoints are "asynchronous" - our sample clock sets the rate. To the host, we send 44 or 45 frames per USB
// frame to match how fast we're producing them. Our in endpoint is marked as implicit feedback for the out one, so the
// host sends us as many frames as we sent it, and they're copied straight through with no resampling. Hosts that don't
// do implicit feedback send at their own clock's rate, which we cope with by dropping or repeating the odd frame.
// Audio from the host is laid out in a ring buffer exactly like the SSI RX buffer, so it can be used anywhere the
// line/mic input is.

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

/// Call before rendering a window. Fills the USB input ring, from where it left off up to numSamples beyond
/// rxPosition (the SSI RX buffer position the render starts at - AudioEngine::i2sRXBufferPos), with whatever has
/// arrived from the host - or silence, if nothing's streaming. txQueued is how many frames are already waiting in the
/// SSI TX buffer in front of this render.
void fillInputRing(uint32_t rxPosition, size_t numSamples, size_t txQueued);

/// The USB input ring buffer. It's the same size and layout as the SSI RX buffer, and kept in step with it, so the same
/// offset into either is the same moment in time.
int32_t* getInputRingStart();
int32_t* getInputRingEnd();
/// The position in the USB input ring buffer matching a position in the SSI RX buffer
int32_t* toInputRing(int32_t const* rxPosition);

/// Queue audio to send to the host. Does nothing if the host isn't collecting it. txPosition is the SSI TX buffer
/// position just after where these frames went.
void sendToHost(std::span<const StereoSample> frames, int32_t const* txPosition);

/// The "USB to outputs" setting: mix audio from the host straight into the Deluge's outputs
extern bool directMonitoring;

} // namespace deluge::usb_audio
#endif
