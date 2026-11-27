#pragma once

/// @file
/// @brief The firmware's error codes: what a load, a save, a listing or a card operation answers when it fails or
///        defers.
///
/// The numbers cross the libdeluge ABI as a byte (`libdeluge/audio_load.h`), and the Rust loader keeps its own copy of
/// them, so an enumerator is only ever appended. `storage/audio/sample_build.cpp` pins each number with a
/// `static_assert`.

#include <cstddef>
#include <cstdint>
#include <utility>

namespace deluge {

/// @brief Why an operation failed or deferred. `NONE` is success.
enum class Error : int32_t {
	NONE,                                          ///< success
	INSUFFICIENT_RAM,                              ///< an allocation failed
	UNSPECIFIED,                                   ///< a failure with no more specific code
	SD_CARD,                                       ///< the card failed an operation
	NO_FURTHER_PRESETS,                            ///< a preset walk reached its end
	FILE_CORRUPTED,                                ///< a file's contents do not parse
	FILE_UNREADABLE,                               ///< a file could not be read
	FILE_UNSUPPORTED,                              ///< a file is of a kind or format the firmware does not load
	FILE_FIRMWARE_VERSION_TOO_NEW,                 ///< a file was saved by a newer firmware
	RESULT_TAG_UNUSED,                             ///< not produced; holds its number
	FOLDER_DOESNT_EXIST,                           ///< a folder named by a path is missing
	WRITE_PROTECTED,                               ///< the card is write-protected
	BUG,                                           ///< an internal inconsistency
	WRITE_FAIL,                                    ///< a write to the card failed
	FILE_TOO_BIG,                                  ///< a file is larger than the firmware can hold
	PRESET_IN_USE,                                 ///< a preset is in use and cannot be replaced
	NO_FURTHER_FILES_THIS_DIRECTION,               ///< a file walk reached its end
	FILE_ALREADY_EXISTS,                           ///< a file to be created exists
	FILE_NOT_FOUND,                                ///< a file named by a path is missing
	ABORTED_BY_USER,                               ///< not produced; holds the numbers after it
	MAX_FILE_SIZE_REACHED,                         ///< a file reached the largest size it may have
	SD_CARD_FULL,                                  ///< the card has no free space
	FILE_NOT_LOADABLE_AS_WAVETABLE,                ///< a sample cannot be read as a wavetable
	FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO, ///< a stereo sample cannot be read as a wavetable
	NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP,          ///< a folder walk is at the card's root
	NO_ERROR_BUT_GET_OUT,                          ///< not a failure: the caller leaves what it was doing
	INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE,     ///< a folder has more entries than memory holds
	SD_CARD_NOT_PRESENT,                           ///< no card is inserted
	SD_CARD_NO_FILESYSTEM,                         ///< the card has no filesystem the firmware reads
	INVALID_PATTERN_VERSION,                       ///< a pattern file's version is not one the firmware reads
	OUT_OF_BUFFER_SPACE,                           ///< a buffer filled before its contents ended
	INVALID_SYSEX_FORMAT,                          ///< a SysEx message is not in the expected format
	POS_PAST_STRING,                               ///< a position lies past a string's end
	FILE_NOT_SAVED,                                ///< a save did not complete
	TOO_MANY_OPEN_STREAMS,                         ///< every audio stream is in use
	/// @brief Not a failure: the async sample loader has been asked for this file and has not finished yet.
	///
	/// A caller that gets this got no audio file this time and asks again on a later tick. Only the opt-in async path
	/// of `AudioFileManager::getAudioFileFromFilename` (`libdeluge/audio_load.h`) produces it.
	///
	/// @warning Must not re-request, report a load failure, or write anything on the strength of this result.
	LOAD_IN_PROGRESS,
	/// @brief Not a failure: a folder listing needs a directory the async directory channel has not opened yet.
	///
	/// The caller got no listing this time and lets the browser's per-tick surface run the attempt again. Only
	/// produced after boot, where every directory open goes through `libdeluge/file_directory.h`. It is not
	/// `LOAD_IN_PROGRESS` because a listing can itself reach the sample loader (`addInstrumentsToFileItems`), and the
	/// catch site must tell the two deferrals apart.
	///
	/// @warning Must not re-request, report a failure, or commit anything on the strength of this result.
	LISTING_DEFERRED,
};

/// @brief How many `Error` values there are: one past the last.
inline constexpr std::size_t kErrorCount = std::to_underlying(Error::LISTING_DEFERRED) + 1;

} // namespace deluge
