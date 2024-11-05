#include "definitions_cxx.hpp"
#include "io/midi/midi_device_manager.h"
#include "storage/storage_manager.h"

namespace smSysex {

const uint32_t MAX_PATH_NAME_LEN = 255;

int openFIL(const char* fPath, bool forWrite, uint32_t* fsize, FRESULT* eCode);
FRESULT closeFIL(int fx);
int32_t findEmptyFIL();

void startDirect(JsonSerializer& writer);
void startReply(JsonSerializer& writer, JsonDeserializer& reader);
void sendMsg(MIDIDevice* device, JsonSerializer& writer);

void sysexReceived(MIDIDevice* device, uint8_t* data, int32_t len);
void handleNextSysEx();
void openFile(MIDIDevice* device, JsonDeserializer& reader);
void closeFile(MIDIDevice* device, JsonDeserializer& reader);
void readBlock(MIDIDevice* device, JsonDeserializer& reader);
void writeBlock(MIDIDevice* device, JsonDeserializer& reader);
void getDirEntries(MIDIDevice* device, JsonDeserializer& reader);
void deleteFile(MIDIDevice* device, JsonDeserializer& reader);
void createDirectory(MIDIDevice* device, JsonDeserializer& reader);
FRESULT createPathDirectories(String& path, uint32_t date, uint32_t time);
void rename(MIDIDevice* device, JsonDeserializer& reader);
void updateTime(MIDIDevice* device, JsonDeserializer& reader);
void doPing(MIDIDevice* device, JsonDeserializer& reader);
uint32_t decodeDataFromReader(JsonDeserializer& reader, uint8_t* dest, uint32_t destMax);
} // namespace smSysex
