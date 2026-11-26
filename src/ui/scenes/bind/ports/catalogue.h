#pragma once

/// @file
/// @brief The card's file listing a browser shows: one catalogue for the whole program, listed one folder at a time
///        and owned by one listing at a time.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace deluge::ui::scenes::bind {

/// @brief Which files a listing keeps, besides folders.
enum class FileKinds : uint8_t {
	XML,   ///< presets, songs and MIDI device definitions: `.XML` and `.json`
	AUDIO, ///< samples: `.WAV`, `.AIFF` and `.AIF`
	SYX,   ///< DX7 SysEx banks: `.SYX`
};

/// @brief Which entries a listing reads, relative to the name it starts at.
enum class SearchFrom : uint8_t {
	LEFT,  ///< the entries before the name
	RIGHT, ///< the name and the entries after it
	BOTH,  ///< a window either side of the name
};

/// @brief What a listing does to the catalogue's folder before it reads.
enum class ListingStep : uint8_t {
	OPEN,        ///< list `path`, or the current folder when `path` is empty
	INTO_FOLDER, ///< go into the folder `path` names, below the current one
	UP_LEVEL,    ///< go to the parent folder, starting at the folder left
	BY_PATH,     ///< list the folder holding the file `path` names, starting at that file
	RELOAD,      ///< list the current folder again
};

/// @brief One listing request.
struct ListingSpec {
	ListingStep step = ListingStep::OPEN; ///< what happens to the folder before the read
	std::string path;                     ///< OPEN: the folder; INTO_FOLDER: the child folder's name; BY_PATH: the file
	std::string start_at;                 ///< the name the read starts at; empty starts at the folder's first entry
	std::string default_dir;              ///< the folder tried, and made, when the requested one does not exist
	FileKinds kinds = FileKinds::XML;     ///< which files the listing keeps
	bool allow_folders = true;            ///< whether folders are listed
	bool allow_folders_sharing_name_with_file = false; ///< whether a folder named as a file is kept beside it
	bool interpret_note_names = false;                 ///< whether names sort note names in pitch order
	std::optional<SearchFrom> search;                  ///< empty: BOTH when there is a name to start at, else RIGHT
};

/// @brief A listing's result: the name a caller cannot read back from the catalogue, and its error.
struct ListingOutcome {
	/// @brief The error a failed listing announces, as `base::notify::notice_error_code` takes it; empty on success.
	std::optional<int16_t> error;
	std::string entered_text; ///< UP_LEVEL: the folder left; BY_PATH: the file's name, which the caller selects
};

/// @brief One entry of the listed folder.
struct CatalogueEntry {
	std::string_view name; ///< the file's name with its extension, or the folder's; valid until the catalogue changes
	bool folder = false;   ///< whether the entry is a folder
};

/// @brief Where a name sorts among the listed entries.
struct SearchHit {
	std::size_t index = 0; ///< the first entry not before the name; the count when every entry is before it
	bool exact = false;    ///< whether the entry at `index` is the name
};

/// @brief The card's file catalogue: one for the whole program, owned by one listing at a time.
class Catalogue {
public:
	virtual ~Catalogue() = default;
	/// @brief Take the catalogue for @p owner.
	/// @return False when another owner holds it, which is left holding it.
	[[nodiscard]] virtual bool claim(const void* owner) = 0;
	/// @brief Give the catalogue back; nothing happens unless @p owner holds it.
	virtual void release(const void* owner) = 0;
	/// @brief Run one listing on the claimed catalogue, on the caller's stack.
	[[nodiscard]] virtual ListingOutcome list(const ListingSpec& spec) = 0;
	/// @return The listed folder's path, empty at the card's root.
	[[nodiscard]] virtual std::string_view dir() const = 0;
	/// @return The number of entries held: the listing's window, not the whole folder.
	[[nodiscard]] virtual std::size_t count() const = 0;
	/// @return The entry at @p index, or none past the end.
	[[nodiscard]] virtual std::optional<CatalogueEntry> entry(std::size_t index) const = 0;
	/// @brief Where @p name sorts among the entries, in the order the last listing sorted them.
	[[nodiscard]] virtual SearchHit search(std::string_view name) = 0;
	/// @return Whether the listing left out entries before its window.
	[[nodiscard]] virtual bool culled_before() const = 0;
	/// @return Whether the listing left out entries after its window.
	[[nodiscard]] virtual bool culled_after() const = 0;
	/// @brief Drop the entry at @p index, as a deleted file leaves the list; nothing happens past the end.
	virtual void erase(std::size_t index) = 0;
	/// @brief Drop every entry.
	virtual void clear() = 0;
};

} // namespace deluge::ui::scenes::bind
