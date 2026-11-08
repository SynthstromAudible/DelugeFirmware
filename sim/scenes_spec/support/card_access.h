#pragma once

/// @file
/// @brief Two guards for specs that drive the real app: where the card is (`SdRoot`), and holding
///        the audio routine still while an example runs (`QuietAudio`).
///
/// They live here so each is derived once: a guard that has to be re-derived in each spec will
/// eventually be re-derived wrong; one `#include` is not.
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
	for (auto walk = std::filesystem::recursive_directory_iterator(dir, ec);
	     !ec && walk != std::filesystem::recursive_directory_iterator(); walk.increment(ec)) {
		if (walk->is_regular_file()) {
			std::string rel = std::filesystem::relative(walk->path(), dir).generic_string();
			std::ranges::transform(rel, rel.begin(), [](unsigned char c) { return std::tolower(c); });
			files.emplace(std::move(rel), static_cast<uint32_t>(walk->file_size()));
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

/// @brief Confirm no card files are installed for an example (`SCENES_SPEC_CARD=none`); throws when there are.
struct NoSdRoot {
	NoSdRoot() {
		if (!no_sd_root_matches()) {
			throw std::logic_error("NoSdRoot: a card is installed (set SCENES_SPEC_CARD=none)");
		}
	}
	NoSdRoot(const NoSdRoot&) = delete;
	NoSdRoot& operator=(const NoSdRoot&) = delete;
};

/// @brief Hold off the audio tick for the scope, so the audio routine does nothing while an example runs.
///
/// An `AudioEngine::AudioTickHold`: the audio tier's `routine_task()` and `routine()` return at once while it is
/// alive, so no render interleaves with what an example asserts. Code that defers while the routine is busy takes its
/// deferring branch too: `View::endMIDILearn` and the port skip the settings write, and the background waveform
/// overview scan waits (`deluge::sample::advance_overview_scan`). Holds nest, so one inside the scenario's whole-file
/// hold leaves ticks held when it ends.
///
/// @note A spec-side guard, held for a whole example. Shipped code holds ticks only around the edit itself: on the
///       device every held tick is silence.
struct QuietAudio {
	AudioEngine::AudioTickHold hold;
};

} // namespace deluge::scenes::spec
