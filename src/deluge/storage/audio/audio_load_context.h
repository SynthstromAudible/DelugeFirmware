/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "definitions_cxx.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/// @brief What a load knows about its alternate directory.
enum class AlternateLoadDirStatus {
	NONE_SET,    ///< No alternate directory: the lookup belongs to no song, synth or kit load.
	NOT_FOUND,   ///< Probed, and absent.
	MIGHT_EXIST, ///< Named but not yet probed. Every song, synth and kit load starts here.
	DOES_EXIST,  ///< Probed, and present.
};

/// libdeluge/audio_load.h's opaque handle for a deluge::storage::AudioLoadContext.
struct DelugeAudioLoadContext;

namespace deluge::storage {

/// @brief How a lookup may use the async loader (libdeluge/audio_load.h) for a sample that is not in memory.
enum class AsyncLoadMode : uint8_t {
	/// Never: a miss is read from the card on the calling tier.
	Blocking,
	/// Ask the loader, and read from the card on the calling tier only when no request can be formed. The caller must
	/// handle Error::LOAD_IN_PROGRESS by asking again on a later tick.
	AsyncOrBlocking,
	/// Ask the loader, and never read a sample from the card on the calling tier: a path no request can be formed for
	/// answers Error::FILE_UNREADABLE, and a disabled card or an absent loader Error::SD_CARD. The caller must handle
	/// Error::LOAD_IN_PROGRESS as for AsyncOrBlocking. What AsyncSampleLoad (storage/audio/async_sample_load.h) uses.
	AsyncOnly,
};

/// @brief The state one load of a song, synth or kit carries while it finds its audio files.
///
/// A song's samples may sit in the song's own folder under their collect-media names, and a preset's samples in the
/// preset's own folder, so a load looks there as well as at each file's own path. Whether that folder exists is
/// learnt on the load's first miss and kept for the rest of the load, so every lookup of one load shares one context.
/// The owner of the load owns the context, and passes it by pointer.
///
/// A null context and a default-constructed one mean the same thing: no alternate directory, nothing to learn.
struct AudioLoadContext {
	/// The folder a load looks in besides each file's own path, "<dir>/<name>".
	std::string alternateDir{};
	/// What the load knows about @ref alternateDir so far.
	AlternateLoadDirStatus alternateDirStatus = AlternateLoadDirStatus::NONE_SET;
	/// What is being loaded. For SYNTH and KIT, a file found in the alternate directory takes that path as its own.
	/// For SONG, the in-memory search under the alternate directory's name is skipped.
	ThingType thing = ThingType::NONE;
	/// Paths an AsyncLoadMode::AsyncOnly lookup asked the loader for while @ref alternateDirStatus was still
	/// MIGHT_EXIST. Each of those requests carried only the file's own path, so a miss collected once the folder is
	/// known to exist is asked for again with the folder's candidates rather than taken as final. At most one entry
	/// per path; freed with the context.
	std::vector<std::string> requestedUnresolved{};

	/// @brief The context a song's load starts with.
	/// @param songDir  The folder the song file is in, e.g. "SONGS".
	/// @param songName The song file's name without its extension.
	[[nodiscard]] static AudioLoadContext forSong(std::string_view songDir, std::string_view songName) {
		return {alternateDirFor(songDir, songName), AlternateLoadDirStatus::MIGHT_EXIST, ThingType::SONG, {}};
	}

	/// @brief The context a synth's or kit's load starts with, when it is not part of a song's load.
	/// @param dirPath The folder the preset file is in, e.g. "KITS".
	/// @param name    The preset's name.
	/// @param thing   ThingType::SYNTH or ThingType::KIT.
	[[nodiscard]] static AudioLoadContext forInstrument(std::string_view dirPath, std::string_view name,
	                                                    ThingType thing) {
		return {alternateDirFor(dirPath, name), AlternateLoadDirStatus::MIGHT_EXIST, thing, {}};
	}

	/// @brief Whether @p context names no alternate directory: null, or never made for a load.
	[[nodiscard]] static bool isNone(const AudioLoadContext* context) {
		return context == nullptr || context->alternateDirStatus == AlternateLoadDirStatus::NONE_SET;
	}

	/// @brief The folder named for @p name in @p dir, "<dir>/<name>": where a song's collected media go, and where
	///        a preset's own files are looked for.
	[[nodiscard]] static std::string alternateDirFor(std::string_view dir, std::string_view name) {
		std::string path{dir};
		path.append("/");
		path.append(name);
		return path;
	}
};

/// @brief @p context as libdeluge/audio_load.h's opaque handle.
[[nodiscard]] inline DelugeAudioLoadContext* toAbi(AudioLoadContext& context) {
	return reinterpret_cast<DelugeAudioLoadContext*>(&context);
}

/// @copydoc toAbi(AudioLoadContext&)
[[nodiscard]] inline const DelugeAudioLoadContext* toAbi(const AudioLoadContext& context) {
	return reinterpret_cast<const DelugeAudioLoadContext*>(&context);
}

/// @brief The context behind a non-null handle made by toAbi().
[[nodiscard]] inline AudioLoadContext* fromAbi(DelugeAudioLoadContext* context) {
	return reinterpret_cast<AudioLoadContext*>(context);
}

/// @copydoc fromAbi(DelugeAudioLoadContext*)
[[nodiscard]] inline const AudioLoadContext* fromAbi(const DelugeAudioLoadContext* context) {
	return reinterpret_cast<const AudioLoadContext*>(context);
}

} // namespace deluge::storage
