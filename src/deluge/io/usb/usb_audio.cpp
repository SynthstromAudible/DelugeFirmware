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

#include "io/usb/usb_audio.h"
#include "definitions.h"
#include "libdeluge/usb_audio.h"
#include "storage/flash_storage.h"
#include <algorithm>
#include <cstring>

static_assert(sizeof(StereoSample) == sizeof(DelugeStereoSample));

namespace deluge::usb_audio {

bool directMonitoring = true;
OutputSource outputSource = OutputSource::MAIN_MIX;

namespace {

constexpr size_t kRingFrames = SSI_RX_BUFFER_NUM_SAMPLES;

PLACE_SDRAM_BSS int32_t inputRing[kRingFrames * NUM_MONO_INPUT_CHANNELS];

// How many frames in a row have been filled with silence, up to a whole ring - after which it's all silence, and
// there's nothing more to clear until the host starts sending again
size_t inputRingSilentFrames = 0;

} // namespace

void startDevice() {
	deluge_usb_device_start(FlashStorage::usbAudioEnabled);
}

bool present() {
	return deluge_usb_audio_present();
}

bool receivingFromHost() {
	return deluge_usb_audio_receiving();
}

bool sendingToHost() {
	return deluge_usb_audio_sending();
}

int32_t* getInputRingStart() {
	return inputRing;
}

int32_t* getInputRingEnd() {
	return &inputRing[kRingFrames * NUM_MONO_INPUT_CHANNELS];
}

void fillInputRing(int32_t* at, size_t numSamples) {
	bool const receiving = receivingFromHost();
	if (!receiving && inputRingSilentFrames >= kRingFrames) {
		return;
	}
	while (numSamples) {
		auto const startFrame = static_cast<size_t>(at - inputRing) / NUM_MONO_INPUT_CHANNELS;
		size_t numThisTime = std::min(numSamples, kRingFrames - startFrame);
		if (receiving) {
			deluge_usb_audio_read(reinterpret_cast<DelugeStereoSample*>(at), numThisTime);
			inputRingSilentFrames = 0;
		}
		else {
			std::memset(at, 0, numThisTime * sizeof(StereoSample));
			inputRingSilentFrames += numThisTime;
		}
		numSamples -= numThisTime;
		at = (startFrame + numThisTime == kRingFrames) ? inputRing : at + numThisTime * NUM_MONO_INPUT_CHANNELS;
	}
}

void sendToHost(std::span<const StereoSample> frames) {
	deluge_usb_audio_write(reinterpret_cast<const DelugeStereoSample*>(frames.data()), frames.size());
}

} // namespace deluge::usb_audio
