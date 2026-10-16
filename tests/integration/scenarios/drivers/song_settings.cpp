/// @file
/// @brief Every song setting through SetSetting, over a corpus song: for every setting group of every owner the song
///        addresses, and every legal value of every path of the group's table, the Edit is applied and every record
///        must then equal its engine copy.
///
/// Arguments: the song's path and its kind (SONG). Exits 0 when every value holds, 1 when the song does not load, 3
/// when a value is refused or leaves a record and its engine copy apart. Prints `SONG-SETTINGS <group> <path> n=<k>`
/// per path, k counting the values applied across the song's owners.

#include "deluge-project/setting_group.h"
#include "engine-api/types/setting.h"
#include "extern.h"
#include "memory/general_memory_allocator.h"
#include "model/edits/edit.h"
#include "model/edits/set_setting.h"
#include "model/global_effectable/global_effectable.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/drivers/tree_load.h"
#include "scenarios/slot_coherence.h"
#include "serialization/tree.h"
#include "storage/document_format.h"
#include "storage/storage_manager.h"
#include "storage/value_tree_deserializer.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

using deluge::engine::SettingInfo;
using deluge::engine::SettingKind;
using deluge::project::SettingAddress;
using deluge::project::SettingGroupId;
using deluge::serialization::Value;

/// The song in @p bytes (of @p format), loaded as the app loads a song and made current; nullptr on a load failure.
Song* loadSong(std::string_view bytes, deluge::storage::DocumentFormat format) {
	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	std::optional<deluge::storage::Value> tree = parseDocument(bytes, format);
	if (!tree) {
		return nullptr;
	}
	deluge::storage::ValueTreeDeserializer adapter(*tree, sourceFormatOf(format));
	activeReader = &adapter;
	Error e = openTreeFile(adapter, "song", format);
	if (e == Error::NONE) {
		e = deluge::bridge::loadSong(*song, adapter);
	}
	activeReader = &smDeserializer;
	if (e != Error::NONE) {
		return nullptr;
	}
	currentSong = song;
	song->setCurrentClip(nullptr);
	// The engine's one reverb takes the current song's, as when a loaded song becomes the one playing.
	AudioEngine::getReverbParamsFromSong(song);
	return song;
}

/// The row of @p group's table at @p path.
const SettingInfo& rowOf(SettingGroupId group, std::string_view path) {
	for (const SettingInfo& row : deluge::project::table_of(group)) {
		if (row.path == path) {
			return row;
		}
	}
	__builtin_unreachable();
}

/// The values to apply at @p row for the owner @p address names: every value of a BOOL or a CHOICE; a text; an
/// integer's ends, default and user-scale ends. A sample marker keeps the value it has, since another would have to
/// lie within the clip's sample.
std::vector<Value> valuesOf(Song& song, const SettingAddress& address, const SettingInfo& row) {
	std::vector<Value> values;
	switch (row.kind) {
	case SettingKind::BOOL:
		values = {Value::makeNumber(0), Value::makeNumber(1)};
		break;
	case SettingKind::CHOICE:
		for (std::string_view choice : row.choices) {
			values.push_back(Value::makeScalar(std::string{choice}));
		}
		break;
	case SettingKind::TEXT:
		values = {Value::makeScalar("SONG SETTINGS")};
		break;
	case SettingKind::INTEGER: {
		if (row.path.ends_with("SamplePos")) {
			if (auto current = deluge::project::read_setting(song.project, address, row.path)) {
				values.push_back(*current);
			}
			break;
		}
		std::vector<int64_t> numbers{row.min, row.max, row.default_value};
		if (!row.user.identity()) {
			for (int32_t user : {row.user.user_min, row.user.user_max}) {
				if (const auto stored = deluge::engine::to_stored(row, user)) {
					numbers.push_back(*stored);
				}
			}
		}
		std::ranges::sort(numbers);
		const auto [first, last] = std::ranges::unique(numbers);
		numbers.erase(first, last);
		for (int64_t n : numbers) {
			values.push_back(Value::makeNumber(n));
		}
		break;
	}
	case SettingKind::STRUCTURED:
		break;
	}
	return values;
}

const char* groupName(SettingGroupId group) {
	constexpr const char* kNames[] = {"SONG",       "TRACK", "INSTRUMENT_DEVICE", "LANE",       "NOTE_ROW",
	                                  "AUDIO_CLIP", "CLIP",  "CLIP_VALUES",       "AUDIO_INPUT"};
	return kNames[static_cast<size_t>(group)];
}

int song_settings_main(int argc, char** argv, deluge::scenarios::driver::Io io) {
	if (argc < 3) {
		std::fprintf(io.err, "usage: song_settings <song> SONG\n");
		return 2;
	}
	FILE* f = std::fopen(argv[1], "rb");
	if (f == nullptr) {
		std::fprintf(io.err, "LOADFAIL cannot open %s\n", argv[1]);
		return 1;
	}
	std::string bytes;
	char chunk[4096];
	for (size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) {
		bytes.append(chunk, n);
	}
	std::fclose(f);
	Song* song = loadSong(bytes, deluge::storage::formatOfPath(argv[1]));
	if (song == nullptr) {
		std::fprintf(io.err, "LOADFAIL\n");
		return 1;
	}

	if (const std::string what = deluge::harness::songSettingsIncoherence(*song); !what.empty()) {
		std::fprintf(io.err, "SONG-SETTINGS FAIL loaded: %s\n", what.c_str());
		return 3;
	}

	struct Target {
		SettingAddress address;
		uint32_t owner;
		std::vector<std::string_view> paths;
	};
	std::vector<Target> targets;
	deluge::harness::forEachSettingAddress(
	    *song, [&](const SettingAddress& address, uint32_t owner, std::span<const std::string_view> paths) {
		    targets.push_back({address, owner, {paths.begin(), paths.end()}});
	    });

	std::map<std::string, int32_t> applied;
	int32_t failures = 0;
	for (const Target& target : targets) {
		for (std::string_view path : target.paths) {
			const SettingInfo& row = rowOf(target.address.group, path);
			for (const Value& value : valuesOf(*song, target.address, row)) {
				deluge::edits::Context ctx{*song, song->project};
				deluge::edits::SetSetting edit{target.address, std::string{path}, value};
				const Error error = deluge::edits::apply(ctx, edit);
				std::string what =
				    error != Error::NONE ? std::string{"refused"} : deluge::harness::songSettingsIncoherence(*song);
				if (!what.empty()) {
					std::fprintf(io.err, "SONG-SETTINGS FAIL %s %u %.*s=%s: %s\n", groupName(target.address.group),
					             target.owner, static_cast<int>(path.size()), path.data(), value.str().c_str(),
					             what.c_str());
					failures++;
					continue;
				}
				applied[std::string{groupName(target.address.group)} + " " + std::string{path}]++;
			}
		}
	}
	for (const auto& [key, n] : applied) {
		std::fprintf(io.out, "SONG-SETTINGS %s n=%d\n", key.c_str(), static_cast<int>(n));
	}
	std::fprintf(io.err, "SONG-SETTINGS-RESULT failures=%d\n", static_cast<int>(failures));
	return failures == 0 ? 0 : 3;
}

} // namespace

DELUGE_DRIVER_SCENARIO(song_settings, song_settings_main)
