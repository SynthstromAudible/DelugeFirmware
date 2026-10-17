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
#include "model/drum/drum.h"
#include "model/edits/edit.h"
#include "model/edits/learn_mapping.h"
#include "model/edits/set_setting.h"
#include "model/global_effectable/global_effectable.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/output.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/midi_command.h"
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
#include <tuple>
#include <vector>

namespace {

using deluge::engine::SettingInfo;
using deluge::engine::SettingKind;
using deluge::project::SettingAddress;
using deluge::project::SettingGroupId;
using deluge::serialization::Value;
namespace params = deluge::modulation::params;

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
		// What the render generates while the lock holds: its locked values and the note pattern. They are planted
		// first, so the Edits after them must leave them as they are.
		ArpeggiatorSettings& settings = midiClip->arpSettings;
		for (uint32_t i = 0; i < RANDOMIZER_LOCK_MAX_SAVED_VALUES; i++) {
			settings.lockedNoteProbabilityValues[i] = static_cast<int8_t>(i * 7 - 50);
			settings.lockedSpreadOctaveValues[i] = static_cast<int8_t>(3 - i);
		}
		settings.lastLockedNoteProbabilityParameterValue = 123456;
		settings.notePattern[0] = 5;
		const auto generated =
		    std::tuple{settings.lockedNoteProbabilityValues, settings.lockedSpreadOctaveValues, settings.notePattern};
		const bool set = applySetting(song, arp, "arpeggiator/arpMode", Value::makeScalar("arp"))
		                 && applySetting(song, arp, "arpeggiator/randomizerLock", Value::makeNumber(0))
		                 && applySetting(song, arp, "arpeggiator/randomizerLock", Value::makeNumber(1))
		                 && applySetting(song, arp, "arpeggiator/numOctaves", Value::makeNumber(3))
		                 && applySetting(song, arp, "arpeggiator/noteMode", Value::makeScalar("down"))
		                 && applySetting(song, arp, "arpeggiator/preset", Value::makeScalar("up"));
		Song* reloaded = set ? savedAndReloaded(song) : nullptr;
		auto* back = reloaded != nullptr ? static_cast<::InstrumentClip*>(reloaded->clipForId(clipId)) : nullptr;
		if (back == nullptr || !back->arpSettings.randomizerLock
		    || std::tuple{back->arpSettings.lockedNoteProbabilityValues, back->arpSettings.lockedSpreadOctaveValues,
		                  back->arpSettings.notePattern}
		           != generated
		    || back->arpSettings.lastLockedNoteProbabilityParameterValue != 123456) {
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

/// @brief The table's name for patched param @p p of a sound, or (when @p unpatchedKind is given) unpatched param @p p
///        of that kind.
deluge::engine::ParamId knobParam(int32_t p, std::optional<params::Kind> unpatchedKind = std::nullopt) {
	ParamDescriptor descriptor;
	descriptor.setToHaveParamOnly(unpatchedKind ? params::UNPATCHED_START + p : p);
	return deluge::bridge::knobTargetOf(descriptor, unpatchedKind.value_or(params::Kind::UNPATCHED_SOUND));
}

/// @brief @p mapping as a failure shows it: its target's kind (the MappingTarget alternative), its source's channel or
///        zone, note or CC and device, and its relative flag.
std::string describeMapping(const deluge::project::Mapping& mapping) {
	std::string text = "target#" + std::to_string(mapping.target.index()) + " ";
	if (const auto* knob = std::get_if<deluge::project::ParamTarget>(&mapping.target)) {
		text += "param " + std::to_string(knob->param.value) + " ";
	}
	text += std::to_string(mapping.source.channel_or_zone) + "/" + std::to_string(mapping.source.note_or_cc);
	if (const auto& device = mapping.source.device) {
		text += " port=" + std::to_string(static_cast<int>(device->port)) + " name=" + device->name
		        + " ids=" + std::to_string(device->vendor_id) + ":" + std::to_string(device->product_id);
	}
	return text + (mapping.relative ? " relative" : "");
}

/// @brief The bindings a save must carry through to the song it reloads into, learned through the mapping Edits: an
///        MPE-zone knob on a synth, a song knob, an audio track's knob (when the song has one), a kit's input (its root
///        note) told apart by its port, a clip mute from any device, a kit row's mute, and a kit row's input from a
///        hosted device the Deluge does not know (whose name only the table holds). The reloaded song's table must hold
///        the same entries, and each engine field play what its entry names. Prints `SONG-SETTINGS-SAVE mappings ok`
///        when the song has a synth, a kit row and an instrument clip, and `SONG-SETTINGS-SAVE audioKnob ok` when it
///        also has an audio track, whose knob was among them.
/// @return The failures.
int32_t mappingsLeg(Song& song, deluge::scenarios::driver::Io io) {
	using deluge::project::ControlsAddress;
	using deluge::project::Mapping;
	using deluge::project::MidiDeviceRef;
	::Output* synth = nullptr;
	::Output* audio = nullptr;
	::Output* kit = nullptr;
	::Drum* row = nullptr;
	for (::Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (!output->id()) {
			continue;
		}
		if (output->type == OutputType::SYNTH && synth == nullptr) {
			synth = output;
		}
		if (output->type == OutputType::AUDIO && audio == nullptr) {
			audio = output;
		}
		if (output->type == OutputType::KIT && row == nullptr) {
			for (::Drum* drum = static_cast<::Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
				if (drum->id()) {
					kit = output;
					row = drum;
					break;
				}
			}
		}
	}
	::Clip* clip = nullptr;
	for (::Clip* each : AllClips::everywhere(&song)) {
		if (each->type == ClipType::INSTRUMENT) {
			clip = each;
			break;
		}
	}
	if (synth == nullptr || row == nullptr || clip == nullptr) {
		return 0;
	}
	const MidiDeviceRef absent{
	    .port = MidiDeviceRef::Port::HOSTED, .name = "SONG SETTINGS ABSENT", .vendor_id = 0x1209, .product_id = 0x5d1e};
	const uint32_t rowId = *row->id();
	const Mapping absentInput{.source = {.channel_or_zone = 9, .note_or_cc = 37, .device = absent},
	                          .target = deluge::project::LaneInputTarget{rowId}};
	std::vector<Mapping> learned{
	    {.source = {.channel_or_zone = 17, .note_or_cc = 74},
	     .target =
	         deluge::project::ParamTarget{{ControlsAddress::Kind::OUTPUT, *synth->id()}, knobParam(params::LOCAL_PAN)}},
	    {.source = {.channel_or_zone = 4, .note_or_cc = 21},
	     .target =
	         deluge::project::ParamTarget{{ControlsAddress::Kind::SONG, 0},
	                                      knobParam(params::UNPATCHED_DELAY_AMOUNT, params::Kind::UNPATCHED_GLOBAL)}},
	    {.source = {.channel_or_zone = 3,
	                .note_or_cc = 36,
	                .device = MidiDeviceRef{.port = MidiDeviceRef::Port::UPSTREAM_USB_2}},
	     .target = deluge::project::TrackInputTarget{*kit->id()}},
	    {.source = {.channel_or_zone = 2, .note_or_cc = 40}, .target = deluge::project::ClipMuteTarget{clip->id()}},
	    {.source = {.channel_or_zone = 9, .note_or_cc = 36}, .target = deluge::project::DrumMuteTarget{rowId}},
	    absentInput,
	};
	if (audio != nullptr) {
		learned.push_back({.source = {.channel_or_zone = 5, .note_or_cc = 22},
		                   .target = deluge::project::ParamTarget{
		                       {ControlsAddress::Kind::OUTPUT, *audio->id()},
		                       knobParam(params::UNPATCHED_LPF_FREQ, params::Kind::UNPATCHED_GLOBAL)}});
	}
	bool set = true;
	for (const Mapping& mapping : learned) {
		deluge::edits::Context ctx{song, song.project};
		deluge::edits::LearnMapping edit{mapping};
		set = set && deluge::edits::apply(ctx, edit) == Error::NONE;
	}
	std::string what = !set ? "a learn was refused" : deluge::harness::mappingIncoherence(song);
	// The Deluge knew no device of that name: projecting the row's input made it an entry, as a load does, and the row
	// plays from that entry's cable, which is not connected.
	::MIDICable* made = deluge::bridge::find_cable(absent);
	if (what.empty() && (made == nullptr || row->midiInput.cable != made || made->connectionFlags != 0)) {
		what = "the absent device's row input does not play from an unconnected entry made for it";
	}
	Song* reloaded = what.empty() ? savedAndReloaded(song) : nullptr;
	if (what.empty() && reloaded == nullptr) {
		what = "the song did not save and reload";
	}
	if (what.empty()) {
		const auto entries = song.project.mappings.entries();
		const auto back = reloaded->project.mappings.entries();
		for (Mapping mapping : learned) {
			// The table sets a knob's relative flag as it learns it.
			if (const Mapping* entry = song.project.mappings.find(mapping.target)) {
				mapping.relative = entry->relative;
			}
			if (what.empty() && std::ranges::find(back, mapping) == back.end()) {
				std::string held;
				if (const auto* knob = std::get_if<deluge::project::ParamTarget>(&mapping.target)) {
					for (const Mapping& entry : reloaded->project.mappings.params_of(knob->owner)) {
						held += (held.empty() ? "" : ", ") + describeMapping(entry);
					}
				}
				else if (const Mapping* entry = reloaded->project.mappings.find(mapping.target)) {
					held = describeMapping(*entry);
				}
				what = "the reloaded table lost the learned " + describeMapping(mapping) + " (it holds "
				       + (held.empty() ? std::string{"none"} : held) + ")";
			}
		}
		if (what.empty() && !std::ranges::is_permutation(entries, back)) {
			what = "the reloaded table holds other entries (" + std::to_string(back.size()) + " for "
			       + std::to_string(entries.size()) + ")";
		}
	}
	if (what.empty()) {
		what = deluge::harness::mappingIncoherence(*reloaded);
	}
	if (what.empty()) {
		// The reload names the same entry for the absent device.
		::Drum* backRow = reloaded->drumForId(rowId);
		if (backRow == nullptr || backRow->midiInput.cable != made || backRow->midiInput.channelOrZone != 9
		    || backRow->midiInput.noteOrCC != 37) {
			what = "the reloaded kit row's input does not play from the absent device's entry";
		}
	}
	if (!what.empty()) {
		std::fprintf(io.err, "SONG-SETTINGS-SAVE FAIL mappings: %s\n", what.c_str());
	}
	else {
		std::fprintf(io.out, "SONG-SETTINGS-SAVE mappings ok\n");
		if (audio != nullptr) {
			// The audio track's knob was among those learned, saved and reloaded.
			std::fprintf(io.out, "SONG-SETTINGS-SAVE audioKnob ok\n");
		}
	}
	currentSong = &song;
	return what.empty() ? 0 : 1;
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
	int32_t saveFailures = saveLegs(*song, io) + mappingsLeg(*song, io);

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
