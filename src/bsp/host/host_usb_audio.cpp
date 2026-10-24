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

/// Host-sim <libdeluge/usb_audio.h>: no USB host is ever connected. The input
/// ring still exists (silent), since the audio engine reads through it whenever
/// a track or the sampler is set to record from USB. [stub]

#include "definitions.h"
#include "libdeluge/usb_audio.h"

extern "C" {
bool usbAudioStartPeripheral(void) {
	return false;
}
bool usbAudioActive(void) {
	return false;
}
void usbAudioSetInterface(uint16_t interfaceNumber, uint16_t alternateSetting) {
	(void)interfaceNumber;
	(void)alternateSetting;
}
void usbAudioStopAll(void) {
}
uint16_t usbAudioHandleBrdy(uint16_t pipeBits) {
	(void)pipeBits;
	return 0;
}
void usbAudioHandleSof(void) {
}
}

namespace deluge::usb_audio {

namespace {
int32_t inputRing[SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS];
}

bool directMonitoring = true;
OutputSource outputSource = OutputSource::MAIN_MIX;

bool receivingFromHost() {
	return false;
}
bool sendingToHost() {
	return false;
}
void fillInputRing(uint32_t ringFrame, size_t numSamples, size_t txQueued) {
	(void)ringFrame;
	(void)numSamples;
	(void)txQueued;
}
int32_t* getInputRingStart() {
	return inputRing;
}
int32_t* getInputRingEnd() {
	return &inputRing[SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS];
}
int32_t* toInputRing(uint32_t ringFrame) {
	return inputRing + ringFrame * NUM_MONO_INPUT_CHANNELS;
}
void sendToHost(std::span<const StereoSample> frames, uint32_t blockOffsetEnd) {
	(void)frames;
	(void)blockOffsetEnd;
}

} // namespace deluge::usb_audio
