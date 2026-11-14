#include "util/firmware_version.h"

// A spec binary that compiles the deserializers (storage/value_tree_deserializer.cpp, storage/file_reader.cpp) without
// storage_manager.cpp, which defines song_firmware_version, takes it from here.
FirmwareVersion song_firmware_version = current_firmware_version();
