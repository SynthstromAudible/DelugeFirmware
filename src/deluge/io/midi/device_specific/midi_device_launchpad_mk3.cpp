/*
 * Copyright (c) 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#include "io/midi/device_specific/midi_device_launchpad_mk3.h"

#include "io/midi/device_specific/launchpad_cable.h"
#include "io/midi/device_specific/launchpad_extension.h"
#include "io/midi/device_specific/launchpad_sysex.h"

void MIDIDeviceLaunchpadMK3::hookOnConnected() {
	launchpad_cable::registerPort2(this);
	launchpad_sysex::sendSetup(this);
	launchpad_extension::onDeviceConnected();
}
