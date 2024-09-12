#include "definitions_cxx.hpp"
#include "io/midi/midi_device_manager.h"
#include "storage/storage_manager.h"

namespace smSysex {

const uint32_t MAX_PATH_NAME_LEN = 255;

void sysexReceived(MIDIDevice* device, uint8_t* data, int32_t len);
void sendMemoryBlock(MIDIDevice* device, JsonDeserializer& reader);
void putMemoryBlock(MIDIDevice* device, JsonDeserializer& reader);
void getDirEntries(MIDIDevice* device, JsonDeserializer& reader);
uint32_t decodeDataFromReader(JsonDeserializer& reader, uint8_t* dest, uint32_t destMax);
} // namespace smSysex
