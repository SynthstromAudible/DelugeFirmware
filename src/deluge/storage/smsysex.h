#include "definitions_cxx.hpp"
#include "io/midi/midi_device_manager.h"
#include "storage/storage_manager.h"

namespace smSysex {

const uint32_t MAX_PATH_NAME_LEN = 255;

void sysexReceived(MIDIDevice* device, uint8_t* data, int32_t len);
void sendMemoryBlock(MIDIDevice* device, Deserializer &reader);
void getDirEntries(MIDIDevice* device, Deserializer &reader);

} // namespace smSysex
