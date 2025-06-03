#include "definitions_cxx.hpp"
#include "io/midi/midi_device_manager.h"
#include "storage/storage_manager.h"

struct FILdata;

namespace smSysex {

const uint32_t MAX_PATH_NAME_LEN = 255;

// Helper structure for file operation parameters
struct FileOpParams {
	String fromName;
	String toName;
	uint32_t date = 0;
	uint32_t time = 0;

	const char* getFromPath() const { return fromName.get(); }
	const char* getToPath() const { return toName.get(); }
	const TCHAR* getFromTC() const { return (const TCHAR*)fromName.get(); }
	const TCHAR* getToTC() const { return (const TCHAR*)toName.get(); }
	bool hasTimestamp() const { return date != 0 || time != 0; }
};

FILdata* openFIL(const char* fPath, int forWrite, uint32_t* fsize, FRESULT* eCode);
FRESULT closeFIL(FILdata* fd);
FILdata* findEmptyFIL();

void noteSessionIdUse(uint8_t msgId);
void noteFileIdUse(FILdata* fp);
FILdata* entryForFID(uint32_t fileId);
void startDirect(JsonSerializer& writer);
void startReply(JsonSerializer& writer, JsonDeserializer& reader);
void sendMsg(MIDICable& device, JsonSerializer& writer);

void sysexReceived(MIDICable& cable, uint8_t* data, int32_t len);
void handleNextSysEx();
void openFile(MIDICable& cable, JsonDeserializer& reader);
void closeFile(MIDICable& cable, JsonDeserializer& reader);
void readBlock(MIDICable& cable, JsonDeserializer& reader);
void writeBlock(MIDICable& cable, JsonDeserializer& reader);
void getDirEntries(MIDICable& cable, JsonDeserializer& reader);
void deleteFile(MIDICable& cable, JsonDeserializer& reader);
void createDirectory(MIDICable& cable, JsonDeserializer& reader);
FRESULT createPathDirectories(String& path, uint32_t date, uint32_t time);
void rename(MIDICable& cable, JsonDeserializer& reader);
void updateTime(MIDICable& cable, JsonDeserializer& reader);
void copyFile(MIDICable& cable, JsonDeserializer& reader);
void moveFile(MIDICable& cable, JsonDeserializer& reader);
void assignSession(MIDICable& cable, JsonDeserializer& reader);
void doPing(MIDICable& cable, JsonDeserializer& reader);
uint32_t decodeDataFromReader(JsonDeserializer& reader, uint8_t* dest, uint32_t destMax);

// Helper functions for file operations
bool parseFileOpParams(JsonDeserializer& reader, FileOpParams& params);
FRESULT performFileCopy(const FileOpParams& params);
void setFileTimestamp(const TCHAR* path, uint32_t date, uint32_t time);

} // namespace smSysex
