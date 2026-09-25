/*
 * Copyright © 2024 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#pragma once

#include <cstdint>

namespace deluge::io::midi {

// Clip/song matching: omit outputDevice from XML → legacy channel+suffix match only.
static constexpr uint8_t kMIDIOutputDeviceMatchUnspecified = 255;

// Output device index used by MIDIInstrument / MIDIDrum / MidiEngine:
// 0 = all connected outputs (legacy behaviour)
// 1 = DIN
// 2 = USB device-mode cable 1 (computer) — offered in the Output Device menu
// 3 = USB device-mode cable 2 (MPE-oriented); offered only when the current MIDI track
//     is in an MPE zone so you can e.g. send MPE to DIN only and skip the computer
// 4+ = USB host-mode devices (hostedMIDIDevices[i] at index i + 4)

} // namespace deluge::io::midi
