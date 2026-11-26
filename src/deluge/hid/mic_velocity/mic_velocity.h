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

#include "dsp/mic_velocity/velocity_map.h"
#include <cstdint>

/// Mic velocity: a pad press's velocity from the thump it makes in the internal mic.
///
/// While active, the mic (and, with the speaker on, what's sent to the speaker) runs continuously through the echo
/// canceller and thump detector (dsp/mic_velocity). A press that plays notes at its velocity (keyboard view's pads,
/// audition pads) is stamped with the frame it arrived at and queued (deluge.cpp); everything else goes on as usual.
/// When it reaches the front its thump is usually already there: it starts a couple of ms before the press arrives.
/// It then goes to the UI with the velocity it measured, so everything downstream (sounds, MIDI and CV out,
/// recording, the arpeggiator) gets it as if the pads were velocity sensitive. If the thump is still rising, the press
/// (and its release, if that's arrived too) waits for it, for up to 4 ms.
namespace deluge::mic_velocity {

/// Whether presses are being measured: enabled, calibrated, the internal mic live (nothing in the mic or line-in
/// jacks), and not exporting stems
[[nodiscard]] bool active();

/// Starts or stops measuring to match active(). Called every 100 ms with the jack detection, and when the settings
/// change.
void update();

/// Any pad or button edge: the echo canceller stops learning for a moment, as the press may be thumping
void noteActivity();

struct PressVelocity {
	bool wait;        ///< The thump is still rising: hand the press back and ask again shortly
	uint8_t velocity; ///< Otherwise, the velocity to send the press with (1-127)
};

/// The mic's frame clock now, for stamping a press as it arrives
[[nodiscard]] uint32_t now();

/// A pad press that arrived at frame arrived (from now()). Call it again for the same press after a wait.
PressVelocity velocityForPress(int32_t x, int32_t y, uint32_t arrived);

/// After a wait: whether it's time to ask about the press again (0.5 ms on). Cheap enough for a scheduler condition.
[[nodiscard]] bool retryDue();

/// Enabled in the settings
extern bool enabled;

/// How tap strength maps onto velocity (the settings)
extern dsp::mic_velocity::VelocityCurve curve;

/// The calibration presses are measured against, once there is one
[[nodiscard]] bool calibrated();
[[nodiscard]] dsp::mic_velocity::Calibration const& calibration();
void setCalibration(dsp::mic_velocity::Calibration const& newCalibration);

} // namespace deluge::mic_velocity
