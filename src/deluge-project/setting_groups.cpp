#include "deluge-project/setting_group.h"

#include "deluge-project/arp_record.h"
#include "deluge-project/audio_input.h"

#include "deluge-project/fraction.h"
#include "deluge-project/sequence_direction.h"
#include "deluge-project/song.h"
#include "engine-api/types/arp.h"
#include "engine-api/types/rate_tables.h"
#include "engine-api/types/sync.h"
#include "engine-api/types/voice_priority.h"
#include "serialization/tree.h"

#include <algorithm>
#include <array>
#include <climits>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace deluge::project {

namespace {
/// The menu-time tables: read when a menu or an Edit looks a setting up, never by the audio tier or an interrupt.
/// The device image places this namespace's data in SDRAM (linker/sdram_sections.x matches its name).
inline namespace menu_tables {

using engine::SettingInfo;
using engine::SettingKind;
using engine::SettingUnits;
using engine::UserScale;
using serialization::Value;

/// A fraction setting a menu shows as 0..50 (`kMaxMenuValue`), stored as the file's Q31.
constexpr UserScale kFiftyths{.user_min = 0, .user_max = 50, .stored_min = 0, .stored_max = INT32_MAX};

constexpr SettingInfo integer(std::string_view path, int32_t min, int32_t max, int32_t default_value = 0,
                              UserScale user = {}, SettingUnits units = SettingUnits::NONE,
                              const engine::SettingDescriptorExtras* extras = nullptr) {
	return SettingInfo{.path = path,
	                   .kind = SettingKind::INTEGER,
	                   .min = min,
	                   .max = max,
	                   .user = user,
	                   .default_value = default_value,
	                   .units = units,
	                   .extras = extras};
}

/// A fiftieths menu over a fraction the record holds as a float: it rounds the float, and stores through the scale.
constexpr engine::SettingDescriptorExtras kFloatFractionExtras{
    .mapping = {.kind = engine::DisplayMappingKind::FLOAT_FRACTION}};
/// A param-style menu over a signed Q31, 0..50.
constexpr engine::SettingDescriptorExtras kStandardExtras{.mapping = {.kind = engine::DisplayMappingKind::STANDARD}};
constexpr SettingInfo boolean(std::string_view path, int32_t default_value = 0,
                              const engine::SettingDescriptorExtras* extras = nullptr) {
	return SettingInfo{.path = path, .kind = SettingKind::BOOL, .default_value = default_value, .extras = extras};
}
constexpr SettingInfo text(std::string_view path, const engine::SettingDescriptorExtras* extras = nullptr) {
	return SettingInfo{.path = path, .kind = SettingKind::TEXT, .extras = extras};
}

using Instrument = engine::SettingContext::Instrument;
/// A MIDI track: what has programs, a Y-axis CC, MPE collapsing and a device definition.
constexpr std::array<int32_t, 1> kMidiInstrument{std::to_underlying(Instrument::MIDI)};
constexpr std::array kOnMidi{engine::instrument_is(kMidiInstrument)};
constexpr engine::SettingDescriptorExtras kOnMidiExtras{.applies_when = kOnMidi};
/// A synth, MIDI or CV track: its clip bends and plays in a direction as a whole (a kit's rows have their own bend
/// and direction, and an audio clip neither).
constexpr std::array<int32_t, 3> kMelodicTracks{
    std::to_underlying(Instrument::SYNTH), std::to_underlying(Instrument::MIDI), std::to_underlying(Instrument::CV)};
constexpr std::array kOnMelodicTrack{engine::instrument_is(kMelodicTracks)};
constexpr engine::SettingDescriptorExtras kOnMelodicTrackExtras{.applies_when = kOnMelodicTrack};

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

/// The reverb models with filters of their own: mutable and digital.
constexpr std::array<int32_t, 2> kFilteredReverbModels{std::to_underlying(ReverbModel::MUTABLE),
                                                       std::to_underlying(ReverbModel::DIGITAL)};
constexpr std::array kReverbHasFilters{engine::setting_is("reverb/model", kFilteredReverbModels)};
constexpr engine::SettingDescriptorExtras kReverbFilterExtras{
    .mapping = {.kind = engine::DisplayMappingKind::FLOAT_FRACTION}, .applies_when = kReverbHasFilters};
/// The reverb sidechain's menus show while it ducks at a set volume: a negative volume is automatic (the menu's -1
/// stores -21474836, a song's default another negative). Its attack and release menus are a step of the sidechain's
/// rate table, the stored rate the entry shifted.
constexpr std::array<int32_t, 1> kSetVolume{0};
constexpr std::array kReverbDucksAtSetVolume{engine::setting_at_least("reverb/compressor/volume", kSetVolume)};
constexpr engine::SettingDescriptorExtras kReverbSidechainAttackExtras{
    .mapping = {.kind = engine::DisplayMappingKind::TABLE, .table_shift = 2, .table = engine::kSidechainAttackRates},
    .applies_when = kReverbDucksAtSetVolume};
constexpr engine::SettingDescriptorExtras kReverbSidechainReleaseExtras{
    .mapping = {.kind = engine::DisplayMappingKind::TABLE, .table_shift = 3, .table = engine::kSidechainReleaseRates},
    .applies_when = kReverbDucksAtSetVolume};
constexpr engine::SettingDescriptorExtras kReverbSidechainShapeExtras{
    .mapping = {.kind = engine::DisplayMappingKind::STANDARD}, .applies_when = kReverbDucksAtSetVolume};
constexpr engine::SettingDescriptorExtras kReverbSidechainSyncExtras{.applies_when = kReverbDucksAtSetVolume};
/// The reverb's pan menu, −25..25.
constexpr engine::SettingDescriptorExtras kPanExtras{.mapping = {.kind = engine::DisplayMappingKind::PAN}};

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
		    integer("reverb/roomSize", 0, INT32_MAX, q31_of(30.0f / 50), kFiftyths, SettingUnits::NONE,
		            &kFloatFractionExtras),
		    integer("reverb/dampening", 0, INT32_MAX, q31_of(36.0f / 50), kFiftyths, SettingUnits::NONE,
		            &kFloatFractionExtras),
		    integer("reverb/width", 0, INT32_MAX, INT32_MAX, kFiftyths, SettingUnits::NONE, &kFloatFractionExtras),
		    integer("reverb/hpf", 0, INT32_MAX, 0, kFiftyths, SettingUnits::NONE, &kReverbFilterExtras),
		    integer("reverb/lpf", 0, INT32_MAX, INT32_MAX, kFiftyths, SettingUnits::NONE, &kReverbFilterExtras),
		    SettingInfo{.path = "reverb/pan",
		                .kind = SettingKind::INTEGER,
		                .min = INT32_MIN,
		                .max = INT32_MAX,
		                .extras = &kPanExtras},
		    integer("reverb/model", 0, 2, 1),
		    SettingInfo{.path = "reverb/compressor/attack",
		                .kind = SettingKind::INTEGER,
		                .min = 0,
		                .max = INT32_MAX,
		                .extras = &kReverbSidechainAttackExtras},
		    SettingInfo{.path = "reverb/compressor/release",
		                .kind = SettingKind::INTEGER,
		                .min = 0,
		                .max = INT32_MAX,
		                .extras = &kReverbSidechainReleaseExtras},
		    // The volume (with -1 = automatic) truncates onto its menu; its menu keeps that mapping.
		    integer("reverb/compressor/volume", INT32_MIN, INT32_MAX),
		    integer("reverb/compressor/shape", INT32_MIN, INT32_MAX, -601295438, {}, SettingUnits::NONE,
		            &kReverbSidechainShapeExtras),
		    integer("reverb/compressor/syncLevel", 0, engine::kMaxSyncLevel, 0, {}, SettingUnits::SYNC_LEVEL,
		            &kReverbSidechainSyncExtras),
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

/// The CV mode each of the second channel's menu steps stores: off, then the modes after pitch, which the menu skips.
constexpr std::array<int32_t, 4> kCv2SourceModes{0, 2, 3, 4};
/// The CV instrument's channel word for both channels: the second channel is free for a source only then.
constexpr std::array<int32_t, 1> kBothCvChannels{2};
constexpr std::array<int32_t, 1> kCvInstrument{std::to_underlying(Instrument::CV)};
constexpr std::array kCv2SourceConditions{engine::instrument_is(kCvInstrument),
                                          engine::setting_is("channel", kBothCvChannels)};
/// A stored pitch mode shows as the first step, off.
constexpr engine::SettingDescriptorExtras kCv2SourceExtras{
    .mapping = {.kind = engine::DisplayMappingKind::TABLE, .table = kCv2SourceModes},
    .applies_when = kCv2SourceConditions};

constexpr SettingInfo kInstrumentDeviceTable[] = {
    integer("channel", 0, kMaxChannelWord),
    integer("channelSuffix", -1, 25, -1),
    text("deviceDefinitionFileName", &kOnMidiExtras),
    SettingInfo{.path = "outputMpeY", .kind = SettingKind::CHOICE, .choices = kMpeYChoices, .extras = &kOnMidiExtras},
    boolean("collapseAftertouch", 0, &kOnMidiExtras),
    boolean("collapseMpe", 1, &kOnMidiExtras),
    integer("cv2Source", 0, 4, 0, {}, SettingUnits::NONE, &kCv2SourceExtras),
};
static_assert(engine::conditions_are_valid(kInstrumentDeviceTable));
static_assert(engine::conditions_are_valid(kTrackTable));

constexpr std::string_view kCcLabelPrefix = "ccLabels/";
/// The CC numbers a MIDI instrument can label: 0..119.
constexpr int32_t kNumCcLabels = 120;

/// The instrument-device table: the fixed rows, the hide flag, then one `ccLabels/<cc>` row per CC. The label paths are
/// built once, so the table's views stay valid for the program's life.
std::span<const SettingInfo> instrument_device_table() {
	static const std::vector<std::string> label_paths = [] {
		std::vector<std::string> paths;
		for (int32_t cc = 0; cc < kNumCcLabels; cc++) {
			paths.push_back(std::string{kCcLabelPrefix} + std::to_string(cc));
		}
		return paths;
	}();
	static const std::vector<SettingInfo> rows = [] {
		std::vector<SettingInfo> out(std::begin(kInstrumentDeviceTable), std::end(kInstrumentDeviceTable));
		out.push_back(boolean("hideUnlabeledCC", 0, &kOnMidiExtras));
		for (const std::string& path : label_paths) {
			out.push_back(text(path));
		}
		return out;
	}();
	return rows;
}

/// The CC a `ccLabels/<cc>` path names, or nullopt.
std::optional<uint8_t> cc_label_of(std::string_view path) {
	if (!path.starts_with(kCcLabelPrefix)) {
		return std::nullopt;
	}
	const std::optional<int64_t> cc = engine::whole_number(path.substr(kCcLabelPrefix.size()));
	return cc && *cc >= 0 && *cc < kNumCcLabels ? std::optional<uint8_t>{static_cast<uint8_t>(*cc)} : std::nullopt;
}

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
	if (path == "hideUnlabeledCC") {
		return number(o.hide_unlabeled_cc ? 1 : 0);
	}
	if (const std::optional<uint8_t> cc = cc_label_of(path)) {
		const auto label = o.cc_labels.find(*cc);
		return Value::makeScalar(label != o.cc_labels.end() ? label->second : std::string{});
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
	else if (path == "hideUnlabeledCC") {
		o.hide_unlabeled_cc = v != 0;
	}
	else if (const std::optional<uint8_t> cc = cc_label_of(path)) {
		if (text_value.empty()) {
			o.cc_labels.erase(*cc);
		}
		else {
			o.cc_labels[*cc] = text_value;
		}
	}
}

// ---------------------------------------------------------------------------------------------
// LANE (on DocDrum)

constexpr SettingInfo kLaneTable[] = {text("name")};

// ---------------------------------------------------------------------------------------------
// NOTE_ROW (on DocNoteRow)

constexpr std::string_view kDirectionChoices[] = {"forward", "reverse", "pingpong", "obeyParent"};
/// A clip has no parent direction to obey: its menu lists the first three.
constexpr std::string_view kClipDirectionChoices[] = {"forward", "reverse", "pingpong"};
/// A bend range's semitones, as its menu offers them.
constexpr int32_t kMaxBendRange = 96;
/// The factory bend ranges: a whole clip's, and a finger's (MPE).
constexpr int32_t kDefaultBendRange = 2;
constexpr int32_t kDefaultBendRangeMpe = 48;

/// A sound kit row's bend: the MIDI and gate rows' menus have none.
constexpr std::array<int32_t, 1> kSoundDrum{std::to_underlying(Instrument::SOUND_DRUM)};
constexpr std::array kOnSoundDrum{engine::instrument_is(kSoundDrum)};
constexpr engine::SettingDescriptorExtras kOnSoundDrumExtras{.applies_when = kOnSoundDrum};

constexpr SettingInfo kNoteRowTable[] = {
    SettingInfo{.path = "sequenceDirection", .kind = SettingKind::CHOICE, .choices = kDirectionChoices},
    // A kit row's finger bend; a melodic row has none of its own.
    integer("bendRange", 0, kMaxBendRange, kDefaultBendRangeMpe, {}, SettingUnits::SEMITONES, &kOnSoundDrumExtras),
};
static_assert(engine::conditions_are_valid(kNoteRowTable));
static_assert(engine::conditions_are_valid(kLaneTable));

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

/// The track an input records from shows only while the input records a specific track.
constexpr std::array<int32_t, 1> kSpecificTrackChannel{kInputChannelSpecificTrack};
constexpr std::array kRecordsATrack{engine::setting_is("inputChannel", kSpecificTrackChannel)};
constexpr engine::SettingDescriptorExtras kRecordingFromExtras{.applies_when = kRecordsATrack};

constexpr SettingInfo kAudioInputTable[] = {
    SettingInfo{.path = "mode", .kind = SettingKind::CHOICE, .choices = kAudioModeWords},
    SettingInfo{.path = "inputChannel", .kind = SettingKind::CHOICE, .choices = kInputChannelWords, .default_value = 1},
    // Another track's durable id; -1 = none.
    integer("outputRecordingFrom", -1, INT32_MAX, -1, {}, SettingUnits::NONE, &kRecordingFromExtras),
};
static_assert(engine::conditions_are_valid(kAudioInputTable));

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
    SettingInfo{.path = "sequenceDirection",
                .kind = SettingKind::CHOICE,
                .choices = kClipDirectionChoices,
                .extras = &kOnMelodicTrackExtras},
};
static_assert(engine::conditions_are_valid(kClipTable));

/// MIDI program numbers 0..127; 128 is none (no message sent).
constexpr int32_t kNoMidiProgram = 128;

constexpr SettingInfo kClipValuesTable[] = {
    integer("midiBank", 0, kNoMidiProgram, kNoMidiProgram, {}, SettingUnits::NONE, &kOnMidiExtras),
    integer("midiSub", 0, kNoMidiProgram, kNoMidiProgram, {}, SettingUnits::NONE, &kOnMidiExtras),
    integer("midiPGM", 0, kNoMidiProgram, kNoMidiProgram, {}, SettingUnits::NONE, &kOnMidiExtras),
    integer("bendRange", 0, kMaxBendRange, kDefaultBendRange, {}, SettingUnits::SEMITONES, &kOnMelodicTrackExtras),
    integer("bendRangeMPE", 0, kMaxBendRange, kDefaultBendRangeMpe, {}, SettingUnits::SEMITONES,
            &kOnMelodicTrackExtras),
};
static_assert(engine::conditions_are_valid(kClipValuesTable));

// ---------------------------------------------------------------------------------------------
// AUDIO_CLIP (on DocAudioClip)

/// The transpose menu: one value in cents over the semitones and the cents row.
constexpr engine::SettingDescriptorExtras kTransposeExtras{
    .mapping = {.kind = engine::DisplayMappingKind::TRANSPOSE_CENTS, .partner = "cents"}};
/// The interpolation menu lists linear first: menu 0 stores 1.
constexpr engine::SettingDescriptorExtras kInterpolationExtras{
    .mapping = {.kind = engine::DisplayMappingKind::INVERTED_BOOL}};

constexpr SettingInfo kAudioClipTable[] = {
    // A row's range is int32: a marker past 2^31 frames (an 8-bit file over 2 GB) is refused, though the record holds
    // 64 bits.
    integer("startSamplePos", 0, INT32_MAX),
    integer("endSamplePos", 0, INT32_MAX),
    SettingInfo{.path = "transpose",
                .kind = SettingKind::INTEGER,
                .min = -96,
                .max = 96,
                .units = SettingUnits::CENTS,
                .extras = &kTransposeExtras},
    integer("cents", -50, 50, 0, {}, SettingUnits::CENTS),
    SettingInfo{.path = "linearInterpolation", .kind = SettingKind::BOOL, .extras = &kInterpolationExtras},
    boolean("pitchSpeedIndependent", 1),
    boolean("reversed"),
    integer("attack", INT32_MIN, INT32_MAX, INT32_MIN, {}, SettingUnits::NONE, &kStandardExtras),
    SettingInfo{.path = "priority",
                .kind = SettingKind::CHOICE,
                .choices = engine::kVoicePriorityWords,
                .default_value = std::to_underlying(engine::VoicePriority::MEDIUM)},
    boolean("overdubsShouldCloneAudioTrack", 1),
};
static_assert(engine::conditions_are_valid(kAudioClipTable));

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

// ---------------------------------------------------------------------------------------------
// CLIP_ARP (in DocClip's value layer) and LANE_ARP (on DocDrum)

bool is_arp_group(SettingGroupId group) {
	return group == SettingGroupId::CLIP_ARP || group == SettingGroupId::LANE_ARP;
}

/// The slot of the arp record @p address names (empty until one is recorded); nullptr when the owner does not resolve.
std::optional<ArpRecord>* arp_record_of(Song& song, const SettingAddress& address) {
	if (address.group == SettingGroupId::CLIP_ARP) {
		const auto* clip = std::get_if<ClipAddress>(&address.owner);
		DocClip* record = clip != nullptr ? song.clip_structure.find(clip->clip_id) : nullptr;
		return record != nullptr ? &record->values.arp : nullptr;
	}
	const auto* lane = std::get_if<LaneAddress>(&address.owner);
	DocDrum* drum = lane != nullptr ? song.drum_structure.find(lane->drum_id) : nullptr;
	return drum != nullptr ? &drum->arp : nullptr;
}

Value word(std::span<const std::string_view> words, size_t index) {
	return Value::makeScalar(std::string{index < words.size() ? words[index] : words.front()});
}

std::optional<Value> read_arp(const ArpRecord& record, std::string_view path) {
	if (path == "arpeggiator/preset") {
		return word(engine::kArpPresetWords, preset_of(record));
	}
	if (const engine::SettingInfo* host = engine::find_setting(engine::kArpHostRows, path)) {
		return number(host_value(record, path).value_or(host->default_value));
	}
	if (const bool* flag = arp_flag_at(record, path)) {
		return number(*flag ? 1 : 0);
	}
	const uint8_t* field = arp_field_at(record, path);
	if (field == nullptr) {
		return std::nullopt;
	}
	const SettingInfo* row = engine::find_setting(engine::kArpSettingRows, path);
	if (row->kind == SettingKind::CHOICE && path != "arpeggiator/syncType") {
		return word(row->choices, *field);
	}
	return number(*field);
}

void write_arp(ArpRecord& record, std::string_view path, const Value& value) {
	const SettingInfo* row = engine::find_setting(engine::kArpSettingRows, path);
	const auto index = [&] {
		return static_cast<uint8_t>(std::ranges::find(row->choices, value.str()) - row->choices.begin());
	};
	if (path == "arpeggiator/preset") {
		const std::optional<engine::ArpPresetFields> fields = engine::arp_preset_fields(index());
		record.mode = fields->mode;
		record.octave_mode = fields->octave_mode.value_or(record.octave_mode);
		record.note_mode = fields->note_mode.value_or(record.note_mode);
		return;
	}
	const int32_t v = integer_of(value).value_or(0);
	if (host_index(path)) {
		set_host_value(record, path, v);
	}
	else if (bool* flag = arp_flag_at(record, path)) {
		*flag = v != 0;
	}
	else if (uint8_t* field = arp_field_at(record, path)) {
		*field = row->kind == SettingKind::CHOICE && path != "arpeggiator/syncType" ? index() : static_cast<uint8_t>(v);
	}
}

} // namespace menu_tables
} // namespace

