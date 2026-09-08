#include "definitions_cxx.hpp"
#include "io/midi/midi_device_manager.h"
#include "storage/storage_manager.h"

#include "io/file.hpp"
#include "storage/fs_status.h"
#include <optional>

struct FILdata;

namespace smSysex {

const uint32_t MAX_PATH_NAME_LEN = 255;

// Helper structure for file operation parameters
struct FileOpParams {
	std::string fromName{};
	std::string toName{};
	uint32_t date = 0;
	uint32_t time = 0;

	const char* getFromPath() const { return fromName.c_str(); }
	const char* getToPath() const { return toName.c_str(); }
	bool hasTimestamp() const { return date != 0 || time != 0; }
};

/// @brief Start the card work an open needs: a size for a read, an empty file for a write.
///
/// No handle is opened. What an open MEANS here is "the client may now name this file by fid", and
/// the transfer that follows is offset-addressed.
void beginOpenFIL(const char* fPath, bool forWrite);

/// @brief Finish an open whose card work has landed, recording the fid the client will use.
FILdata* finishOpenFIL(const char* fPath, bool forWrite, deluge::io::Status status,
                       deluge::storage::FsWireStatus* eCode);
deluge::storage::FsWireStatus closeFIL(FILdata* fd);
FILdata* findEmptyFIL();

void noteSessionIdUse(uint8_t msgId);
void noteFileIdUse(FILdata* fp);
FILdata* entryForFID(uint32_t fileId);
void startDirect(JsonSerializer& writer);
void startReply(JsonSerializer& writer, JsonDeserializer& reader);
void sendMsg(MIDICable& device, JsonSerializer& writer);

void sysexReceived(MIDICable& cable, uint8_t* data, int32_t len);
void handleNextSysEx();
/// @return true when the request is finished and its entry may be popped; false while a path
///         operation it started is still in flight, in which case it is re-entered next tick.
bool openFile(MIDICable& cable, JsonDeserializer& reader);
/// @return true when the request is finished and its entry may be popped; false while a path
///         operation it started is still in flight, in which case it is re-entered next tick.
bool closeFile(MIDICable& cable, JsonDeserializer& reader);
/// @return true when the request is finished and its entry may be popped; false while a path
///         operation it started is still in flight, in which case it is re-entered next tick.
bool readBlock(MIDICable& cable, JsonDeserializer& reader);
/// @return true when the request is finished and its entry may be popped; false while a path
///         operation it started is still in flight, in which case it is re-entered next tick.
bool writeBlock(MIDICable& cable, JsonDeserializer& reader);
/// @return true when finished; false while the directory it asked for is still opening.
bool getDirEntries(MIDICable& cable, JsonDeserializer& reader);

/// @brief Open `path` through the directory channel, or synchronously when it is not in use.
///
/// @return the directory once open; `nullopt` both while opening AND on failure — told apart by
///         `*outCode`, which stays `Ok` for the former.
std::optional<deluge::io::Directory> openDirectoryForSysex(const char* path, deluge::storage::FsWireStatus* outCode);
/// @return true when the request is finished and its entry may be popped; false while a path
///         operation it started is still in flight, in which case it is re-entered next tick.
bool deleteFile(MIDICable& cable, JsonDeserializer& reader);
/// @return true when the request is finished and its entry may be popped; false while a path
///         operation it started is still in flight, in which case it is re-entered next tick.
/// @return true when the request is finished and its entry may be popped; false while a path
///         operation it started is still in flight, in which case it is re-entered next tick.
bool createDirectory(MIDICable& cable, JsonDeserializer& reader);
/// @return true when the request is finished and its entry may be popped; false while a path
///         operation it started is still in flight, in which case it is re-entered next tick.
bool rename(MIDICable& cable, JsonDeserializer& reader);
/// @return true when the request is finished and its entry may be popped; false while a path
///         operation it started is still in flight, in which case it is re-entered next tick.
bool updateTime(MIDICable& cable, JsonDeserializer& reader);
/// @return true when finished; false while a copy it started is still in flight.
bool copyFile(MIDICable& cable, JsonDeserializer& reader);

/// @brief Copy `params.fromName` to `params.toName`, stamping the result if a timestamp was given.
///
/// The read/write loop that used to live here is gone: the copy is ONE request, and the loop runs in
/// Rust.
/// @return true when finished, with `*outCode` set; false while an operation is still in flight.
bool performFileCopy(const FileOpParams& params, deluge::storage::FsWireStatus* outCode);
/// @return true when finished; false while an operation it started is still in flight.
bool moveFile(MIDICable& cable, JsonDeserializer& reader);
void assignSession(MIDICable& cable, JsonDeserializer& reader);
void doPing(MIDICable& cable, JsonDeserializer& reader);
uint32_t decodeDataFromReader(JsonDeserializer& reader, uint8_t* dest, uint32_t destMax);

// Helper functions for file operations
bool parseFileOpParams(JsonDeserializer& reader, FileOpParams& params);
/// @return true when finished, with `*outCode` set; false while an operation is still in flight.

} // namespace smSysex
