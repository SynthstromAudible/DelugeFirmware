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
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/edit.h"
#include "model/edits/set_setting.h"
#include "model/global_effectable/global_effectable.h"
#include "model/instrument/midi_instrument.h"
#include "model/output.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "processing/audio_output.h"
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
		// An id names another clip or track: none, or one the song holds.
		if (row.path == "syncScalingClip") {
			values.push_back(Value::makeNumber(-1));
			for (::Clip* clip : AllClips::everywhere(&song)) {
				if (!clip->isArrangementOnlyClip()) {
					values.push_back(Value::makeNumber(clip->id()));
					break;
				}
			}
			break;
		}
		if (row.path == "outputRecordingFrom") {
			values.push_back(Value::makeNumber(-1));
			const auto owner = std::get<deluge::project::TrackAddress>(address.owner).output_id;
			for (::Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
				if (output->type == OutputType::AUDIO && output->id() && *output->id() != owner) {
					values.push_back(Value::makeNumber(*output->id()));
					break;
				}
			}
			break;
		}
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

/// @brief Apply @p value at @p path of @p address through its Edit.
bool applySetting(Song& song, const SettingAddress& address, std::string_view path, const Value& value) {
	deluge::edits::Context ctx{song, song.project};
	deluge::edits::SetSetting edit{address, std::string{path}, value};
	return deluge::edits::apply(ctx, edit) == Error::NONE;
}

/// @brief @p song saved as the app saves a song and loaded back as a new current song; nullptr when either fails.
Song* savedAndReloaded(Song& song) {
	static std::vector<char> buffer(8u << 20);
	const long length = deluge::scenarios::driver::saveSongCapture(song, buffer.data(), buffer.size());
	if (length < 0) {
		return nullptr;
	}
	return loadSong(std::string_view{buffer.data(), static_cast<size_t>(length)}, deluge::storage::kSongSaveFormat);
}

/// @brief The settings a save must carry through to the song it reloads into: an audio track recording from another
///        (by id in the record, by place in the file), and a MIDI track's hide flag (on a track never otherwise
///        edited) and a CC label, and a MIDI clip's arp with its randomizer locked: the values the render generated
///        come back as they were. Prints `SONG-SETTINGS-SAVE <leg> ok` for each leg the song has the tracks for.
/// @return The failures.
int32_t saveLegs(Song& song, deluge::scenarios::driver::Io io) {
	::AudioOutput* recorder = nullptr;
	::Output* source = nullptr;
	::MIDIInstrument* midi = nullptr;
	for (::Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (!output->id()) {
			continue;
		}
		if (output->type == OutputType::AUDIO) {
			if (recorder == nullptr) {
				recorder = static_cast<::AudioOutput*>(output);
			}
			else if (source == nullptr) {
				source = output;
			}
		}
		if (output->type == OutputType::MIDI_OUT && midi == nullptr) {
			midi = static_cast<::MIDIInstrument*>(output);
		}
	}
	int32_t failures = 0;
	if (recorder != nullptr && source != nullptr) {
		const SettingAddress input{SettingGroupId::AUDIO_INPUT, deluge::project::TrackAddress{*recorder->id()}};
		const uint32_t recorderId = *recorder->id();
		const uint32_t sourceId = *source->id();
		const bool set = applySetting(song, input, "inputChannel", Value::makeScalar("specificTrack"))
		                 && applySetting(song, input, "outputRecordingFrom", Value::makeNumber(sourceId));
		Song* reloaded = set ? savedAndReloaded(song) : nullptr;
		auto* back = reloaded != nullptr ? static_cast<::AudioOutput*>(reloaded->outputForId(recorderId)) : nullptr;
		::Output* from = back != nullptr ? back->getOutputRecordingFrom() : nullptr;
		const auto* record = reloaded != nullptr ? reloaded->project.output_structure.find(recorderId) : nullptr;
		if (from == nullptr || from->id() != sourceId || record == nullptr
		    || record->recording_from_output_id != sourceId) {
			std::fprintf(io.err, "SONG-SETTINGS-SAVE FAIL audioSource: the reloaded track does not record from %u\n",
			             sourceId);
			failures++;
		}
		else {
			std::fprintf(io.out, "SONG-SETTINGS-SAVE audioSource ok\n");
		}
		currentSong = &song;
	}
	if (midi != nullptr) {
		const SettingAddress device{SettingGroupId::INSTRUMENT_DEVICE, deluge::project::TrackAddress{*midi->id()}};
		const uint32_t midiId = *midi->id();
		(void)applySetting(song, device, "hideUnlabeledCC", Value::makeNumber(0));
		midi->editedByUser = false; // a track the user has not otherwise edited
		const bool set = applySetting(song, device, "hideUnlabeledCC", Value::makeNumber(1))
		                 && applySetting(song, device, "ccLabels/20", Value::makeScalar("SAVED LABEL"));
		Song* reloaded = set ? savedAndReloaded(song) : nullptr;
		auto* back = reloaded != nullptr ? static_cast<::MIDIInstrument*>(reloaded->outputForId(midiId)) : nullptr;
		if (back == nullptr || !back->hideUnlabeledCC || back->getNameFromCC(20) != "SAVED LABEL") {
			std::fprintf(io.err, "SONG-SETTINGS-SAVE FAIL midiDevice: the reloaded MIDI track lost its hide flag or CC "
			                     "label\n");
			failures++;
		}
		else {
			std::fprintf(io.out, "SONG-SETTINGS-SAVE midiDevice ok\n");
		}
		currentSong = &song;
	}
	::InstrumentClip* midiClip = nullptr;
	for (::Clip* clip : AllClips::everywhere(&song)) {
		if (clip->type == ClipType::INSTRUMENT && clip->output != nullptr
		    && clip->output->type == OutputType::MIDI_OUT) {
			midiClip = static_cast<::InstrumentClip*>(clip);
			break;
		}
	}
	if (midiClip != nullptr) {
		const uint32_t clipId = midiClip->id();
		const SettingAddress arp{SettingGroupId::CLIP_ARP, deluge::project::ClipAddress{.clip_id = clipId}};
		const bool set = applySetting(song, arp, "arpeggiator/arpMode", Value::makeScalar("arp"))
		                 && applySetting(song, arp, "arpeggiator/randomizerLock", Value::makeNumber(1));
		// What the render generates while the lock holds: its locked values and the note pattern.
		ArpeggiatorSettings& settings = midiClip->arpSettings;
		for (uint32_t i = 0; i < RANDOMIZER_LOCK_MAX_SAVED_VALUES; i++) {
			settings.lockedNoteProbabilityValues[i] = static_cast<int8_t>(i * 7 - 50);
			settings.lockedSpreadOctaveValues[i] = static_cast<int8_t>(3 - i);
		}
		settings.lastLockedNoteProbabilityParameterValue = 123456;
		settings.notePattern[0] = 5;
		Song* reloaded = set ? savedAndReloaded(song) : nullptr;
		auto* back = reloaded != nullptr ? static_cast<::InstrumentClip*>(reloaded->clipForId(clipId)) : nullptr;
		if (back == nullptr || !back->arpSettings.randomizerLock
		    || back->arpSettings.lockedNoteProbabilityValues != settings.lockedNoteProbabilityValues
		    || back->arpSettings.lockedSpreadOctaveValues != settings.lockedSpreadOctaveValues
		    || back->arpSettings.lastLockedNoteProbabilityParameterValue != 123456
		    || back->arpSettings.notePattern != settings.notePattern) {
			std::fprintf(io.err,
			             "SONG-SETTINGS-SAVE FAIL arpLock: the reloaded MIDI clip lost its locked arp values\n");
			failures++;
		}
		else {
			std::fprintf(io.out, "SONG-SETTINGS-SAVE arpLock ok\n");
		}
		currentSong = &song;
	}
	return failures;
}