SettingRecordSnapshot snapshot_setting_record(Song& song, const SettingAddress& address) {
	if (!is_arp_group(address.group)) {
		return {};
	}
	const std::optional<ArpRecord>* record = arp_record_of(song, address);
	return record != nullptr ? SettingRecordSnapshot{.arp = *record} : SettingRecordSnapshot{};
}

bool restore_setting_record(Song& song, const SettingAddress& address, const SettingRecordSnapshot& snapshot) {
	if (!snapshot.arp || !is_arp_group(address.group)) {
		return false;
	}
	std::optional<ArpRecord>* record = arp_record_of(song, address);
	if (record == nullptr) {
		return false;
	}
	*record = *snapshot.arp;
	return true;
}

bool is_derived_setting(SettingGroupId group, std::string_view path) {
	return is_arp_group(group) && path == "arpeggiator/preset";
}

std::optional<Value> arp_setting_value(const ArpRecord& record, std::string_view path) {
	return read_arp(record, path);
}

std::span<const SettingInfo> table_of(SettingGroupId group) {
	switch (group) {
	case SettingGroupId::SONG:
		return song_table();
	case SettingGroupId::TRACK:
		return kTrackTable;
	case SettingGroupId::INSTRUMENT_DEVICE:
		return instrument_device_table();
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
	case SettingGroupId::CLIP_ARP:
	case SettingGroupId::LANE_ARP:
		return engine::kArpRows;
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
	case SettingGroupId::CLIP_ARP:
	case SettingGroupId::LANE_ARP: {
		const std::optional<ArpRecord>* record = arp_record_of(song, address);
		return record != nullptr ? read_arp(record->value_or(ArpRecord{}), path) : std::nullopt;
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
	case SettingGroupId::CLIP_ARP:
	case SettingGroupId::LANE_ARP: {
		std::optional<ArpRecord>* record = arp_record_of(song, address);
		if (record == nullptr) {
			return false;
		}
		write_arp(record->has_value() ? **record : record->emplace(), path, value);
		return true;
	}
	}
	return false;
}

namespace {

/// The slot of @p address's own value at @p path when it may be left to the track, or nullptr.
std::optional<int32_t>* own_slot(Song& song, const SettingAddress& address, std::string_view path) {
	if (address.group == SettingGroupId::CLIP_VALUES) {
		const auto* clip = std::get_if<ClipAddress>(&address.owner);
		DocClip* record = clip != nullptr ? song.clip_structure.find(clip->clip_id) : nullptr;
		return record != nullptr ? value_at(record->values, path) : nullptr;
	}
	if (address.group == SettingGroupId::NOTE_ROW && path == "bendRange") {
		const auto* row_address = std::get_if<NoteRowAddress>(&address.owner);
		DocNoteRow* row = row_address != nullptr ? resolve_note_row(song, *row_address) : nullptr;
		return row != nullptr ? &row->bend_range : nullptr;
	}
	return nullptr;
}

/// The arp record whose host value at @p path @p address names, or nullptr.
ArpRecord* arp_host_owner(Song& song, const SettingAddress& address, std::string_view path) {
	if (!is_arp_group(address.group) || !host_index(path)) {
		return nullptr;
	}
	std::optional<ArpRecord>* record = arp_record_of(song, address);
	return record != nullptr && record->has_value() ? &**record : nullptr;
}

bool leavable(const SettingAddress& address, std::string_view path) {
	return address.group == SettingGroupId::CLIP_VALUES
	       || (address.group == SettingGroupId::NOTE_ROW && path == "bendRange")
	       || (is_arp_group(address.group) && engine::find_setting(engine::kArpHostRows, path) != nullptr);
}

} // namespace

bool holds_setting(Song& song, const SettingAddress& address, std::string_view path) {
	if (!leavable(address, path)) {
		return read_setting(song, address, path).has_value();
	}
	if (is_arp_group(address.group)) {
		const ArpRecord* record = arp_host_owner(song, address, path);
		return record != nullptr && host_value(*record, path).has_value();
	}
	const std::optional<int32_t>* slot = own_slot(song, address, path);
	return slot != nullptr && slot->has_value();
}

void forget_setting(Song& song, const SettingAddress& address, std::string_view path) {
	if (ArpRecord* record = arp_host_owner(song, address, path)) {
		set_host_value(*record, path, std::nullopt);
	}
	else if (std::optional<int32_t>* slot = own_slot(song, address, path)) {
		slot->reset();
	}
}

} // namespace deluge::project
