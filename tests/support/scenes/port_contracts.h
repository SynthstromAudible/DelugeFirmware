#pragma once

/// @file
/// @brief Cases every implementation of a port must pass, the fake and the host's alike. Each takes the port under
///        test and returns whether it holds; a spec states the case and expects true.

#include "scenes/bind/ports/ports.h"

namespace deluge::scenes::bind::contract {

/// @brief A port answers the same thing twice when nothing changed between the calls.
[[nodiscard]] inline bool ui_facts_are_stable(const UiFacts& ui) {
	return ui.auditioning() == ui.auditioning() && ui.popup_showing() == ui.popup_showing()
	       && ui.flash_cursor() == ui.flash_cursor() && ui.performance_editing_mode() == ui.performance_editing_mode()
	       && ui.dx7_engine_enabled() == ui.dx7_engine_enabled();
}

/// @brief The card's facts are stable across two calls. No case that the host and a fake both answer the same way
///        can fail on a correct host's idle state, so a falsifiable CardFacts case is the fake's alone (the unit spec
///        sets it loading and expects true).
[[nodiscard]] inline bool card_facts_are_stable(const CardFacts& card) {
	return card.sample_load_in_flight() == card.sample_load_in_flight()
	       && card.output_may_be_replaced() == card.output_may_be_replaced();
}

/// @brief The voice count is never negative.
[[nodiscard]] inline bool voice_count_is_a_count(const AudioFacts& audio) {
	return audio.live_voice_count() >= 0;
}

/// @brief The flash cursor is one of its three values.
[[nodiscard]] inline bool flash_cursor_is_named(const UiFacts& ui) {
	const auto c = ui.flash_cursor();
	return c == FlashCursor::Fast || c == FlashCursor::Off || c == FlashCursor::Slow;
}

/// @brief A slot no song holds has no state, no loaded source, no cables and no ranges.
[[nodiscard]] inline bool unknown_slot_is_empty(const SoundFacts& sound, project::SlotAddress nowhere) {
	const project::ParamAddress param{.owner = nowhere.kind == project::SlotAddress::Kind::DRUM
	                                               ? project::OwnerId::drum(nowhere.id)
	                                               : project::OwnerId::instrument(nowhere.id)};
	return sound.sound_state(nowhere) == nullptr && sound.cables(param).empty() && sound.loaded_sources(nowhere) == 0
	       && !sound.default_range(nowhere, 0).has_value() && !sound.range_identity(nowhere, 0, 0).has_value()
	       && !sound.kit_takes_whole_folder(nowhere);
}

/// @brief A clip no song holds is no audio clip, does not play, and measures nothing.
[[nodiscard]] inline bool unknown_clip_is_silent(const ClipFacts& clips, project::ClipAddress nowhere) {
	const std::pair<int64_t, int64_t> nothing{0, 0};
	return !clips.audio_clip(nowhere).has_value() && !clips.clip_playing(nowhere)
	       && !clips.clip_recording_linearly(nowhere) && clips.audio_clip_samples(nowhere, 0, 1) == nothing;
}

/// @brief A chain no song holds has no effects.
[[nodiscard]] inline bool unknown_chain_has_no_effects(const TrackFacts& tracks, project::ChainAddress nowhere) {
	return tracks.chain_effects(nowhere) == nullptr;
}

/// @brief Every clip edit on a clip no song holds is refused. Meaningful on the host, which refuses by resolution; a
///        recording fake refuses only with `accept` false, so set that first.
[[nodiscard]] inline bool unknown_clip_is_refused(ClipWrites& writes, project::ClipAddress nowhere) {
	return !writes.recolour_clip(nowhere, 1) && !writes.clear_clip(nowhere, true, true)
	       && !writes.move_audio_clip_marker(nowhere, "endSamplePos", 1)
	       && !writes.resize_audio_clip(nowhere, ResizeKind::NO_STRETCH, 96, 192);
}

/// @brief Every sound edit on a sound no song holds is refused. A fake that accepts everything is set to refuse first.
[[nodiscard]] inline bool unknown_sound_is_refused(SoundWrites& writes, project::SlotAddress nowhere) {
	const instrument::DxPatchState patch{};
	return !writes.write_ranges(nowhere, 0, {}, {}) && !writes.insert_range(nowhere, 0, 0, 40, std::nullopt)
	       && !writes.delete_range(nowhere, 0, 0, std::nullopt)
	       && !writes.attach_recording(nowhere, 0, 0, "NOWHERE.WAV") && !writes.write_dx_patch(nowhere, 0, patch)
	       && !writes.load_dx_program(nowhere, 0, 0)
	       && !writes.write_source_transpose(Addresses{.slot = nowhere}, 0, std::nullopt, 0);
}

/// @brief A velocity default for no device is refused: the host has no cable to read it from. A fake that accepts
///        everything is set to refuse first.
[[nodiscard]] inline bool velocity_of_no_device_is_refused(SoundWrites& writes) {
	return !writes.velocity_to_level_everywhere(midi::DeviceId{});
}

/// @brief A knob learned onto a param of an owner no song holds is refused, a MIDI knob's and a mod knob's alike, and
///        so is a learn of a MIDI command onto that param. A fake that accepts everything is set to refuse first.
[[nodiscard]] inline bool unknown_owner_knob_is_refused(LearnSink& learns, const project::OwnerParamTarget& nowhere) {
	return !learns.learn_knob(nowhere, midi::kDin, 74, 3) && !learns.learn_knob(nowhere, midi::DeviceId{}, 1, 255)
	       && !learns.learn(project::MappingTarget{nowhere},
	                        project::MidiCommand{.channel_or_zone = 1, .note_or_cc = 74}, std::nullopt);
}

/// @brief A menu write on a sound no song holds, and a text write and an automation delete on a clip no song holds,
///        are refused: the host refuses each by resolving its owner. A recording fake refuses only with `accept`
///        false, so set that first.
[[nodiscard]] inline bool unknown_slot_write_is_refused(WriteSink& writes, project::SlotAddress nowhere,
                                                        project::ClipAddress nowhere_clip) {
	const WriteRequest transpose{
	    .target = {.kind = Target::Kind::SOUND, .path = "osc1/transpose"}, .where = {.slot = nowhere}, .menu_value = 1};
	const Target name{.kind = Target::Kind::MODEL, .path = "name", .group = project::SettingGroupId::CLIP};
	const project::ParamAddress param =
	    project::ParamAddress::of(project::OwnerId::clip(nowhere_clip.clip_id), project::engine_target(3));
	return !writes.write(transpose) && !writes.write_text(name, Addresses{.clip = nowhere_clip}, "NOWHERE")
	       && !writes.delete_automation(param);
}

/// @brief A param of an owner no song holds has no value and is not automated.
[[nodiscard]] inline bool unknown_param_has_no_value(const controls::Facts& controls,
                                                     const project::ParamAddress& nowhere) {
	return !controls.param_value(nowhere).has_value() && !controls.param_automated(nowhere);
}

/// @brief A knob's write to, and the making of, a param of an owner no song holds are refused. A recording fake
///        refuses only with `accept` false, so set that first.
[[nodiscard]] inline bool unknown_param_write_is_refused(controls::Sink& sink, const project::ParamAddress& nowhere) {
	return !sink.write_param(nowhere, 1, 0, 0, controls::Resolution::KNOB) && !sink.make_param(nowhere);
}

/// @brief Every connected device has a name and settings, and no device names none.
[[nodiscard]] inline bool connected_devices_are_known(const MidiDevices& midi) {
	for (const midi::DeviceId d : midi.connected()) {
		if (!d || midi.name(d).empty() || !midi.settings(d).has_value()) {
			return false;
		}
	}
	return !midi.settings(midi::DeviceId{}).has_value();
}

/// @brief A write to no device is refused.
[[nodiscard]] inline bool write_to_no_device_is_refused(MidiDevices& midi) {
	return !midi.write(midi::DeviceId{}, MidiDeviceSettings{.send_clock = true});
}

} // namespace deluge::scenes::bind::contract
