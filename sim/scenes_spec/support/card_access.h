#pragma once

/// @file
/// @brief The two guards any spec that drives a real file listing needs: where the card is
///        (`SdRoot`) and why the audio engine must be held still while the catalogue works
///        (`QuietAudio`).
///
/// Both were born inside `file_catalogue_spec.cpp` (task 1 of the 2026-09-02 plan) and are hoisted
/// here by task 2, because `QuietAudio` is not a convenience -- **it is the disarm for a landmine
/// that segfaults a spec process rather than failing it**, and the next three specs in this plan
/// all have to step over it. A guard that has to be re-derived from a task report is a guard that
/// will eventually be re-derived wrong; one `#include` is not.
///
/// @warning **Include this BEFORE `<cppspec.hpp>`.** CppSpec `#define`s `it` (its own DSL keyword),
///          and this header pulls in `processing/engines/audio_engine.h`, whose include closure
///          reaches `util/container/ordered_pos_vector.h` -- which has locals named `it`. Included
///          after CppSpec, those locals expand to `self.it` and the translation unit fails to
///          compile with a wall of errors that name neither file. Put it with the project includes
///          at the top, not with `spec/support/spec_main.h` at the bottom.

#include "io/file.hpp"
#include "processing/engines/audio_engine.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace deluge::scenes::spec {

/// @brief Every file on a tree, as (path lowercased, size): FAT folds case, so names compare without it.
using CardFiles = std::set<std::pair<std::string, uint32_t>>;

/// @brief The files under @p dir on the host, relative to it.
/// @param dir A card's contents on the host (`SONGS/`, `SAMPLES/`, ...).
/// @return Its files, or nothing when @p dir does not exist.
inline CardFiles host_card_files(const std::string& dir) {
	CardFiles files;
	std::error_code ec;
	for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
	     !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
		if (it->is_regular_file()) {
			std::string rel = std::filesystem::relative(it->path(), dir).generic_string();
			std::ranges::transform(rel, rel.begin(), [](unsigned char c) { return std::tolower(c); });
			files.emplace(std::move(rel), static_cast<uint32_t>(it->file_size()));
		}
	}
	return files;
}

/// @brief The files on the installed card under @p path, appended to @p files.
/// @param path A directory on the card ("" for its root).
/// @param files Where each file goes, as host_card_files() spells it.
inline void installed_card_files(const std::string& path, CardFiles& files) {
	auto dir = deluge::io::Directory::open(path.empty() ? "/" : path);
	if (!dir.has_value()) {
		return;
	}
	for (;;) {
		auto entry = dir->read();
		if (!entry.has_value() || !entry->has_value()) {
			break;
		}
		const DelugeDirEntry& e = **entry;
		std::string name = e.name;
		if (name == "." || name == "..") {
			continue;
		}
		const std::string full = path.empty() ? name : path + "/" + name;
		if (e.is_directory) {
			installed_card_files(full, files);
		}
		else {
			std::string rel = full;
			std::ranges::transform(rel, rel.begin(), [](unsigned char c) { return std::tolower(c); });
			files.emplace(std::move(rel), e.size);
		}
	}
	(void)dir->close();
}

/// @brief Whether the installed card holds exactly the files under @p dir, at their sizes.
///
/// The card a file runs on is chosen before boot (`SCENES_SPEC_CARD`); this is how an example confirms it is the
/// card it was written against.
/// @param dir A card's contents on the host.
/// @return True when the card and @p dir hold the same files at the same sizes.
inline bool sd_root_matches(const std::string& dir) {
	CardFiles card;
	installed_card_files("", card);
	const CardFiles host = host_card_files(dir);
	return !host.empty() && card == host;
}

/// @brief Whether the installed card holds no files at all (`SCENES_SPEC_CARD=none`).
/// @return True when the card is empty.
inline bool no_sd_root_matches() {
	CardFiles card;
	installed_card_files("", card);
	return card.empty();
}

/// @brief Confirm the installed card is the one at @p root for an example; throws when it is not, which fails the
///        example rather than letting it list another card.
///
/// The card cannot be switched mid-run: the file's card is installed before boot from `SCENES_SPEC_CARD`, and a file
/// whose examples need two cards is split into one file per card.
struct SdRoot {
	/// @param root The card's contents on the host that the example expects to be installed.
	explicit SdRoot(const std::string& root) {
		if (!sd_root_matches(root)) {
			throw std::logic_error("SdRoot: the installed card is not " + root + " (set SCENES_SPEC_CARD)");
		}
	}
	SdRoot(const SdRoot&) = delete;
	SdRoot& operator=(const SdRoot&) = delete;
};

/// @brief Hold `AudioEngine`'s own re-entrancy latch for the scope, so the catalogue's yield points
///        are no-ops.
///
/// @warning **Without this, a spec that drives a listing does not fail -- it segfaults.**
///
/// `emptyFileItems()` and `deleteSomeFileItems()` pump `AudioEngine::routineWithClusterLoading()`
/// between chunks (`storage/file_catalogue.cpp:85 emptyFileItems`, `:101 deleteSomeFileItems`) --
/// they can be erasing thousands of entries, and the audio must keep running. A spec process never
/// boots the audio engine: `AudioEngine::init()` (`processing/engines/audio_engine.cpp:226`) builds
/// a preview `SoundDrum` off `currentSong` and only then sets `inputRingPos` (`:246`), so an
/// un-booted process walks into `fillInputRing` (`:179`) with a cursor of 0 and dies on the first
/// non-empty erase.
///
/// `audioRoutineLocked` is the engine's *own* answer to "do not run the routine right now"
/// (`:1122` -- `routine()` returns immediately while it is set, and `runRoutine()` reaches
/// `routine()` directly here because no scheduler task is registered). Holding it makes every pump
/// call a documented no-op. It changes nothing about the catalogue: the pump is a yield point, not
/// part of the listing, sort, search or cull contract.
///
/// @note This is a **spec-side** guard, and it stays spec-side on purpose. The shipped seam
///       (`src/scenes/browser/listing.cpp`) must never hold this latch: on the device the pump is
///       exactly what keeps audio alive across a multi-second directory read, and a listing that
///       silenced it would be a regression, not a safety measure.
struct QuietAudio {
	bool saved = AudioEngine::audioRoutineLocked;
	QuietAudio() { AudioEngine::audioRoutineLocked = true; }
	~QuietAudio() { AudioEngine::audioRoutineLocked = saved; }
	QuietAudio(const QuietAudio&) = delete;
	QuietAudio& operator=(const QuietAudio&) = delete;
};

} // namespace deluge::scenes::spec
