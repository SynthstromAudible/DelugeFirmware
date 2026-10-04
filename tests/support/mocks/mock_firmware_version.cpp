#include "util/firmware_version.h"

// The CppSpec build compiles storage/value_tree_deserializer.cpp, which records the version of the document it reads
// in song_firmware_version, but it does not link storage_manager.cpp, which defines it.
FirmwareVersion song_firmware_version = current_firmware_version();
