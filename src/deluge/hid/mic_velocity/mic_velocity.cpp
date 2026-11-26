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

#include "hid/mic_velocity/mic_velocity.h"
#include "definitions_cxx.hpp"
#include "dsp/mic_velocity/echo_canceller.h"
#include "dsp/mic_velocity/thump_detector.h"
#include "hid/mic_velocity/mic_input.h"
#include "processing/engines/audio_engine.h"
#include "processing/stem_export/stem_export.h"
#include <algorithm>
#include <cmath>

namespace deluge::mic_velocity {

bool enabled = false;
dsp::mic_velocity::VelocityCurve curve{};

namespace {

using dsp::mic_velocity::Decimator;
using dsp::mic_velocity::EchoCanceller;
using dsp::mic_velocity::Thump;
using dsp::mic_velocity::ThumpDetector;
using input::Frame;

constexpr Frame ms(int32_t milliseconds) {
	return static_cast<Frame>(kSampleRate * milliseconds / 1000);
}

/// A press takes the loudest thump that started from this long before it arrived (pads pressed together share one).
/// Measured on a Deluge the thump starts a few ms before the press reaches the firmware, and a press is stamped when
/// the pad task next reads it, up to 5 ms later.
constexpr Frame kLookBack = ms(15);
/// The longest a press waits for its thump to settle
constexpr Frame kMaxWait = ms(4);
/// How long a press with no thump yet waits for one to start (5-10% start just after the press is handled)
constexpr Frame kNoThumpWait = ms(2);
/// The echo canceller doesn't learn for this long after a pad or button edge: the press may be thumping
constexpr Frame kFreezeAfterActivity = ms(60);
/// A press that's waiting is asked about again this often
constexpr Frame kRetryEvery = kSampleRate / 2000; // 0.5 ms

Decimator micDecimator;
Decimator sentDecimator;
EchoCanceller canceller;
ThumpDetector detector;
bool running = false;
Frame freezeUntil = 0;

Frame retryAt = 0;

bool before(Frame a, Frame b) {
	return static_cast<int32_t>(a - b) < 0;
}

PLACE_SDRAM_BSS dsp::mic_velocity::Calibration currentCalibration{}; // Read once a press, so out of internal RAM
bool haveCalibration = false;

/// The velocity a thump of this level (full scale 1) plays at, on this pad
uint8_t velocityFromLevel(int32_t x, int32_t y, float level) {
	float const levelDb = 20.f * std::log10(std::max(level, 1e-9f));
	return dsp::mic_velocity::velocityFor(currentCalibration, curve, x, y, levelDb);
}

/// Frames from the mic reader: decimate, take the speaker out (when it's on) and look for thumps
void consume(Frame first, std::span<int32_t const> mic, std::span<int16_t const> sent) {
	bool const cancel = AudioEngine::speakerOn;
	for (size_t i = 0; i < mic.size(); i++) {
		Frame const frame = first + i;
		auto m = micDecimator.push(mic[i]);
		auto s = sentDecimator.push(static_cast<int32_t>(sent[i]) << 16);
		if (!m || !s) {
			continue;
		}
		float x = *m;
		if (cancel) {
			bool const adapt = !detector.inThump() && !before(frame, freezeUntil);
			x = canceller.process(*m, *s, adapt);
		}
		detector.process(x, frame);
	}
}

} // namespace

bool active() {
	return running;
}

bool calibrated() {
	return haveCalibration;
}

dsp::mic_velocity::Calibration const& calibration() {
	return currentCalibration;
}

void setCalibration(dsp::mic_velocity::Calibration const& newCalibration) {
	currentCalibration = newCalibration;
	haveCalibration = true;
	update();
}

void update() {
	bool const shouldRun = enabled && calibrated() && !AudioEngine::micPluggedIn && !AudioEngine::lineInPluggedIn
	                       && !stemExport.processStarted;
	if (shouldRun == running) {
		return;
	}
	running = shouldRun;
	if (running) {
		micDecimator.reset();
		sentDecimator.reset();
		canceller.reset();
		detector.reset();
		input::setConsumer(consume);
		freezeUntil = input::now();
	}
	else {
		input::setConsumer(nullptr);
	}
}

void noteActivity() {
	if (running) {
		freezeUntil = input::now() + kFreezeAfterActivity;
	}
}

uint32_t now() {
	return input::now();
}

PressVelocity velocityForPress(int32_t x, int32_t y, uint32_t arrived) {
	if (!running) {
		return {.wait = false, .velocity = USE_DEFAULT_VELOCITY};
	}
	input::service(); // Right up to now
	Frame const now = input::now();
	Frame const waited = now - arrived;

	Thump const* loudest = nullptr;
	for (int32_t i = 0; Thump const* t = detector.recent(i); i++) {
		if (!before(t->onset, arrived - kLookBack) && !before(now, t->onset)
		    && (loudest == nullptr || t->level > loudest->level)) {
			loudest = t;
		}
	}

	float level;
	if (loudest != nullptr) {
		if (!loudest->settled && before(waited, kMaxWait)) {
			retryAt = now + kRetryEvery;
			return {.wait = true, .velocity = 0};
		}
		level = loudest->level;
	}
	else {
		if (before(waited, kNoThumpWait)) {
			retryAt = now + kRetryEvery;
			return {.wait = true, .velocity = 0};
		}
		// No thump over the noise: the press was no louder than the noise
		level = detector.noiseFloor();
	}
	return {.wait = false, .velocity = velocityFromLevel(x, y, level)};
}

bool retryDue() {
	return !before(input::now(), retryAt);
}

} // namespace deluge::mic_velocity
