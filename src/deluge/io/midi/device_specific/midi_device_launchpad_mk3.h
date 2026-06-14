/*
 * Copyright (c) 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#pragma once

#include "io/midi/cable_types/usb_hosted.h"

class MIDIDeviceLaunchpadMK3 final : public MIDICableUSBHosted {
public:
	void hookOnConnected() override;
	void hookOnWriteHostedDeviceToFile() override {}
};
