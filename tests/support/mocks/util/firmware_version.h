#pragma once

// The spec build's stand-in for the app's generated util/firmware_version.h: the spec tree is not configured with a
// project version, so this build reports 0.0.0.
#include "serialization/firmware_version.h"

constexpr FirmwareVersion current_firmware_version() {
	return FirmwareVersion::community({0, 0, 0});
}
