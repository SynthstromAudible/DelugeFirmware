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
//
// Multitrack is an optional configuration in place of stereo, chosen at power-on: 8 channels of 16-bit to the host (the
// usual stereo output, then three stereo track buses), and 4 of 16-bit from it (two stereo inputs). Its rings are
// allocated then, and only if it's chosen, as 16-bit frames in exactly the layout of the USB packets - so the DMA moves
// packets straight between them and the FIFOs, with no copying in the interrupts, and NEON converts to and from the
// Deluge's q31 samples in the audio routine. With it off, it costs nothing beyond a few never-taken branches.

#ifdef __cplusplus
extern "C" {
#endif

/// Called as USB starts up as a peripheral. Takes the user's USB audio setting (FlashStorage::usbAudioMode), which then
/// holds until the next start - it decides which configuration descriptor the host gets, which it returns
/// (USB_PAUDIO_CONFIG_*). The first time multitrack is chosen, its buffers are allocated - if they can't be, it falls
/// back to stereo. That's at power-on: it's the only time USB starts as a peripheral
uint8_t usbAudioStartPeripheral(void);
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
/// From the USB task, with usbLock held. Reconnects us if the host's given up sending after a stall (see
/// usb_audio.cpp)
void usbAudioServiceTask(void);

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

/// The stereo inputs from the host: one, or with multitrack two - its channels 1/2 and 3/4
constexpr uint8_t kMaxInputs = 2;
/// How many there are this session
uint8_t numInputs();
/// The input that asking for input actually gets: itself, or the first if there's no such input this session - so
/// something set to read the second (in a song from a multitrack session, say) reads the only one there is
uint8_t resolveInput(uint8_t input);

/// A USB input's ring buffer. It's the same size and layout as the SSI RX buffer, and kept in step with it, so the
/// same offset into either is the same moment in time.
int32_t* getInputRingStart(uint8_t input = 0);
int32_t* getInputRingEnd(uint8_t input = 0);
/// The position in a USB input's ring buffer matching a position in the SSI RX buffer
int32_t* toInputRing(int32_t const* rxPosition, uint8_t input = 0);

/// Queue audio to send to the host. Does nothing if the host isn't collecting it. txPosition is the SSI TX buffer
/// position just after where these frames went. renderOffset is how far into the render the first of them is - with
/// multitrack, the track buses' audio from there goes with them.
void sendToHost(std::span<const StereoSample> frames, int32_t const* txPosition, size_t renderOffset);

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

/// The USB audio setting (FlashStorage::usbAudioMode). The values are what's stored in flash
enum class Mode : uint8_t {
	STEREO = 0, // Stereo both ways, 24-bit
	OFF = 1,    // USB MIDI only
	MULTITRACK = 2,
};
constexpr uint8_t kNumModes = 3;

/// Whether this USB session is multitrack
bool multitrackActive();

/*
 * Multitrack's track buses. Channels 1/2 to the host are the usual output (outputSource); 3/4, 5/6 and 7/8 are buses 1
 * to 3, which tracks can be routed to (Output::getUSBBus()). A routed track is rendered into its bus instead of the
 * main mix, at the same level - before the song's FX and volume. Its reverb send still goes to the Deluge's reverb, in
 * the main mix.
 */
constexpr uint8_t kNumTrackBuses = 3;

/// Whether tracks routed to buses should render into them this render. Only ever set while multitrack is active, and
/// not while exporting stems, which need every track
extern bool routingToTrackBuses;

/// Call at the start of each render, before the song's
void startRender(bool routeTracks);

/// A track bus's buffer for this render, bus being 1 to kNumTrackBuses, cleared the first time it's asked for in each
/// render. Only while routingToTrackBuses
std::span<StereoSample> trackBus(uint8_t bus, size_t numSamples);

} // namespace deluge::usb_audio
#endif