const char* groupName(SettingGroupId group) {
	constexpr const char* kNames[] = {"SONG", "TRACK",       "INSTRUMENT_DEVICE", "LANE",     "NOTE_ROW", "AUDIO_CLIP",
	                                  "CLIP", "CLIP_VALUES", "AUDIO_INPUT",       "CLIP_ARP", "LANE_ARP"};
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
	// The save legs run on the song as loaded, before the sweep rewrites its settings.
	int32_t saveFailures = saveLegs(*song, io);

	std::vector<Target> targets;
	deluge::harness::forEachSettingAddress(
	    *song, [&](const SettingAddress& address, uint32_t owner, std::span<const std::string_view> paths) {
		    targets.push_back({address, owner, {paths.begin(), paths.end()}});
	    });

	std::map<std::string, int32_t> applied;
	int32_t failures = 0;
	// Each value is checked against its own owner's record and engine copy, and the whole song once each owner's group
	// is done, so a write that reaches another owner is still found.
	for (const Target& target : targets) {
		for (std::string_view path : target.paths) {
			const SettingInfo& row = rowOf(target.address.group, path);
			for (const Value& value : valuesOf(*song, target.address, row)) {
				deluge::edits::Context ctx{*song, song->project};
				deluge::edits::SetSetting edit{target.address, std::string{path}, value};
				const Error error = deluge::edits::apply(ctx, edit);
				std::string what =
				    error != Error::NONE
				        ? std::string{"refused"}
				        : deluge::harness::settingIncoherence(*song, target.address, target.owner, target.paths);
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
		if (const std::string what = deluge::harness::songSettingsIncoherence(*song); !what.empty()) {
			std::fprintf(io.err, "SONG-SETTINGS FAIL after %s %u: %s\n", groupName(target.address.group), target.owner,
			             what.c_str());
			failures++;
		}
	}
	for (const auto& [key, n] : applied) {
		std::fprintf(io.out, "SONG-SETTINGS %s n=%d\n", key.c_str(), static_cast<int>(n));
	}

	failures += saveFailures;
	std::fprintf(io.err, "SONG-SETTINGS-RESULT failures=%d\n", static_cast<int>(failures));
	return failures == 0 ? 0 : 3;
}

} // namespace

DELUGE_DRIVER_SCENARIO(song_settings, song_settings_main)
