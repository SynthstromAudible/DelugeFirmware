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
	const project::ParamAddress param{.owner = project::OwnerId::instrument(nowhere.id)};
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

/// @brief Every clip edit on a clip no song holds is refused. A fake that accepts everything is set to refuse first.
[[nodiscard]] inline bool unknown_clip_is_refused(ClipWrites& writes, project::ClipAddress nowhere) {
	return !writes.recolour_clip(nowhere, 1) && !writes.clear_clip(nowhere, true, true)
	       && !writes.move_audio_clip_marker(nowhere, "endSamplePos", 1)
	       && !writes.resize_audio_clip(nowhere, ResizeKind::NO_STRETCH, 96, 192);
}

} // namespace deluge::scenes::bind::contract
