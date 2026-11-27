#include "fakes.h"

#include "project/device_settings.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <tuple>

namespace deluge::ui::scenes::bind::test {

uint8_t FakeSoundFacts::loaded_sources(project::SlotAddress slot) const {
	asked.push_back(slot);
	const auto found = loaded.find(slot.id);
	return found != loaded.end() ? found->second : 0;
}

const instruments::classic::SoundState* FakeSoundFacts::sound_state(project::SlotAddress slot) const {
	const auto found = states.find(slot.id);
	return found != states.end() ? &found->second : nullptr;
}

bool FakeSoundFacts::kit_takes_whole_folder(project::SlotAddress slot) const {
	const auto found = whole_folder.find(slot.id);
	return found != whole_folder.end() && found->second;
}

std::optional<uint16_t> FakeSoundFacts::default_range(project::SlotAddress slot, uint8_t source) const {
	const auto found = default_ranges.find({slot.id, source});
	return found != default_ranges.end() ? std::optional{found->second} : std::nullopt;
}

std::optional<uint64_t> FakeSoundFacts::range_identity(project::SlotAddress slot, uint8_t source,
                                                       uint16_t range) const {
	const auto found = identities.find({slot.id, source, range});
	return found != identities.end() ? std::optional{found->second} : std::nullopt;
}

std::optional<uint16_t> FakeSoundFacts::take_range(const TakeTarget& /*target*/) const {
	return take;
}

CableList FakeSoundFacts::cables(const project::ParamAddress& /*owner*/) const {
	CableList list;
	for (const CableFact& cable : cable_list) {
		if (list.count < list.items.size()) {
			list.items[list.count++] = cable;
		}
	}
	return list;
}

std::optional<AudioClipFact> FakeClipFacts::audio_clip(project::ClipAddress clip) const {
	const auto found = audio_clips.find(clip.clip_id);
	return found != audio_clips.end() ? std::optional{found->second} : std::nullopt;
}

bool FakeClipFacts::clip_playing(project::ClipAddress clip) const {
	return playing_at.contains(clip.clip_id);
}

int32_t FakeClipFacts::clip_live_pos(project::ClipAddress clip) const {
	const auto found = playing_at.find(clip.clip_id);
	return found != playing_at.end() ? found->second : 0;
}

bool FakeClipFacts::clip_arrangement_only(project::ClipAddress clip) const {
	const auto found = arrangement_only.find(clip.clip_id);
	return found != arrangement_only.end() && found->second;
}

bool FakeClipFacts::clip_recording_linearly(project::ClipAddress clip) const {
	const auto found = recording_linearly.find(clip.clip_id);
	return found != recording_linearly.end() && found->second;
}

std::pair<int64_t, int64_t> FakeClipFacts::audio_clip_samples(project::ClipAddress clip, int32_t /*scroll*/,
                                                              int32_t /*zoom*/) const {
	return audio_clips.contains(clip.clip_id) ? samples : std::pair<int64_t, int64_t>{0, 0};
}

uint32_t FakeTrackFacts::chain_key(project::ChainAddress chain) {
	return chain.kind == project::ChainAddress::Kind::SONG ? UINT32_MAX : chain.id;
}

int32_t FakeTrackFacts::played_bend_range(const project::SettingAddress& owner, int32_t which) const {
	// As a set made for the owner plays: a kit row's finger range starts at the main one.
	return which == 0 || owner.group == project::SettingGroupId::NOTE_ROW ? bend_main : bend_mpe;
}

const effects::EffectStates* FakeTrackFacts::chain_effects(project::ChainAddress chain) const {
	const auto found = chains.find(chain_key(chain));
	return found != chains.end() ? &found->second : nullptr;
}

bool FakeControlFacts::allows_note_tails(project::SlotAddress slot,
                                         std::optional<project::ClipAddress> /*clip*/) const {
	const auto found = note_tails.find(slot.id);
	return found == note_tails.end() || found->second;
}

std::optional<int32_t> FakeControlFacts::param_value(const project::ParamAddress& address) const {
	for (const auto& [at, value] : param_values) {
		if (at.owner == address.owner && at.space == address.space && at.id == address.id
		    && at.clip_id == address.clip_id) {
			return value;
		}
	}
	return std::nullopt;
}

bool RecordingClipWrites::recolour_clip(project::ClipAddress clip, int32_t offset) {
	calls.push_back({.kind = Call::Kind::RECOLOUR, .clip = clip, .value = offset});
	return accept;
}

bool RecordingClipWrites::clear_clip(project::ClipAddress clip, bool clear_automation, bool clear_sequence_and_mpe) {
	calls.push_back({.kind = Call::Kind::CLEAR,
	                 .clip = clip,
	                 .clear_automation = clear_automation,
	                 .clear_sequence_and_mpe = clear_sequence_and_mpe});
	return accept;
}

bool RecordingClipWrites::move_audio_clip_marker(project::ClipAddress clip, std::string_view path, int64_t value) {
	calls.push_back({.kind = Call::Kind::MARKER, .clip = clip, .value = value, .path = std::string{path}});
	return accept;
}

bool RecordingClipWrites::resize_audio_clip(project::ClipAddress clip, ResizeKind kind, int32_t new_length,
                                            int32_t old_length) {
	calls.push_back(
	    {.kind = Call::Kind::RESIZE, .clip = clip, .value = new_length, .old_length = old_length, .resize = kind});
	return accept;
}

void RecordingClipWrites::set_undo_scroll_after(project::ClipAddress clip, int32_t x_scroll) {
	calls.push_back({.kind = Call::Kind::UNDO_SCROLL, .clip = clip, .value = x_scroll});
}

bool RecordingSoundWrites::write_ranges(project::SlotAddress slot, uint8_t source,
                                        std::span<const synth::SourceRange> ranges, RangesCause cause) {
	calls.push_back({.kind = Call::Kind::RANGES,
	                 .slot = slot,
	                 .source = source,
	                 .ranges = {ranges.begin(), ranges.end()},
	                 .cause = cause});
	return accept;
}

void RecordingSoundWrites::set_default_range(project::SlotAddress slot, uint8_t source, uint16_t range) {
	calls.push_back({.kind = Call::Kind::DEFAULT_RANGE, .slot = slot, .source = source, .range = range});
}

bool RecordingSoundWrites::insert_range(project::SlotAddress slot, uint8_t source, uint16_t at, int16_t top_note,
                                        std::optional<int16_t> previous_top) {
	calls.push_back({.kind = Call::Kind::INSERT_RANGE,
	                 .slot = slot,
	                 .source = source,
	                 .range = at,
	                 .value = top_note,
	                 .neighbour_top = previous_top});
	return accept;
}

bool RecordingSoundWrites::delete_range(project::SlotAddress slot, uint8_t source, uint16_t index,
                                        std::optional<int16_t> lower_top) {
	calls.push_back(
	    {.kind = Call::Kind::DELETE_RANGE, .slot = slot, .source = source, .range = index, .neighbour_top = lower_top});
	return accept;
}

bool RecordingSoundWrites::attach_recording(project::SlotAddress slot, uint8_t source, uint16_t range,
                                            std::string_view path) {
	calls.push_back(
	    {.kind = Call::Kind::ATTACH, .slot = slot, .source = source, .range = range, .text = std::string{path}});
	return accept;
}

bool RecordingSoundWrites::write_dx_patch(project::SlotAddress slot, const instruments::classic::DxPatchState& patch) {
	calls.push_back({.kind = Call::Kind::DX_PATCH, .slot = slot, .dx_patch = patch});
	return accept;
}

bool RecordingSoundWrites::load_dx_program(project::SlotAddress slot, int32_t program) {
	calls.push_back({.kind = Call::Kind::DX_PROGRAM, .slot = slot, .value = program});
	return accept;
}

bool RecordingSoundWrites::write_source_transpose(const Addresses& where, uint8_t source, std::optional<uint16_t> range,
                                                  int32_t menu_value) {
	calls.push_back(
	    {.kind = Call::Kind::TRANSPOSE, .source = source, .range = range, .value = menu_value, .where = where});
	return accept;
}

bool RecordingSoundWrites::velocity_to_level_everywhere(midi::DeviceId device) {
	calls.push_back({.kind = Call::Kind::VELOCITY_EVERYWHERE, .device = device});
	return accept;
}

bool RecordingLearnSink::learn(project::MappingTarget target, std::optional<project::MidiCommand> command,
                               std::optional<LearnInput> input) {
	calls.push_back(
	    {.kind = Call::Kind::LEARN, .target = std::move(target), .command = std::move(command), .input = input});
	return accept;
}

bool RecordingLearnSink::learn_knob(const project::OwnerParamTarget& target, midi::DeviceId device, int32_t which_knob,
                                    int32_t midi_channel) {
	calls.push_back({.kind = Call::Kind::LEARN_KNOB,
	                 .target = project::MappingTarget{target},
	                 .device = device,
	                 .which_knob = which_knob,
	                 .midi_channel = midi_channel});
	return accept;
}

namespace {

/// @brief Keep @p binding in @p kept under @p key, or forget @p key for nullopt; a binding naming a device @p devices
///        does not know keeps nothing.
template <typename Key>
void keep(const FakeMidiDevices& devices, std::map<Key, LearnedBinding>& kept, Key key,
          const std::optional<LearnedBinding>& binding) {
	if (!binding) {
		kept.erase(key);
		return;
	}
	if (binding->device && !devices.settings(*binding->device)) {
		return;
	}
	kept.insert_or_assign(key, *binding);
}

} // namespace

void RecordingLearnSink::learn_global(project::GlobalCommand command, std::optional<LearnedBinding> binding) {
	calls.push_back({.kind = Call::Kind::LEARN_GLOBAL, .global = command, .binding = binding});
	if (devices != nullptr) {
		keep(*devices, devices->globals, command, binding);
	}
}

void RecordingLearnSink::learn_follow(project::FollowInput input, std::optional<LearnedBinding> binding) {
	calls.push_back({.kind = Call::Kind::LEARN_FOLLOW, .follow = input, .binding = binding});
	if (devices != nullptr) {
		keep(*devices, devices->follows, input, binding);
	}
}

FakeDeviceSettings::FakeDeviceSettings() {
	for (const plugin::SettingInfo& row : project::device_table()) {
		std::array<char, 12> digits{};
		const std::optional<std::string_view> text = plugin::stored_text(row, row.default_value, digits);
		values.emplace(std::string{row.path}, row.kind == plugin::SettingKind::CHOICE && text
		                                          ? serialization::Value::makeScalar(std::string{*text})
		                                          : serialization::Value::makeNumber(row.default_value));
	}
}

serialization::Value FakeDeviceSettings::load(const plugin::SettingInfo& row) const {
	const auto found = values.find(row.path);
	return found != values.end() ? found->second : serialization::Value::makeNumber(row.default_value);
}

DeviceWrite FakeDeviceSettings::store(const plugin::SettingInfo& row, const serialization::Value& value) {
	stores.emplace_back(std::string{row.path}, value);
	if (refuse.contains(row.path)) {
		return DeviceWrite::REFUSED;
	}
	values.insert_or_assign(std::string{row.path}, value);
	return accept_restart.contains(row.path) ? DeviceWrite::STORED_FOR_RESTART : DeviceWrite::STORED;
}

std::string_view FakeMidiDevices::name(midi::DeviceId device) const {
	const auto at = names.find(device.value);
	return at != names.end() ? std::string_view{at->second} : std::string_view{};
}

std::optional<MidiDeviceSettings> FakeMidiDevices::settings(midi::DeviceId device) const {
	const auto at = kept.find(device.value);
	return at != kept.end() ? std::optional{at->second} : std::nullopt;
}

std::optional<project::MidiDeviceRef> FakeMidiDevices::reference(midi::DeviceId device) const {
	const auto at = refs.find(device.value);
	return at != refs.end() ? std::optional{at->second} : std::nullopt;
}

bool FakeMidiDevices::write(midi::DeviceId device, const MidiDeviceSettings& settings) {
	writes.emplace_back(device, settings);
	const auto at = kept.find(device.value);
	if (at == kept.end()) {
		return false;
	}
	at->second = settings;
	return true;
}

std::optional<LearnedBinding> FakeMidiDevices::global_command(project::GlobalCommand command) const {
	const auto at = globals.find(command);
	return at != globals.end() ? std::optional{at->second} : std::nullopt;
}

std::optional<LearnedBinding> FakeMidiDevices::follow_input(project::FollowInput input) const {
	const auto at = follows.find(input);
	return at != follows.end() ? std::optional{at->second} : std::nullopt;
}

void FakeMidiDevices::connect(midi::DeviceId device, std::string_view device_name) {
	list.push_back(device);
	names.insert_or_assign(device.value, std::string{device_name});
	kept.insert_or_assign(device.value, MidiDeviceSettings{});
	refs.insert_or_assign(device.value, project::MidiDeviceRef{.port = project::MidiDeviceRef::Port::HOSTED,
	                                                           .name = std::string{device_name}});
}

std::optional<uint8_t> RecordingMidiFeedback::cc_of(project::OwnerPlugin owner, project::ParamTarget target) const {
	const auto at = std::ranges::find_if(
	    ccs, [&](const auto& entry) { return std::get<0>(entry) == owner && std::get<1>(entry) == target; });
	return at != ccs.end() ? std::optional{std::get<2>(*at)} : std::nullopt;
}

void RecordingTransportActions::play() {
	calls.push_back({.kind = Call::Kind::PLAY});
	if (transport != nullptr) {
		transport->is_playing = !transport->is_playing;
	}
}

void RecordingTransportActions::toggle_metronome() {
	calls.push_back({.kind = Call::Kind::TOGGLE_METRONOME});
	if (transport != nullptr) {
		transport->metronome = !transport->metronome;
	}
}

bool RecordingWrites::write(const WriteRequest& request) {
	requests.push_back(request);
	return accept;
}

bool RecordingWrites::delete_automation(const project::ParamAddress& address) {
	deletes.push_back(address);
	return accept;
}

bool RecordingWrites::write_text(const Target& target, const Addresses& where, std::string_view text) {
	gestures.push_back({.kind = Gesture::Kind::TEXT, .target = target, .where = where, .text = std::string{text}});
	if (order != nullptr) {
		order->push_back("name");
	}
	return accept;
}

void RecordingWrites::set_default_audio_input_channel(int32_t channel) {
	gestures.push_back({.kind = Gesture::Kind::DEFAULT_INPUT_CHANNEL, .value = channel});
}

void RecordingWrites::set_song_threshold_recording(int32_t mode) {
	gestures.push_back({.kind = Gesture::Kind::THRESHOLD_RECORDING, .value = mode});
}

void RecordingWrites::set_reverb_sidechain_sync_type(plugin::SyncType type) {
	gestures.push_back({.kind = Gesture::Kind::SIDECHAIN_SYNC_TYPE, .value = std::to_underlying(type)});
}

bool RecordingControlsSink::write_param(const project::ParamAddress& address, int32_t value, int32_t pos,
                                        int32_t length, ui::controls::Resolution resolution) {
	param_writes.push_back({address, value, pos, length, resolution});
	return accept;
}

void RecordingControlsSink::learned_written(const project::ParamAddress& address, int32_t knob_pos) {
	learned_follows.emplace_back(address, knob_pos);
}

std::optional<uint32_t> RecordingControlsSink::take_for(uint32_t output_id) {
	take_requests.push_back(output_id);
	return take_answer;
}

bool RecordingControlsSink::make_param(const project::ParamAddress& address) {
	made.push_back(address);
	return accept;
}

void RecordingControlsSink::knob_absolute(const project::OwnerParamTarget& target, const project::MidiCommand& source) {
	absolutes.emplace_back(target, source);
}

bool RecordingControlsSink::knob_mode(project::KnobModeOwner owner, uint8_t mode) {
	gestures.push_back({.kind = Gesture::Kind::KNOB_MODE, .value = mode, .knob_owner = owner});
	return accept;
}

bool RecordingControlsSink::chain_view(project::ChainAddress chain, std::string_view key, std::string_view word) {
	gestures.push_back(
	    {.kind = Gesture::Kind::CHAIN_VIEW, .chain = chain, .key = std::string{key}, .text = std::string{word}});
	return accept;
}

bool RecordingControlsSink::write_controls(project::ControlsAddress owner,
                                           std::vector<project::ControllerMapping> controls) {
	gestures.push_back({.kind = Gesture::Kind::CONTROLS, .controls_address = owner, .controls = std::move(controls)});
	return accept;
}

bool RecordingControlsSink::stutter(const KnobFocus& focus, bool begin) {
	gestures.push_back({.kind = Gesture::Kind::STUTTER, .focus = focus, .begin = begin});
	return accept;
}

bool RecordingControlsSink::grab_region(const KnobFocus& focus, uint32_t pos) {
	gestures.push_back({.kind = Gesture::Kind::GRAB_REGION, .value = pos, .focus = focus});
	return accept;
}

bool RecordingControlsSink::write_effect(const EffectOwner& owner, std::string_view path,
                                         const serialization::Value& value) {
	gestures.push_back({.kind = Gesture::Kind::EFFECT_WORD,
	                    .key = std::string{path},
	                    .text = value.isScalar() ? value.str() : std::string{},
	                    .effect_owner = owner});
	return accept;
}

bool RecordingControlsSink::midi_cc_select(const KnobFocus& focus, int32_t knob, bool on) {
	gestures.push_back({.kind = Gesture::Kind::MIDI_CC_SELECT, .value = knob, .focus = focus, .begin = on});
	return accept;
}

bool RecordingControlsSink::delete_knob_automation(const project::ParamAddress& address) {
	gestures.push_back({.kind = Gesture::Kind::DELETE_KNOB_AUTOMATION, .address = address});
	return accept;
}

bool RecordingControlsSink::instrument_edited(const KnobFocus& focus) {
	gestures.push_back({.kind = Gesture::Kind::INSTRUMENT_EDITED, .focus = focus});
	return accept;
}

namespace {

/// @brief The first entry of @p waveforms at @p address, or null.
template <typename Entries>
auto* entry_at(Entries& waveforms, const WaveformAddress& address) {
	const auto found = std::ranges::find(waveforms, address, &FakeWaveforms::Entry::at);
	return found != waveforms.end() ? &*found : nullptr;
}

} // namespace

std::optional<WaveformInfo> FakeWaveforms::info(const WaveformAddress& address) const {
	const Entry* entry = entry_at(waveforms, address);
	return entry != nullptr ? std::optional{entry->info} : std::nullopt;
}

views::waveform::PeakRead FakeWaveforms::peaks(const WaveformAddress& address, views::waveform::Window at) const {
	reads.push_back({.at = address, .in = at});
	Entry* entry = entry_at(waveforms, address);
	if (entry == nullptr) {
		return views::waveform::PeakRead::all_past_end();
	}
	const bool pending_now = entry->pending_reads > 0;
	if (pending_now) {
		entry->pending_reads--;
	}
	const auto length = static_cast<int64_t>(entry->info.length_samples);
	// A take's columns end by its captured frames; a finished file's start before its length.
	const auto captured = static_cast<int64_t>(entry->captured.value_or(entry->info.length_samples));
	views::waveform::PeakRead read;
	for (int32_t c = 0; c < views::waveform::kColumns; c++) {
		const int64_t start = at.scroll + int64_t{c} * at.zoom;
		const bool past = entry->info.recording ? start + at.zoom > captured : start >= length;
		if (at.zoom < 1 || past || start + at.zoom <= 0) {
			read.set(c, views::waveform::ColumnRead::past_end());
		}
		else if (pending_now && entry->pending.test(static_cast<size_t>(c))) {
			read.set(c, views::waveform::ColumnRead::pending());
		}
		else {
			read.set(c, views::waveform::ColumnRead::of(entry->peaks[static_cast<size_t>(c)]));
		}
	}
	return read;
}

std::optional<int64_t> FakeWaveforms::play_position(const WaveformAddress& address) const {
	const Entry* entry = entry_at(waveforms, address);
	return entry != nullptr ? entry->position : std::nullopt;
}

bool RecordingTakeActions::begin(TakeChannels c) {
	calls.push_back(Kind::BEGIN);
	channels = c;
	if (order != nullptr) {
		order->push_back("begin");
	}
	if (begins && audio != nullptr) {
		audio->take = InputTake{.channels = static_cast<uint8_t>(c)};
	}
	return begins;
}

void RecordingTakeActions::finish() {
	calls.push_back(Kind::FINISH);
	if (audio != nullptr) {
		audio->take.reset();
		audio->take_path.reset();
	}
}

void RecordingTakeActions::orphan() {
	calls.push_back(Kind::ORPHAN);
	if (audio != nullptr) {
		audio->take.reset();
		audio->take_path.reset();
	}
}

} // namespace deluge::ui::scenes::bind::test
