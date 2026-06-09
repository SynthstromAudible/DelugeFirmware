/*
 * Copyright (c) 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#pragma once

class MIDICable;
class MIDICableUSBHosted;

namespace launchpad_cable {

// Cached port-2 cable; slow rescan when disconnected (no per-frame USB walk).
MIDICableUSBHosted* getPort2();

bool isPort2(MIDICable& cable);

void registerPort2(MIDICableUSBHosted* cable);

} // namespace launchpad_cable
