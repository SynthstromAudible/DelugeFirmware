/*
 * Copyright (c) 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#include "io/midi/device_specific/launchpad_sysex.h"

#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/device_specific/launchpad_extension.h"
#include "io/midi/device_specific/launchpad_programmer_map.h"
#include "io/midi/device_specific/novation_launchpad_mk3.h"
#include "io/midi/midi_engine.h"
#include "util/functions.h"

#include <algorithm>

namespace launchpad_sysex {

namespace {

static constexpr int32_t kSysexBufferBytes = 420;
static constexpr int32_t kMaxLedEntriesPerSysex = 80;
static constexpr int32_t kLedCacheSize = 128;

uint8_t ledCacheR[kLedCacheSize];
uint8_t ledCacheG[kLedCacheSize];
uint8_t ledCacheB[kLedCacheSize];
bool ledCacheValid[kLedCacheSize];

void clearLedCache() {
	for (int32_t i = 0; i < kLedCacheSize; i++) {
		ledCacheValid[i] = false;
	}
}

void sendRaw(MIDICableUSBHosted* cable, uint8_t const* data, int32_t len) {
	if (len < 6 || data[0] != 0xF0 || data[len - 1] != 0xF7) {
		return;
	}

	for (int32_t attempt = 0; attempt < 16; attempt++) {
		if (len <= cable->sendBufferSpace()) {
			if (cable->sendSysex(data, len) == Error::NONE) {
				return;
			}
		}
		midiEngine.flushMIDI();
	}
}

uint8_t deviceIdFor(MIDICableUSBHosted* cable) {
	return novation_launchpad_mk3::sysexDeviceId(cable->productId);
}

void scaleRgb(uint8_t r, uint8_t g, uint8_t b, uint8_t& outR, uint8_t& outG, uint8_t& outB) {
	uint8_t peak = r;
	if (g > peak) {
		peak = g;
	}
	if (b > peak) {
		peak = b;
	}

	if (peak == 0) {
		outR = outG = outB = 0;
		return;
	}

	// Inactive/dim clips on Deluge peak around 10/255 — linear mapping is nearly invisible on MK3.
	if (peak <= 48) {
		constexpr uint8_t kDimTargetPeak = 26; // ~20% of Launchpad max; active clips still hit 127
		uint16_t scale = (static_cast<uint16_t>(kDimTargetPeak) * 255) / peak;
		outR = static_cast<uint8_t>(std::min<uint16_t>(127, (static_cast<uint16_t>(r) * scale) / 255));
		outG = static_cast<uint8_t>(std::min<uint16_t>(127, (static_cast<uint16_t>(g) * scale) / 255));
		outB = static_cast<uint8_t>(std::min<uint16_t>(127, (static_cast<uint16_t>(b) * scale) / 255));
		return;
	}

	uint16_t scale = (static_cast<uint16_t>(127) * 255) / peak;
	outR = static_cast<uint8_t>(std::min<uint16_t>(127, (static_cast<uint16_t>(r) * scale) / 255));
	outG = static_cast<uint8_t>(std::min<uint16_t>(127, (static_cast<uint16_t>(g) * scale) / 255));
	outB = static_cast<uint8_t>(std::min<uint16_t>(127, (static_cast<uint16_t>(b) * scale) / 255));
}

int32_t appendLedPaletteOff(uint8_t* buffer, int32_t pos, uint8_t ledIndex) {
	buffer[pos++] = 0;
	buffer[pos++] = ledIndex;
	buffer[pos++] = 0;
	return pos;
}

int32_t appendLedRgb(uint8_t* buffer, int32_t pos, uint8_t ledIndex, uint8_t r, uint8_t g, uint8_t b) {
	buffer[pos++] = 3;
	buffer[pos++] = ledIndex;
	buffer[pos++] = r;
	buffer[pos++] = g;
	buffer[pos++] = b;
	return pos;
}

int32_t appendSysexHeader(uint8_t* buffer, int32_t pos, uint8_t deviceId) {
	buffer[pos++] = 0xF0;
	buffer[pos++] = 0x00;
	buffer[pos++] = 0x20;
	buffer[pos++] = 0x29;
	buffer[pos++] = 0x02;
	buffer[pos++] = deviceId;
	buffer[pos++] = 0x03;
	return pos;
}

void sendLedFeedbackOff(MIDICableUSBHosted* cable) {
	uint8_t deviceId = deviceIdFor(cable);
	uint8_t buffer[16];
	int32_t pos = 0;
	buffer[pos++] = 0xF0;
	buffer[pos++] = 0x00;
	buffer[pos++] = 0x20;
	buffer[pos++] = 0x29;
	buffer[pos++] = 0x02;
	buffer[pos++] = deviceId;
	buffer[pos++] = 0x0A;
	buffer[pos++] = 0x00;
	buffer[pos++] = 0x00;
	buffer[pos++] = 0xF7;
	sendRaw(cable, buffer, pos);
}

class DeltaBatch {
public:
	void begin(MIDICableUSBHosted* cable, uint8_t deviceId) {
		cable_ = cable;
		deviceId_ = deviceId;
		pos_ = appendSysexHeader(buffer_, 0, deviceId_);
		entryCount_ = 0;
	}

	void queueIfChanged(uint8_t ledIndex, uint8_t r, uint8_t g, uint8_t b, bool force = false) {
		if (ledIndex >= kLedCacheSize) {
			return;
		}

		if (!force && ledCacheValid[ledIndex] && ledCacheR[ledIndex] == r && ledCacheG[ledIndex] == g
		    && ledCacheB[ledIndex] == b) {
			return;
		}

		int32_t entryBytes = (r == 0 && g == 0 && b == 0) ? 3 : 5;
		if (pos_ + entryBytes + 1 > kSysexBufferBytes) {
			flush();
		}

		ledCacheR[ledIndex] = r;
		ledCacheG[ledIndex] = g;
		ledCacheB[ledIndex] = b;
		ledCacheValid[ledIndex] = true;

		if (r == 0 && g == 0 && b == 0) {
			pos_ = appendLedPaletteOff(buffer_, pos_, ledIndex);
		}
		else {
			pos_ = appendLedRgb(buffer_, pos_, ledIndex, r, g, b);
		}

		entryCount_++;
		if (entryCount_ >= kMaxLedEntriesPerSysex) {
			flush();
		}
	}

	void flush() {
		if (entryCount_ == 0) {
			return;
		}

		buffer_[pos_++] = 0xF7;
		sendRaw(cable_, buffer_, pos_);
		pos_ = appendSysexHeader(buffer_, 0, deviceId_);
		entryCount_ = 0;
	}

private:
	MIDICableUSBHosted* cable_ = nullptr;
	uint8_t deviceId_ = 0;
	uint8_t buffer_[kSysexBufferBytes];
	int32_t pos_ = 0;
	int32_t entryCount_ = 0;
};

void sendAllProgrammerLedsOff(MIDICableUSBHosted* cable) {
	uint8_t deviceId = deviceIdFor(cable);
	DeltaBatch batch;
	batch.begin(cable, deviceId);

	for (int32_t lpY = 0; lpY < 8; lpY++) {
		for (int32_t lpX = 0; lpX < 8; lpX++) {
			batch.queueIfChanged(launchpad_programmer_map::gridLedIndex(lpX, lpY), 0, 0, 0);
		}
	}

	for (int32_t y = 0; y < 8; y++) {
		batch.queueIfChanged(launchpad_programmer_map::sceneLedIndexForDelugeRow(y), 0, 0, 0);
	}

	uint8_t const* indices = launchpad_programmer_map::nonGridLedIndices();
	for (uint8_t i = 0; i < launchpad_programmer_map::kNonGridLedIndexCount; i++) {
		batch.queueIfChanged(indices[i], 0, 0, 0);
	}

	batch.flush();
	midiEngine.flushMIDI();
}

} // namespace

void reassertProgrammerMode(MIDICableUSBHosted* cable) {
	uint8_t deviceId = deviceIdFor(cable);
	uint8_t programmerMode[] = {0xF0, 0x00, 0x20, 0x29, 0x02, deviceId, 0x0E, 0x01, 0xF7};
	sendRaw(cable, programmerMode, sizeof programmerMode);
	// Session/Note buttons can re-enable MIDI LED feedback — any Note/CC then lights random pads.
	sendLedFeedbackOff(cable);
	midiEngine.flushMIDI();
}

void invalidateLedCache() {
	clearLedCache();
}

void sendSetup(MIDICableUSBHosted* cable) {
	uint8_t deviceId = deviceIdFor(cable);

	clearLedCache();

	uint8_t programmerMode[] = {0xF0, 0x00, 0x20, 0x29, 0x02, deviceId, 0x0E, 0x01, 0xF7};
	sendRaw(cable, programmerMode, sizeof programmerMode);
	midiEngine.flushMIDI();

	uint8_t maxBrightness[] = {0xF0, 0x00, 0x20, 0x29, 0x02, deviceId, 0x08, 0x7F, 0xF7};
	sendRaw(cable, maxBrightness, sizeof maxBrightness);
	midiEngine.flushMIDI();

	sendLedFeedbackOff(cable);
	midiEngine.flushMIDI();

	// Wipe every Programmer LED (64 grid + scene + top row) so stale bytes cannot leave ghosts.
	sendAllProgrammerLedsOff(cable);
}

void sendLaunchpadLeds(MIDICableUSBHosted* cable, launchpad_extension::ViewMode viewMode,
                       RGB image[][kDisplayWidth + kSideBarWidth],
                       uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], RGB const sceneColours[8],
                       bool transportPlaying, bool transportRecording, bool forceFullRefresh) {
	uint8_t deviceId = deviceIdFor(cable);
	DeltaBatch batch;
	batch.begin(cable, deviceId);

	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			uint8_t ledIndex = launchpad_programmer_map::gridLedIndexFromDeluge(x, y);
			uint8_t outR = 0;
			uint8_t outG = 0;
			uint8_t outB = 0;

			if (occupancyMask[y][x]) {
				scaleRgb(image[y][x].r, image[y][x].g, image[y][x].b, outR, outG, outB);
			}

			batch.queueIfChanged(ledIndex, outR, outG, outB, forceFullRefresh);
		}
	}

	if (viewMode == launchpad_extension::ViewMode::Session || viewMode == launchpad_extension::ViewMode::Note) {
		for (int32_t y = 0; y < 8; y++) {
			uint8_t outR;
			uint8_t outG;
			uint8_t outB;
			scaleRgb(sceneColours[y].r, sceneColours[y].g, sceneColours[y].b, outR, outG, outB);
			batch.queueIfChanged(launchpad_programmer_map::sceneLedIndexForDelugeRow(y), outR, outG, outB,
			                     forceFullRefresh);
		}
	}
	else {
		for (int32_t y = 0; y < 8; y++) {
			batch.queueIfChanged(launchpad_programmer_map::sceneLedIndexForDelugeRow(y), 0, 0, 0, forceFullRefresh);
		}
	}

	uint8_t const* fixedOffIndices = launchpad_programmer_map::fixedOffLedIndices();
	for (uint8_t i = 0; i < launchpad_programmer_map::kFixedOffLedIndexCount; i++) {
		batch.queueIfChanged(fixedOffIndices[i], 0, 0, 0, forceFullRefresh);
	}

	if (transportPlaying) {
		batch.queueIfChanged(launchpad_programmer_map::cc::kCustom, 0, 127, 0, forceFullRefresh);
	}
	else {
		batch.queueIfChanged(launchpad_programmer_map::cc::kCustom, 0, 0, 0, forceFullRefresh);
	}

	if (transportRecording) {
		batch.queueIfChanged(launchpad_programmer_map::cc::kCaptureMidi, 127, 0, 0, forceFullRefresh);
	}
	else {
		batch.queueIfChanged(launchpad_programmer_map::cc::kCaptureMidi, 0, 0, 0, forceFullRefresh);
	}

	if (viewMode == launchpad_extension::ViewMode::Session) {
		batch.queueIfChanged(launchpad_programmer_map::cc::kSession, 0, 127, 0, forceFullRefresh);
		batch.queueIfChanged(launchpad_programmer_map::cc::kNote, 0, 0, 0, forceFullRefresh);
	}
	else {
		batch.queueIfChanged(launchpad_programmer_map::cc::kSession, 0, 0, 0, forceFullRefresh);
		batch.queueIfChanged(launchpad_programmer_map::cc::kNote, 0, 127, 0, forceFullRefresh);
	}

	batch.flush();
	midiEngine.flushMIDI();
}

} // namespace launchpad_sysex
