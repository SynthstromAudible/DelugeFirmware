#include "deluge-project/audio_input.h"
#include "deluge-project/setting_group.h"

#include "deluge-project/fraction.h"
#include "deluge-project/sequence_direction.h"
#include "deluge-project/song.h"
#include "engine-api/types/sync.h"
#include "serialization/tree.h"

#include <array>
#include <climits>
#include <string>
#include <vector>

namespace deluge::project {

namespace {

using engine::SettingInfo;
using engine::SettingKind;
using engine::SettingUnits;
using engine::UserScale;
using serialization::Value;

/// A fraction setting a menu shows as 0..50 (`kMaxMenuValue`), stored as the file's Q31.
constexpr UserScale kFiftyths{.user_min = 0, .user_max = 50, .stored_min = 0, .stored_max = INT32_MAX};

constexpr SettingInfo integer(std::string_view path, int32_t min, int32_t max, int32_t default_value = 0,
                              UserScale user = {}, SettingUnits units = SettingUnits::NONE) {
	return SettingInfo{.path = path,
	                   .kind = SettingKind::INTEGER,
	                   .min = min,
	                   .max = max,
	                   .user = user,
	                   .default_value = default_value,
	                   .units = units};
}
constexpr SettingInfo boolean(std::string_view path, int32_t default_value = 0) {
	return SettingInfo{.path = path, .kind = SettingKind::BOOL, .default_value = default_value};
}
constexpr SettingInfo text(std::string_view path) {
	return SettingInfo{.path = path, .kind = SettingKind::TEXT};
}

Value number(int64_t n) {
	return Value::makeNumber(n);
}
std::optional<int32_t> integer_of(const Value& value) {
	return serialization::int_of(&value);
}

// ---------------------------------------------------------------------------------------------
// SONG

constexpr std::string_view kSongFixedPaths[] = {"reverb/roomSize",
                                                "reverb/dampening",
                                                "reverb/width",
                                                "reverb/hpf",
                                                "reverb/lpf",
                                                "reverb/pan",
                                                "reverb/model",
                                                "reverb/compressor/attack",
                                                "reverb/compressor/release",
                                                "reverb/compressor/volume",
                                                "reverb/compressor/shape",
                                                "reverb/compressor/syncLevel",
                                                "swingInterval",
                                                "syncScalingClip"};

constexpr std::string_view kDisabledPrefix = "scales/disabled/";

/// The song table: the fixed rows, then one `scales/disabled/<i>` row per preset scale. The scale paths are built
/// once, so the table's views stay valid for the program's life.
std::span<const SettingInfo> song_table() {
	static const std::vector<std::string> scale_paths = [] {
		std::vector<std::string> paths;
		for (int32_t i = 0; i < music::NUM_PRESET_SCALES; i++) {
			paths.push_back(std::string{kDisabledPrefix} + std::to_string(i));
		}
		return paths;
	}();
	static const std::vector<SettingInfo> rows = [] {
		std::vector<SettingInfo> out{
		    integer("reverb/roomSize", 0, INT32_MAX, q31_of(30.0f / 50), kFiftyths),
		    integer("reverb/dampening", 0, INT32_MAX, q31_of(36.0f / 50), kFiftyths),
		    integer("reverb/width", 0, INT32_MAX, INT32_MAX, kFiftyths),
		    integer("reverb/hpf", 0, INT32_MAX, 0, kFiftyths),
		    integer("reverb/lpf", 0, INT32_MAX, INT32_MAX, kFiftyths),
		    // The pan, the sidechain's attack and release (rate tables) and its volume (with -1 = automatic) map
		    // non-linearly onto their menus; their menus keep that mapping.
		    integer("reverb/pan", INT32_MIN, INT32_MAX),
		    integer("reverb/model", 0, 2, 1),
		    integer("reverb/compressor/attack", 0, INT32_MAX),
		    integer("reverb/compressor/release", 0, INT32_MAX),
		    integer("reverb/compressor/volume", INT32_MIN, INT32_MAX),
		    integer("reverb/compressor/shape", INT32_MIN, INT32_MAX, -601295438),
		    integer("reverb/compressor/syncLevel", 0, engine::kMaxSyncLevel, 0, {}, SettingUnits::SYNC_LEVEL),
		    integer("swingInterval", 0, engine::kMaxSyncLevel, 0, {}, SettingUnits::SYNC_LEVEL),
		    // A clip's durable id; -1 = none.
		    integer("syncScalingClip", -1, INT32_MAX, -1),
		};
		for (const std::string& path : scale_paths) {
			out.push_back(boolean(path));
		}
		return out;
	}();
	return rows;
}

/// The preset scale a `scales/disabled/<i>` path names, or nullopt.
std::optional<size_t> disabled_scale_of(std::string_view path) {
	if (!path.starts_with(kDisabledPrefix)) {
		return std::nullopt;
	}
	const std::optional<int64_t> index = engine::whole_number(path.substr(kDisabledPrefix.size()));
	if (!index || *index < 0 || *index >= music::NUM_PRESET_SCALES) {
		return std::nullopt;
	}
	return static_cast<size_t>(*index);
}

std::optional<Value> read_song(const Song& song, std::string_view path) {
	const ReverbSettings& r = song.reverb;
	if (path == "reverb/roomSize") {
		return number(q31_of(r.room_size));
	}
	if (path == "reverb/dampening") {
		return number(q31_of(r.damping));
	}
	if (path == "reverb/width") {
		return number(q31_of(r.width));
	}
	if (path == "reverb/hpf") {
		return number(q31_of(r.hpf));
	}
	if (path == "reverb/lpf") {
		return number(q31_of(r.lpf));
	}
	if (path == "reverb/pan") {
		return number(r.pan);
	}
	if (path == "reverb/model") {
		return number(std::to_underlying(r.model));
	}
	if (path == "reverb/compressor/attack") {
		return number(r.sidechain_attack);
	}
	if (path == "reverb/compressor/release") {
		return number(r.sidechain_release);
	}
	if (path == "reverb/compressor/volume") {
		return number(r.sidechain_volume);
	}
	if (path == "reverb/compressor/shape") {
		return number(r.sidechain_shape);
	}
	if (path == "reverb/compressor/syncLevel") {
		return number(engine::index_of(r.sidechain_sync));
	}
	if (path == "swingInterval") {
		return number(engine::index_of(song.groove.swing_interval));
	}
	if (path == "syncScalingClip") {
		return number(song.transport.sync_scaling_clip_id ? int64_t{*song.transport.sync_scaling_clip_id} : -1);
	}
	if (const std::optional<size_t> scale = disabled_scale_of(path)) {
		return number(song.scale.disabled_preset_scales.test(*scale) ? 1 : 0);
	}
	return std::nullopt;
}

void write_song(Song& song, std::string_view path, int32_t v) {
	ReverbSettings& r = song.reverb;
	if (path == "syncScalingClip") {
		song.transport.sync_scaling_clip_id = v < 0 ? std::nullopt : std::optional<uint32_t>{static_cast<uint32_t>(v)};
		return;
	}
	if (path == "reverb/roomSize") {
		r.room_size = fraction_of(v);
	}
	else if (path == "reverb/dampening") {
		r.damping = fraction_of(v);
	}
	else if (path == "reverb/width") {
		r.width = fraction_of(v);
	}
	else if (path == "reverb/hpf") {
		r.hpf = fraction_of(v);
	}
	else if (path == "reverb/lpf") {
		r.lpf = fraction_of(v);
	}
	else if (path == "reverb/pan") {
		r.pan = v;
	}
	else if (path == "reverb/model") {
		r.model = static_cast<ReverbModel>(v);
	}
	else if (path == "reverb/compressor/attack") {
		r.sidechain_attack = v;
	}
	else if (path == "reverb/compressor/release") {
		r.sidechain_release = v;
	}
	else if (path == "reverb/compressor/volume") {
		r.sidechain_volume = v;
	}
	else if (path == "reverb/compressor/shape") {
		r.sidechain_shape = v;
	}
	else if (path == "reverb/compressor/syncLevel") {
		r.sidechain_sync = engine::sync_level_from_index(v);
	}
	else if (path == "swingInterval") {
		song.groove.swing_interval = engine::sync_level_from_index(v);
	}
	else if (const std::optional<size_t> scale = disabled_scale_of(path)) {
		song.scale.disabled_preset_scales.set(*scale, v != 0);
	}
}

// ---------------------------------------------------------------------------------------------
// TRACK and INSTRUMENT_DEVICE (on DocOutput)

constexpr SettingInfo kTrackTable[] = {text("name")};

/// The MIDI CCs an instrument's Y axis can go out as: its MPE Y (CC 74) or the mod wheel (CC 1).
constexpr std::string_view kMpeYChoices[] = {"74", "1"};

/// The highest channel word an instrument can hold: 16 channels, the two MPE zones, and the transpose sentinel.
constexpr int32_t kMaxChannelWord = 55;

constexpr SettingInfo kInstrumentDeviceTable[] = {
    integer("channel", 0, kMaxChannelWord),
    integer("channelSuffix", -1, 25, -1),
    text("deviceDefinitionFileName"),
    SettingInfo{.path = "outputMpeY", .kind = SettingKind::CHOICE, .choices = kMpeYChoices},
    boolean("collapseAftertouch"),
    boolean("collapseMpe", 1),
    integer("cv2Source", 0, 4),
};

std::optional<Value> read_output(const DocOutput& o, SettingGroupId group, std::string_view path) {
	if (group == SettingGroupId::TRACK) {
		return path == "name" ? std::optional<Value>{Value::makeScalar(o.name)} : std::nullopt;
	}
	if (path == "channel") {
		return number(o.channel);
	}
	if (path == "channelSuffix") {
		return number(o.channel_suffix);
	}
	if (path == "deviceDefinitionFileName") {
		return Value::makeScalar(o.device_definition_file_name);
	}
	if (path == "outputMpeY") {
		return number(o.output_mpe_y);
	}
	if (path == "collapseAftertouch") {
		return number(o.collapse_aftertouch ? 1 : 0);
	}
	if (path == "collapseMpe") {
		return number(o.collapse_mpe ? 1 : 0);
	}
	if (path == "cv2Source") {
		return number(std::to_underlying(o.cv2_source));
	}
	return std::nullopt;
}

void write_output(DocOutput& o, std::string_view path, const Value& value) {
	const std::string& text_value = value.str();
	const int32_t v = integer_of(value).value_or(0);
	if (path == "name") {
		o.name = text_value;
	}
	else if (path == "channel") {
		o.channel = v;
	}
	else if (path == "channelSuffix") {
		o.channel_suffix = v;
	}
	else if (path == "deviceDefinitionFileName") {
		o.device_definition_file_name = text_value;
	}
	else if (path == "outputMpeY") {
		o.output_mpe_y = static_cast<uint8_t>(v);
	}
	else if (path == "collapseAftertouch") {
		o.collapse_aftertouch = v != 0;
	}
	else if (path == "collapseMpe") {
		o.collapse_mpe = v != 0;
	}
	else if (path == "cv2Source") {
		o.cv2_source = static_cast<CVMode>(v);
	}
}

// ---------------------------------------------------------------------------------------------
// LANE (on DocDrum)

constexpr SettingInfo kLaneTable[] = {text("name")};

// ---------------------------------------------------------------------------------------------
// NOTE_ROW (on DocNoteRow)

constexpr std::string_view kDirectionChoices[] = {"forward", "reverse", "pingpong", "obeyParent"};
/// A bend range's semitones, as its menu offers them.
constexpr int32_t kMaxBendRange = 96;
/// The factory bend ranges: a whole clip's, and a finger's (MPE).
constexpr int32_t kDefaultBendRange = 2;
constexpr int32_t kDefaultBendRangeMpe = 48;

constexpr SettingInfo kNoteRowTable[] = {
    SettingInfo{.path = "sequenceDirection", .kind = SettingKind::CHOICE, .choices = kDirectionChoices},
    // A kit row's finger bend; a melodic row has none of its own.
    integer("bendRange", 0, kMaxBendRange, kDefaultBendRangeMpe, {}, SettingUnits::SEMITONES),
};

DocNoteRow* resolve_note_row(Song& song, const NoteRowAddress& address) {
	DocClip* clip = song.clip_structure.find(address.clip_id);
	if (clip == nullptr) {
		return nullptr;
	}
	for (DocNoteRow& row : clip->note_rows) {
		const bool matches = address.is_kit ? (row.drum_id && *row.drum_id == static_cast<uint32_t>(address.row_key))
		                                    : row.y == address.row_key;
		if (matches) {
			return &row;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------------------------
// AUDIO_INPUT (on DocOutput)

constexpr SettingInfo kAudioInputTable[] = {
    SettingInfo{.path = "mode", .kind = SettingKind::CHOICE, .choices = kAudioModeWords},
    SettingInfo{.path = "inputChannel", .kind = SettingKind::CHOICE, .choices = kInputChannelWords, .default_value = 1},
    // Another track's durable id; -1 = none.
    integer("outputRecordingFrom", -1, INT32_MAX, -1),
};

std::optional<Value> read_audio_input(const DocOutput& o, std::string_view path) {
	if (path == "mode") {
		return Value::makeScalar(std::string{kAudioModeWords[o.audio_mode]});
	}
	if (path == "inputChannel") {
		return Value::makeScalar(std::string{kInputChannelWords[o.input_channel]});
	}
	return number(o.recording_from_output_id ? int64_t{*o.recording_from_output_id} : -1);
}

void write_audio_input(DocOutput& o, std::string_view path, const Value& value) {
	if (path == "mode") {
		o.audio_mode = static_cast<uint8_t>(index_of_word(kAudioModeWords, value.str()).value_or(0));
	}
	else if (path == "inputChannel") {
		o.input_channel = static_cast<int8_t>(index_of_word(kInputChannelWords, value.str()).value_or(0));
	}
	else {
		const int32_t v = integer_of(value).value_or(-1);
		o.recording_from_output_id = v < 0 ? std::nullopt : std::optional<uint32_t>{static_cast<uint32_t>(v)};
	}
}

// ---------------------------------------------------------------------------------------------
// CLIP and CLIP_VALUES (on DocClip)

constexpr SettingInfo kClipTable[] = {
    text("name"),
    SettingInfo{.path = "sequenceDirection", .kind = SettingKind::CHOICE, .choices = kDirectionChoices},
};

/// MIDI program numbers 0..127; 128 is none (no message sent).
constexpr int32_t kNoMidiProgram = 128;

constexpr SettingInfo kClipValuesTable[] = {
    integer("midiBank", 0, kNoMidiProgram, kNoMidiProgram),
    integer("midiSub", 0, kNoMidiProgram, kNoMidiProgram),
    integer("midiPGM", 0, kNoMidiProgram, kNoMidiProgram),
    integer("bendRange", 0, kMaxBendRange, kDefaultBendRange, {}, SettingUnits::SEMITONES),
    integer("bendRangeMPE", 0, kMaxBendRange, kDefaultBendRangeMpe, {}, SettingUnits::SEMITONES),
};

// ---------------------------------------------------------------------------------------------
// AUDIO_CLIP (on DocAudioClip)

constexpr SettingInfo kAudioClipTable[] = {
    // A row's range is int32: a marker past 2^31 frames (an 8-bit file over 2 GB) is refused, though the record holds
    // 64 bits.
    integer("startSamplePos", 0, INT32_MAX),
    integer("endSamplePos", 0, INT32_MAX),
    integer("transpose", -96, 96, 0, {}, SettingUnits::SEMITONES),
    integer("cents", -50, 50, 0, {}, SettingUnits::CENTS),
    boolean("linearInterpolation"),
    boolean("pitchSpeedIndependent", 1),
    boolean("reversed"),
    integer("attack", INT32_MIN, INT32_MAX, INT32_MIN),
    integer("priority", 0, 2, 1),
    boolean("overdubsShouldCloneAudioTrack", 1),
};

std::optional<Value> read_audio_clip(const DocAudioClip& c, std::string_view path) {
	if (path == "startSamplePos") {
		return number(static_cast<int64_t>(c.start_pos));
	}
	if (path == "endSamplePos") {
		return number(static_cast<int64_t>(c.end_pos));
	}
	if (path == "transpose") {
		return number(c.transpose);
	}
	if (path == "cents") {
		return number(c.cents);
	}
	if (path == "linearInterpolation") {
		return number(c.interpolation_mode == engine::InterpolationMode::LINEAR ? 1 : 0);
	}
	if (path == "pitchSpeedIndependent") {
		return number(c.pitch_and_speed_independent ? 1 : 0);
	}
	if (path == "reversed") {
		return number(c.reversed ? 1 : 0);
	}
	if (path == "attack") {
		return number(c.attack);
	}
	if (path == "priority") {
		return number(std::to_underlying(c.voice_priority));
	}
	if (path == "overdubsShouldCloneAudioTrack") {
		return number(c.overdubs_should_clone ? 1 : 0);
	}
	return std::nullopt;
}

void write_audio_clip(DocAudioClip& c, std::string_view path, int32_t v) {
	if (path == "startSamplePos") {
		c.start_pos = static_cast<uint64_t>(v);
	}
	else if (path == "endSamplePos") {
		c.end_pos = static_cast<uint64_t>(v);
	}
	else if (path == "transpose") {
		c.transpose = static_cast<int16_t>(v);
	}
	else if (path == "cents") {
		c.cents = static_cast<int8_t>(v);
	}
	else if (path == "linearInterpolation") {
		c.interpolation_mode = v != 0 ? engine::InterpolationMode::LINEAR : engine::InterpolationMode::SMOOTH;
	}
	else if (path == "pitchSpeedIndependent") {
		c.pitch_and_speed_independent = v != 0;
	}
	else if (path == "reversed") {
		c.reversed = v != 0;
	}
	else if (path == "attack") {
		c.attack = v;
	}
	else if (path == "priority") {
		c.voice_priority = static_cast<engine::VoicePriority>(v);
	}
	else if (path == "overdubsShouldCloneAudioTrack") {
		c.overdubs_should_clone = v != 0;
	}
}

} // namespace

std::span<const SettingInfo> table_of(SettingGroupId group) {
	switch (group) {
	case SettingGroupId::SONG:
		return song_table();
	case SettingGroupId::TRACK:
		return kTrackTable;
	case SettingGroupId::INSTRUMENT_DEVICE:
		return kInstrumentDeviceTable;
	case SettingGroupId::LANE:
		return kLaneTable;
	case SettingGroupId::NOTE_ROW:
		return kNoteRowTable;
	case SettingGroupId::AUDIO_CLIP:
		return kAudioClipTable;
	case SettingGroupId::CLIP:
		return kClipTable;
	case SettingGroupId::CLIP_VALUES:
		return kClipValuesTable;
	case SettingGroupId::AUDIO_INPUT:
		return kAudioInputTable;
	}
	return {};
}

std::optional<Value> read_setting(Song& song, const SettingAddress& address, std::string_view path) {
	if (engine::find_setting(table_of(address.group), path) == nullptr) {
		return std::nullopt;
	}
	switch (address.group) {
	case SettingGroupId::SONG:
		return read_song(song, path);
	case SettingGroupId::TRACK:
	case SettingGroupId::INSTRUMENT_DEVICE: {
		const auto* track = std::get_if<TrackAddress>(&address.owner);
		const DocOutput* output = track != nullptr ? song.output_structure.find(track->output_id) : nullptr;
		return output != nullptr ? read_output(*output, address.group, path) : std::nullopt;
	}
	case SettingGroupId::AUDIO_INPUT: {
		const auto* track = std::get_if<TrackAddress>(&address.owner);
		const DocOutput* output = track != nullptr ? song.output_structure.find(track->output_id) : nullptr;
		return output != nullptr ? read_audio_input(*output, path) : std::nullopt;
	}
	case SettingGroupId::LANE: {
		const auto* lane = std::get_if<LaneAddress>(&address.owner);
		const DocDrum* drum = lane != nullptr ? song.drum_structure.find(lane->drum_id) : nullptr;
		return drum != nullptr ? std::optional<Value>{Value::makeScalar(drum->name)} : std::nullopt;
	}
	case SettingGroupId::NOTE_ROW: {
		const auto* row_address = std::get_if<NoteRowAddress>(&address.owner);
		const DocNoteRow* row = row_address != nullptr ? resolve_note_row(song, *row_address) : nullptr;
		if (row == nullptr) {
			return std::nullopt;
		}
		if (path == "bendRange") {
			return number(row->bend_range.value_or(kDefaultBendRangeMpe));
		}
		return Value::makeScalar(std::string{to_string(row->sequence_direction)});
	}
	case SettingGroupId::AUDIO_CLIP: {
		const auto* clip = std::get_if<ClipAddress>(&address.owner);
		const DocAudioClip* record = clip != nullptr ? song.audio_clip_structure.find(clip->clip_id) : nullptr;
		return record != nullptr ? read_audio_clip(*record, path) : std::nullopt;
	}
	case SettingGroupId::CLIP:
	case SettingGroupId::CLIP_VALUES: {
		const auto* clip = std::get_if<ClipAddress>(&address.owner);
		const DocClip* record = clip != nullptr ? song.clip_structure.find(clip->clip_id) : nullptr;
		if (record == nullptr) {
			return std::nullopt;
		}
		if (address.group == SettingGroupId::CLIP_VALUES) {
			return number(resolve(record->values, path, engine::find_setting(kClipValuesTable, path)->default_value));
		}
		if (path == "name") {
			return Value::makeScalar(record->name);
		}
		return Value::makeScalar(std::string{to_string(record->sequence_direction)});
	}
	}
	return std::nullopt;
}

bool write_setting(Song& song, const SettingAddress& address, std::string_view path, const Value& value) {
	const std::string* text_value = serialization::text_of(&value);
	if (text_value == nullptr || !engine::check_setting_text(table_of(address.group), path, *text_value)) {
		return false;
	}
	const int32_t v = integer_of(value).value_or(0);
	switch (address.group) {
	case SettingGroupId::SONG:
		if (!std::holds_alternative<SongAddress>(address.owner)) {
			return false;
		}
		write_song(song, path, v);
		return true;
	case SettingGroupId::TRACK:
	case SettingGroupId::INSTRUMENT_DEVICE: {
		const auto* track = std::get_if<TrackAddress>(&address.owner);
		DocOutput* output = track != nullptr ? song.output_structure.find(track->output_id) : nullptr;
		if (output == nullptr) {
			return false;
		}
		write_output(*output, path, value);
		return true;
	}
	case SettingGroupId::AUDIO_INPUT: {
		const auto* track = std::get_if<TrackAddress>(&address.owner);
		DocOutput* output = track != nullptr ? song.output_structure.find(track->output_id) : nullptr;
		if (output == nullptr) {
			return false;
		}
		write_audio_input(*output, path, value);
		return true;
	}
	case SettingGroupId::LANE: {
		const auto* lane = std::get_if<LaneAddress>(&address.owner);
		DocDrum* drum = lane != nullptr ? song.drum_structure.find(lane->drum_id) : nullptr;
		if (drum == nullptr) {
			return false;
		}
		drum->name = value.str();
		return true;
	}
	case SettingGroupId::NOTE_ROW: {
		const auto* row_address = std::get_if<NoteRowAddress>(&address.owner);
		DocNoteRow* row = row_address != nullptr ? resolve_note_row(song, *row_address) : nullptr;
		if (row == nullptr) {
			return false;
		}
		if (path == "bendRange") {
			row->bend_range = v;
			return true;
		}
		const std::optional<SequenceDirection> direction = parse_sequence_direction(value.str());
		if (!direction) {
			return false;
		}
		row->sequence_direction = *direction;
		return true;
	}
	case SettingGroupId::AUDIO_CLIP: {
		const auto* clip = std::get_if<ClipAddress>(&address.owner);
		DocAudioClip* record = clip != nullptr ? song.audio_clip_structure.find(clip->clip_id) : nullptr;
		if (record == nullptr) {
			return false;
		}
		write_audio_clip(*record, path, v);
		return true;
	}
	case SettingGroupId::CLIP:
	case SettingGroupId::CLIP_VALUES: {
		const auto* clip = std::get_if<ClipAddress>(&address.owner);
		DocClip* record = clip != nullptr ? song.clip_structure.find(clip->clip_id) : nullptr;
		if (record == nullptr) {
			return false;
		}
		if (address.group == SettingGroupId::CLIP_VALUES) {
			*value_at(record->values, path) = v;
			return true;
		}
		if (path == "name") {
			record->name = value.str();
			return true;
		}
		const std::optional<SequenceDirection> direction = parse_sequence_direction(value.str());
		if (!direction) {
			return false;
		}
		record->sequence_direction = *direction;
		return true;
	}
	}
	return false;
}

} // namespace deluge::project
