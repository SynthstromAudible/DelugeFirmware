#include "fakes.h"

namespace deluge::scenes::bind::test {

uint8_t FakeSoundFacts::loaded_sources(project::SlotAddress slot) const {
	asked.push_back(slot);
	const auto found = loaded.find(slot.id);
	return found != loaded.end() ? found->second : 0;
}

const instrument::SoundState* FakeSoundFacts::sound_state(project::SlotAddress slot) const {
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
                                        std::span<const instrument::RangeState> ranges, RangesCause cause) {
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

bool RecordingSoundWrites::write_dx_patch(project::SlotAddress slot, uint8_t source,
                                          const instrument::DxPatchState& patch) {
	calls.push_back({.kind = Call::Kind::DX_PATCH, .slot = slot, .source = source, .dx_patch = patch});
	return accept;
}

bool RecordingSoundWrites::load_dx_program(project::SlotAddress slot, uint8_t source, int32_t program) {
	calls.push_back({.kind = Call::Kind::DX_PROGRAM, .slot = slot, .source = source, .value = program});
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

std::string_view FakeMidiDevices::name(midi::DeviceId device) const {
	const auto at = names.find(device.value);
	return at != names.end() ? std::string_view{at->second} : std::string_view{};
}

std::optional<MidiDeviceSettings> FakeMidiDevices::settings(midi::DeviceId device) const {
	const auto at = kept.find(device.value);
	return at != kept.end() ? std::optional{at->second} : std::nullopt;
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

} // namespace deluge::scenes::bind::test
