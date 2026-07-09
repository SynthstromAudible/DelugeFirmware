#include "util/firmware_version.h"

// Mock definitions for the CppSpec build. It compiles storage/value_tree_deserializer.cpp, whose
// tryReadingFirmwareTagFromFile calls FirmwareVersion::parse and assigns song_firmware_version, but it
// does not link the real firmware_version.cpp / storage_manager.cpp. The specs don't exercise
// firmware-version handling (that is covered by the golden song round-trip), so trivial stubs suffice.

FirmwareVersion FirmwareVersion::parse(std::string_view /*string*/) {
	return FirmwareVersion::current();
}

FirmwareVersion song_firmware_version = FirmwareVersion::current();
