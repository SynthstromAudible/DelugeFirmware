#include "definitions_cxx.hpp"
#include "io/midi/midi_device_manager.h"
#include "storage/storage_manager.h"

namespace smSysex {

const uint32_t MAX_PATH_NAME_LEN = 255;

int openFIL(const char* fPath, bool forWrite, FRESULT* eCode);
FRESULT closeFIL(int fx);
int32_t findEmptyFIL();

void startReply(JsonSerializer& writer);
void sendReply(MIDIDevice* device, JsonSerializer& writer);

void sysexReceived(MIDIDevice* device, uint8_t* data, int32_t len);
void openFile(MIDIDevice* device, JsonDeserializer& reader);
void closeFile(MIDIDevice* device, JsonDeserializer& reader);
void readBlock(MIDIDevice* device, JsonDeserializer& reader);
void writeBlock(MIDIDevice* device, JsonDeserializer& reader);
void getDirEntries(MIDIDevice* device, JsonDeserializer& reader);
uint32_t decodeDataFromReader(JsonDeserializer& reader, uint8_t* dest, uint32_t destMax);
} // namespace smSysex
