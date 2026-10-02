#include "definitions_cxx.hpp"
#include "io/midi/midi_device_manager.h"
#include "runtime/app_loop.h"
#include "storage/storage_manager.h"

#include "io/file.hpp"
#include "storage/fs_status.h"
#include <optional>

struct FILdata;

namespace smSysex {

const uint32_t MAX_PATH_NAME_LEN = 255;

/// The SysEx job's id in `deluge::runtime::appLoop`, set by `registerJobs`. The job has no gap: it runs
/// when `sysexReceived` or `handleNextSysEx` itself asks for it, and on `DELUGE_WAKE_STORAGE` while a
/// request waits on a channel, so a message gets a run instead of waiting for the next poll.
extern deluge::runtime::JobId jobId;

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
/// @param fPath    The path to open.
/// @param forWrite Whether the open is for writing (vs. reading).
void beginOpenFIL(const char* fPath, bool forWrite);

/// @brief Finish an open whose card work has landed, recording the fid the client will use.
/// @param fPath    The path that was opened.
/// @param forWrite Whether the open was for writing (vs. reading).
/// @param status   The outcome of the card work `beginOpenFIL` started.
/// @param eCode    Set to the wire status corresponding to @p status.
/// @return The new FIL entry, or `nullptr` when @p status was a failure.
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
/// @brief Handle an sm-sysex `open` request, naming a file by fid for the reads or writes that follow.
/// @param cable  The MIDI cable the request arrived on, and the reply is sent on.
/// @param reader The request's decoded fields.
/// @return true when the request is finished and its entry may be popped; false while a path
///         operation it started is still in flight, in which case it is re-entered next tick.
bool openFile(MIDICable& cable, JsonDeserializer& reader);
/// @brief Handle an sm-sysex `close` request, releasing the fid an `open` request assigned.
/// @param cable  The MIDI cable the request arrived on, and the reply is sent on.
/// @param reader The request's decoded fields.
/// @return true when the request is finished and its entry may be popped; false while a path
///         operation it started is still in flight, in which case it is re-entered next tick.
bool closeFile(MIDICable& cable, JsonDeserializer& reader);
/// @brief Handle an sm-sysex request to read a block from an already-open file.
/// @param cable  The MIDI cable the request arrived on, and the reply is sent on.
/// @param reader The request's decoded fields.
/// @return true when the request is finished and its entry may be popped; false while a path
///         operation it started is still in flight, in which case it is re-entered next tick.
bool readBlock(MIDICable& cable, JsonDeserializer& reader);
/// @brief Handle an sm-sysex request to write a block to an already-open file.
/// @param cable  The MIDI cable the request arrived on, and the reply is sent on.
/// @param reader The request's decoded fields.
/// @return true when the request is finished and its entry may be popped; false while a path
///         operation it started is still in flight, in which case it is re-entered next tick.
bool writeBlock(MIDICable& cable, JsonDeserializer& reader);
/// @brief Handle an sm-sysex request to list a directory's entries.
/// @param cable  The MIDI cable the request arrived on, and the reply is sent on.
/// @param reader The request's decoded fields.
/// @return true when finished; false while the directory it asked for is still opening.
bool getDirEntries(MIDICable& cable, JsonDeserializer& reader);

/// @brief Open `path` through the directory channel, or synchronously when it is not in use.
///
/// @param path    The directory to open.
/// @param outCode Set to a real failure when one occurred; left `Ok` when the open is merely still in
///                flight.
/// @return the directory once open; `nullopt` both while opening AND on failure — told apart by
///         `*outCode`, which stays `Ok` for the former.
std::optional<deluge::io::Directory> openDirectoryForSysex(const char* path, deluge::storage::FsWireStatus* outCode);
/// @brief Handle an sm-sysex request to delete a file.
/// @param cable  The MIDI cable the request arrived on, and the reply is sent on.
/// @param reader The request's decoded fields.
/// @return true when the request is finished and its entry may be popped; false while a path
///         operation it started is still in flight, in which case it is re-entered next tick.
bool deleteFile(MIDICable& cable, JsonDeserializer& reader);
/// @brief Handle an sm-sysex request to create a directory.
/// @param cable  The MIDI cable the request arrived on, and the reply is sent on.
/// @param reader The request's decoded fields.
/// @return true when the request is finished and its entry may be popped; false while a path
///         operation it started is still in flight, in which case it is re-entered next tick.
bool createDirectory(MIDICable& cable, JsonDeserializer& reader);
/// @brief Handle an sm-sysex request to rename a file or directory.
/// @param cable  The MIDI cable the request arrived on, and the reply is sent on.
/// @param reader The request's decoded fields.
/// @return true when the request is finished and its entry may be popped; false while a path
///         operation it started is still in flight, in which case it is re-entered next tick.
bool rename(MIDICable& cable, JsonDeserializer& reader);
/// @brief Handle an sm-sysex request to update a file's timestamp.
/// @param cable  The MIDI cable the request arrived on, and the reply is sent on.
/// @param reader The request's decoded fields.
/// @return true when the request is finished and its entry may be popped; false while a path
///         operation it started is still in flight, in which case it is re-entered next tick.
bool updateTime(MIDICable& cable, JsonDeserializer& reader);
/// @brief Handle an sm-sysex request to copy a file, via @ref performFileCopy.
/// @param cable  The MIDI cable the request arrived on, and the reply is sent on.
/// @param reader The request's decoded fields.
/// @return true when finished; false while a copy it started is still in flight.
bool copyFile(MIDICable& cable, JsonDeserializer& reader);

/// @brief Copy `params.fromName` to `params.toName`, stamping the result if a timestamp was given.
///
/// The copy is ONE request; the read/write loop runs in Rust.
/// @param params  The source and destination paths, and an optional timestamp for the result.
/// @param outCode Set to the outcome once finished.
/// @return true when finished, with `*outCode` set; false while an operation is still in flight.
bool performFileCopy(const FileOpParams& params, deluge::storage::FsWireStatus* outCode);
/// @brief Handle an sm-sysex request to move a file: a rename, falling back to a copy-then-unlink
///        when source and destination do not share a filesystem.
/// @param cable  The MIDI cable the request arrived on, and the reply is sent on.
/// @param reader The request's decoded fields.
/// @return true when finished; false while an operation it started is still in flight.
bool moveFile(MIDICable& cable, JsonDeserializer& reader);
void assignSession(MIDICable& cable, JsonDeserializer& reader);
void doPing(MIDICable& cable, JsonDeserializer& reader);
uint32_t decodeDataFromReader(JsonDeserializer& reader, uint8_t* dest, uint32_t destMax);

// Helper functions for file operations
bool parseFileOpParams(JsonDeserializer& reader, FileOpParams& params);

} // namespace smSysex
