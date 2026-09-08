#pragma once

#include "io/file.hpp"
#include <cstdint>
#include <expected>

namespace deluge::storage {

/// @brief A three-state answer to "does this path exist?", suitable for exhaustive switching.
///
/// @note Unknown is never absence: callers must never take the "absent" branch on `Undeterminable`.
/// @warning Switch on this enum exhaustively, with no `default:` label. That omission is
///          deliberate — it lets `-Wswitch` catch an unhandled enumerator, so never add one.
enum class Presence {
	Present,        ///< the path is there
	Absent,         ///< the path is known not to be there (Status::NOT_FOUND)
	Undeterminable, ///< existence could not be determined (e.g. Status::BUSY off-owner)
};

/// @brief Classify an existence result for exhaustive handling.
/// @param present The result of `StorageManager::fileExists` / `deluge::io::presence_from_open`.
/// @return `Present` if the path is there, `Absent` if it is known not to be, `Undeterminable` if
///         existence could not be established.
Presence presence_of(const std::expected<bool, deluge::io::Status>& present);

/// @brief What a settings-file bootstrap path should do about a settings file.
enum class Bootstrap {
	Load,            ///< the file is there: parse it
	WriteDefaults,   ///< the file is known absent: it is safe to create and write defaults
	UseInMemoryOnly, ///< existence unknown: use in-memory defaults and WRITE NOTHING
};

/// @brief The one rule all settings-bootstrap readers share.
/// @param present The result of the existence check on the settings file.
/// @return `UseInMemoryOnly` whenever existence is undeterminable — never write over a file whose
///         absence is unconfirmed.
Bootstrap bootstrap_action(const std::expected<bool, deluge::io::Status>& present);

/// @brief Whether a save may write to a path whose presence has been checked.
///
/// @note `Undeterminable` is REFUSED, never treated as absent — and confirming an overwrite does not
///       override that. On the storage owner the filesystem answers, so "could not tell" means the
///       card genuinely did not respond: a fault, not a routine condition. Taking the absent branch
///       there would overwrite a file the user still has.
/// @warning Switch on this enum exhaustively, with no `default:` label, for the same reason given on
///          `Presence` above.
enum class SaveAction : uint8_t {
	Write,    ///< the path is free to write (nothing there, or the user confirmed an overwrite)
	Occupied, ///< something is there and the user has not confirmed
	Refuse,   ///< presence could not be determined; write NOTHING and surface a card error
};

/// @brief The one rule both save existence checks share.
///
/// `Occupied` is named for the FACT, not for one site's reaction: the overwrite check turns it into
/// a prompt, while the temp-filename search turns it into "try the next candidate".
///
/// @param present      The result of the existence check on the target path.
/// @param mayOverwrite Whether the user has already confirmed an overwrite.
/// @return `Refuse` whenever existence is undeterminable — never write over a file whose absence is
///         unconfirmed.
SaveAction save_action(const std::expected<bool, deluge::io::Status>& present, bool mayOverwrite);

/// @brief What a startup fail-safe existence check has established.
///
/// @warning Switch on this enum exhaustively, with no `default:` label, for the same reason given on
///          `Presence` above — though note that guard protects the FIRMWARE build only:
///          `tests/CMakeLists.txt` is a separate `project(DelugeTests)` which does not inherit
///          `DELUGE_COMMON_COMPILE_OPTIONS`.
enum class StartupPresence : uint8_t {
	Confirmed, ///< the path is there
	Missing,   ///< the path is known not to be there
	Unusable,  ///< existence could not be established. NEVER the "missing" branch.
};

/// @brief The one rule all four startup fail-safe checks share.
///
/// Named for the FACT, not for one site's reaction, exactly as `save_action`'s `Occupied` is: the
/// four sites react to `Missing` in four different ways -- skip the startup song, CREATE the
/// template, fall back to the template, give up -- and what they share is that the third state
/// exists and is distinguishable.
///
/// `Unusable` is never `Missing`. A boot that cannot tell whether the startup song is there must skip
/// it rather than replace it with a template, and must not treat a canary it could not read as
/// absent -- or the crash loop the fail-safe was built to break would resume.
///
/// @param present The result of the existence check on the path.
/// @return `Unusable` whenever existence is undeterminable.
StartupPresence startup_action(const std::expected<bool, deluge::io::Status>& present);

} // namespace deluge::storage
