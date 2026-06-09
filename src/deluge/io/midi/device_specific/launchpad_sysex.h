/*
 * Copyright (c) 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "gui/colour/rgb.h"

#include <cstdint>

class MIDICableUSBHosted;

namespace launchpad_sysex {

void sendSetup(MIDICableUSBHosted* cable);

// Delta SysEx: only changed grid/scene/transport LEDs are sent (full state on connect via cache invalidation).
void sendSessionGrid(MIDICableUSBHosted* cable, RGB image[][kDisplayWidth + kSideBarWidth],
                     uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], RGB const sceneColours[8],
                     bool transportPlaying, bool transportRecording);

} // namespace launchpad_sysex
