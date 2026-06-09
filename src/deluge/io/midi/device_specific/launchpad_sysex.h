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

namespace launchpad_extension {
enum class ViewMode : uint8_t;
}

namespace launchpad_sysex {

void sendSetup(MIDICableUSBHosted* cable);

// Keep Launchpad in Programmer mode (e.g. after Session/Note button press).
void reassertProgrammerMode(MIDICableUSBHosted* cable);

// Force next LED sync to resend all pads (e.g. after view mode change).
void invalidateLedCache();

// Delta SysEx: only changed grid/scene/transport LEDs are sent (full state on connect via cache invalidation).
void sendLaunchpadLeds(MIDICableUSBHosted* cable, launchpad_extension::ViewMode viewMode,
                       RGB image[][kDisplayWidth + kSideBarWidth],
                       uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], RGB const sceneColours[8],
                       bool transportPlaying, bool transportRecording);

} // namespace launchpad_sysex
