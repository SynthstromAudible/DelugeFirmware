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

#include "scenarios/scenario_table.h"

// Every scenario below is host-only: several compile to nothing on the device.

#include "scenarios/drivers/driver_scenarios.h"
#include "scenarios/support/scenario_declare.h"

#include "playback/render_requests.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <utility>

// The stepped scenarios' entry points. The host drivers' are in drivers/driver_scenarios.h.
DELUGE_SCENARIO_DECLARE(audio_header_dump);
DELUGE_SCENARIO_DECLARE(audio_load_ab);
DELUGE_SCENARIO_DECLARE(audio_tick_hold);
DELUGE_SCENARIO_DECLARE(card_reinsert);
DELUGE_SCENARIO_DECLARE(peak_sidecars);
DELUGE_SCENARIO_DECLARE(clear_song_undo);
DELUGE_SCENARIO_DECLARE(clip_lifetime);
DELUGE_SCENARIO_DECLARE(clock_input);
DELUGE_SCENARIO_DECLARE(container_guard);
DELUGE_SCENARIO_DECLARE(document_save_channel);
DELUGE_SCENARIO_DECLARE(favourites_channel);
DELUGE_SCENARIO_DECLARE(idle_job_rates);
DELUGE_SCENARIO_DECLARE(load_cancel);
DELUGE_SCENARIO_DECLARE(load_policy);
DELUGE_SCENARIO_DECLARE(menu_dump);
DELUGE_SCENARIO_DECLARE(offset_io);
DELUGE_SCENARIO_DECLARE(path_operations);
DELUGE_SCENARIO_DECLARE(pattern_load);
DELUGE_SCENARIO_DECLARE(playhead_moves);
DELUGE_SCENARIO_DECLARE(preset_load);
DELUGE_SCENARIO_DECLARE(recorder_roundtrip);
DELUGE_SCENARIO_DECLARE(recorder_take);
DELUGE_SCENARIO_DECLARE(render_preset);
DELUGE_SCENARIO_DECLARE(render_song);
DELUGE_SCENARIO_DECLARE(sample_claim);
DELUGE_SCENARIO_DECLARE(sample_collect);
DELUGE_SCENARIO_DECLARE(sample_load_tree);
DELUGE_SCENARIO_DECLARE(save_roundtrip);
DELUGE_SCENARIO_DECLARE(scenes_spec);
DELUGE_SCENARIO_DECLARE(settings_channel);
DELUGE_SCENARIO_DECLARE(song_load_channel);
DELUGE_SCENARIO_DECLARE(song_preview_ab);
DELUGE_SCENARIO_DECLARE(song_save_channel);
DELUGE_SCENARIO_DECLARE(sound_lifetime);
DELUGE_SCENARIO_DECLARE(staging_release);
DELUGE_SCENARIO_DECLARE(stale_ui);
DELUGE_SCENARIO_DECLARE(startup_failsafe);
DELUGE_SCENARIO_DECLARE(stem_export);
DELUGE_SCENARIO_DECLARE(ui_script);
DELUGE_SCENARIO_DECLARE(ui_timer_deadline);
// The rows' onAppInit hooks.
extern "C" void deluge_scenario_clip_lifetime_boot(void);
extern "C" void deluge_scenario_scenes_spec_on_app_init(void);

namespace {

using StepFn = void (*)();

/// Shorthands for the cards a row names.
constexpr ScenarioCard kCorpusFixture{ScenarioCard::Kind::CorpusFixture};
constexpr ScenarioCard fromEnv(const char* spec) {
	return {ScenarioCard::Kind::FromEnv, spec};
}
constexpr ScenarioCard manifest(const char* path) {
	return {ScenarioCard::Kind::Manifest, path};
}
constexpr ScenarioCard builder(const char* name) {
	return {ScenarioCard::Kind::Builder, name};
}

// Alphabetical. clip_lifetime's onAppInit clears the song before the render has run a block (CL-EPOCH0).
constexpr DelugeScenario kScenarios[] = {
    {"audio_clip_edit_diff", nullptr, &deluge_scenario_audio_clip_edit_diff_begin,
     &deluge_scenario_audio_clip_edit_diff_complete, &deluge_scenario_audio_clip_edit_diff_dispatch_step,
     &deluge_scenario_audio_clip_edit_diff_step_done, &deluge_scenario_audio_clip_edit_diff_failures},
    {"audio_header_dump", nullptr, &deluge_scenario_audio_header_dump_begin,
     &deluge_scenario_audio_header_dump_complete, &deluge_scenario_audio_header_dump_dispatch_step,
     &deluge_scenario_audio_header_dump_step_done, &deluge_scenario_audio_header_dump_failures},
    {"audio_load_ab", nullptr, &deluge_scenario_audio_load_ab_begin, &deluge_scenario_audio_load_ab_complete,
     &deluge_scenario_audio_load_ab_dispatch_step, &deluge_scenario_audio_load_ab_step_done,
     &deluge_scenario_audio_load_ab_failures, ScenarioPacing::Virtual, ScenarioBoot::Full, ScenarioDrive::Interaction,
     manifest("tests/integration/scenarios/audio_load_ab.card")},
    {"audio_tick_hold", nullptr, &deluge_scenario_audio_tick_hold_begin, &deluge_scenario_audio_tick_hold_complete,
     &deluge_scenario_audio_tick_hold_dispatch_step, &deluge_scenario_audio_tick_hold_step_done,
     &deluge_scenario_audio_tick_hold_failures},
    {"cable_decode", nullptr, &deluge_scenario_cable_decode_begin, &deluge_scenario_cable_decode_complete,
     &deluge_scenario_cable_decode_dispatch_step, &deluge_scenario_cable_decode_step_done,
     &deluge_scenario_cable_decode_failures},
    {"card_reinsert", nullptr, &deluge_scenario_card_reinsert_begin, &deluge_scenario_card_reinsert_complete,
     &deluge_scenario_card_reinsert_dispatch_step, &deluge_scenario_card_reinsert_step_done,
     &deluge_scenario_card_reinsert_failures, ScenarioPacing::Virtual, ScenarioBoot::Full, ScenarioDrive::Interaction,
     manifest("tests/integration/scenarios/card_reinsert.card")},
    {"peak_sidecars", nullptr, &deluge_scenario_peak_sidecars_begin, &deluge_scenario_peak_sidecars_complete,
     &deluge_scenario_peak_sidecars_dispatch_step, &deluge_scenario_peak_sidecars_step_done,
     &deluge_scenario_peak_sidecars_failures, ScenarioPacing::Virtual, ScenarioBoot::Full, ScenarioDrive::Interaction,
     manifest("tests/integration/scenarios/peak_sidecars.card")},
    {"clear_song_undo", nullptr, &deluge_scenario_clear_song_undo_begin, &deluge_scenario_clear_song_undo_complete,
     &deluge_scenario_clear_song_undo_dispatch_step, &deluge_scenario_clear_song_undo_step_done,
     &deluge_scenario_clear_song_undo_failures, ScenarioPacing::Virtual, ScenarioBoot::Full, ScenarioDrive::Interaction,
     manifest("tests/integration/scenarios/clear_song_undo.card")},
    {"clip_clone_roundtrip", nullptr, &deluge_scenario_clip_clone_roundtrip_begin,
     &deluge_scenario_clip_clone_roundtrip_complete, &deluge_scenario_clip_clone_roundtrip_dispatch_step,
     &deluge_scenario_clip_clone_roundtrip_step_done, &deluge_scenario_clip_clone_roundtrip_failures},
    {"clip_lifetime", &deluge_scenario_clip_lifetime_boot, &deluge_scenario_clip_lifetime_begin,
     &deluge_scenario_clip_lifetime_complete, &deluge_scenario_clip_lifetime_dispatch_step,
     &deluge_scenario_clip_lifetime_step_done, &deluge_scenario_clip_lifetime_failures},
    {"clock_input", nullptr, &deluge_scenario_clock_input_begin, &deluge_scenario_clock_input_complete,
     &deluge_scenario_clock_input_dispatch_step, &deluge_scenario_clock_input_step_done,
     &deluge_scenario_clock_input_failures, ScenarioPacing::Realtime},
    {"container_guard", nullptr, &deluge_scenario_container_guard_begin, &deluge_scenario_container_guard_complete,
     &deluge_scenario_container_guard_dispatch_step, &deluge_scenario_container_guard_step_done,
     &deluge_scenario_container_guard_failures},
    {"document_save_channel", nullptr, &deluge_scenario_document_save_channel_begin,
     &deluge_scenario_document_save_channel_complete, &deluge_scenario_document_save_channel_dispatch_step,
     &deluge_scenario_document_save_channel_step_done, &deluge_scenario_document_save_channel_failures},
    {"playhead_moves", nullptr, &deluge_scenario_playhead_moves_begin, &deluge_scenario_playhead_moves_complete,
     &deluge_scenario_playhead_moves_dispatch_step, &deluge_scenario_playhead_moves_step_done,
     &deluge_scenario_playhead_moves_failures},
    {"preset_parse", nullptr, &deluge_scenario_preset_parse_begin, &deluge_scenario_preset_parse_complete,
     &deluge_scenario_preset_parse_dispatch_step, &deluge_scenario_preset_parse_step_done,
     &deluge_scenario_preset_parse_failures},
    {"document_version", nullptr, &deluge_scenario_document_version_begin, &deluge_scenario_document_version_complete,
     &deluge_scenario_document_version_dispatch_step, &deluge_scenario_document_version_step_done,
     &deluge_scenario_document_version_failures},
    {"edit_cost", nullptr, &deluge_scenario_edit_cost_begin, &deluge_scenario_edit_cost_complete,
     &deluge_scenario_edit_cost_dispatch_step, &deluge_scenario_edit_cost_step_done,
     &deluge_scenario_edit_cost_failures},
    {"source_memory", nullptr, &deluge_scenario_source_memory_begin, &deluge_scenario_source_memory_complete,
     &deluge_scenario_source_memory_dispatch_step, &deluge_scenario_source_memory_step_done,
     &deluge_scenario_source_memory_failures, ScenarioPacing::Virtual, ScenarioBoot::Full, ScenarioDrive::Interaction,
     manifest("tests/integration/scenarios/source_memory.card")},
    {"heap_footprint", nullptr, &deluge_scenario_heap_footprint_begin, &deluge_scenario_heap_footprint_complete,
     &deluge_scenario_heap_footprint_dispatch_step, &deluge_scenario_heap_footprint_step_done,
     &deluge_scenario_heap_footprint_failures},
    {"edit_substrate", nullptr, &deluge_scenario_edit_substrate_begin, &deluge_scenario_edit_substrate_complete,
     &deluge_scenario_edit_substrate_dispatch_step, &deluge_scenario_edit_substrate_step_done,
     &deluge_scenario_edit_substrate_failures},
    {"voice_pins", nullptr, &deluge_scenario_voice_pins_begin, &deluge_scenario_voice_pins_complete,
     &deluge_scenario_voice_pins_dispatch_step, &deluge_scenario_voice_pins_step_done,
     &deluge_scenario_voice_pins_failures},
    {"effect_owners", nullptr, &deluge_scenario_effect_owners_begin, &deluge_scenario_effect_owners_complete,
     &deluge_scenario_effect_owners_dispatch_step, &deluge_scenario_effect_owners_step_done,
     &deluge_scenario_effect_owners_failures},
    {"edit_undo_redo", nullptr, &deluge_scenario_edit_undo_redo_begin, &deluge_scenario_edit_undo_redo_complete,
     &deluge_scenario_edit_undo_redo_dispatch_step, &deluge_scenario_edit_undo_redo_step_done,
     &deluge_scenario_edit_undo_redo_failures},
    {"favourites_channel", nullptr, &deluge_scenario_favourites_channel_begin,
     &deluge_scenario_favourites_channel_complete, &deluge_scenario_favourites_channel_dispatch_step,
     &deluge_scenario_favourites_channel_step_done, &deluge_scenario_favourites_channel_failures},
    {"idle_job_rates", nullptr, &deluge_scenario_idle_job_rates_begin, &deluge_scenario_idle_job_rates_complete,
     &deluge_scenario_idle_job_rates_dispatch_step, &deluge_scenario_idle_job_rates_step_done,
     &deluge_scenario_idle_job_rates_failures, ScenarioPacing::DevicePeriod},
    {"engine_settings", nullptr, &deluge_scenario_engine_settings_begin, &deluge_scenario_engine_settings_complete,
     &deluge_scenario_engine_settings_dispatch_step, &deluge_scenario_engine_settings_step_done,
     &deluge_scenario_engine_settings_failures},
    {"song_settings", nullptr, &deluge_scenario_song_settings_begin, &deluge_scenario_song_settings_complete,
     &deluge_scenario_song_settings_dispatch_step, &deluge_scenario_song_settings_step_done,
     &deluge_scenario_song_settings_failures},
    {"mapping_dispatch", nullptr, &deluge_scenario_mapping_dispatch_begin, &deluge_scenario_mapping_dispatch_complete,
     &deluge_scenario_mapping_dispatch_dispatch_step, &deluge_scenario_mapping_dispatch_step_done,
     &deluge_scenario_mapping_dispatch_failures},
    {"host_effect_settings", nullptr, &deluge_scenario_host_effect_settings_begin,
     &deluge_scenario_host_effect_settings_complete, &deluge_scenario_host_effect_settings_dispatch_step,
     &deluge_scenario_host_effect_settings_step_done, &deluge_scenario_host_effect_settings_failures},
    {"instrument_codec_decode", nullptr, &deluge_scenario_instrument_codec_decode_begin,
     &deluge_scenario_instrument_codec_decode_complete, &deluge_scenario_instrument_codec_decode_dispatch_step,
     &deluge_scenario_instrument_codec_decode_step_done, &deluge_scenario_instrument_codec_decode_failures},
    {"instrument_codec_encode", nullptr, &deluge_scenario_instrument_codec_encode_begin,
     &deluge_scenario_instrument_codec_encode_complete, &deluge_scenario_instrument_codec_encode_dispatch_step,
     &deluge_scenario_instrument_codec_encode_step_done, &deluge_scenario_instrument_codec_encode_failures},
    {"instrument_state_roundtrip", nullptr, &deluge_scenario_instrument_state_roundtrip_begin,
     &deluge_scenario_instrument_state_roundtrip_complete, &deluge_scenario_instrument_state_roundtrip_dispatch_step,
     &deluge_scenario_instrument_state_roundtrip_step_done, &deluge_scenario_instrument_state_roundtrip_failures},
    {"json_roundtrip", nullptr, &deluge_scenario_json_roundtrip_begin, &deluge_scenario_json_roundtrip_complete,
     &deluge_scenario_json_roundtrip_dispatch_step, &deluge_scenario_json_roundtrip_step_done,
     &deluge_scenario_json_roundtrip_failures},
    {"load_cancel", nullptr, &deluge_scenario_load_cancel_begin, &deluge_scenario_load_cancel_complete,
     &deluge_scenario_load_cancel_dispatch_step, &deluge_scenario_load_cancel_step_done,
     &deluge_scenario_load_cancel_failures, ScenarioPacing::Virtual, ScenarioBoot::Full, ScenarioDrive::Interaction,
     manifest("tests/integration/scenarios/load_cancel.card")},
    {"load_policy", nullptr, &deluge_scenario_load_policy_begin, &deluge_scenario_load_policy_complete,
     &deluge_scenario_load_policy_dispatch_step, &deluge_scenario_load_policy_step_done,
     &deluge_scenario_load_policy_failures, ScenarioPacing::Virtual, ScenarioBoot::Full, ScenarioDrive::Interaction,
     manifest("tests/integration/scenarios/load_policy.card")},
    {"model_decode_diff", nullptr, &deluge_scenario_model_decode_diff_begin,
     &deluge_scenario_model_decode_diff_complete, &deluge_scenario_model_decode_diff_dispatch_step,
     &deluge_scenario_model_decode_diff_step_done, &deluge_scenario_model_decode_diff_failures},
    {"offset_io", nullptr, &deluge_scenario_offset_io_begin, &deluge_scenario_offset_io_complete,
     &deluge_scenario_offset_io_dispatch_step, &deluge_scenario_offset_io_step_done,
     &deluge_scenario_offset_io_failures},
    {"param_clone", nullptr, &deluge_scenario_param_clone_begin, &deluge_scenario_param_clone_complete,
     &deluge_scenario_param_clone_dispatch_step, &deluge_scenario_param_clone_step_done,
     &deluge_scenario_param_clone_failures},
    {"patch_routing", nullptr, &deluge_scenario_patch_routing_begin, &deluge_scenario_patch_routing_complete,
     &deluge_scenario_patch_routing_dispatch_step, &deluge_scenario_patch_routing_step_done,
     &deluge_scenario_patch_routing_failures},
    {"param_projection", nullptr, &deluge_scenario_param_projection_begin, &deluge_scenario_param_projection_complete,
     &deluge_scenario_param_projection_dispatch_step, &deluge_scenario_param_projection_step_done,
     &deluge_scenario_param_projection_failures},
    {"param_record_diff", nullptr, &deluge_scenario_param_record_diff_begin,
     &deluge_scenario_param_record_diff_complete, &deluge_scenario_param_record_diff_dispatch_step,
     &deluge_scenario_param_record_diff_step_done, &deluge_scenario_param_record_diff_failures},
    {"param_snapshot_bench", nullptr, &deluge_scenario_param_snapshot_bench_begin,
     &deluge_scenario_param_snapshot_bench_complete, &deluge_scenario_param_snapshot_bench_dispatch_step,
     &deluge_scenario_param_snapshot_bench_step_done, &deluge_scenario_param_snapshot_bench_failures},
    {"param_table_check", nullptr, &deluge_scenario_param_table_check_begin,
     &deluge_scenario_param_table_check_complete, &deluge_scenario_param_table_check_dispatch_step,
     &deluge_scenario_param_table_check_step_done, &deluge_scenario_param_table_check_failures},
    {"param_target_check", nullptr, &deluge_scenario_param_target_check_begin,
     &deluge_scenario_param_target_check_complete, &deluge_scenario_param_target_check_dispatch_step,
     &deluge_scenario_param_target_check_step_done, &deluge_scenario_param_target_check_failures},
    {"path_operations", nullptr, &deluge_scenario_path_operations_begin, &deluge_scenario_path_operations_complete,
     &deluge_scenario_path_operations_dispatch_step, &deluge_scenario_path_operations_step_done,
     &deluge_scenario_path_operations_failures},
    {"pattern_load", nullptr, &deluge_scenario_pattern_load_begin, &deluge_scenario_pattern_load_complete,
     &deluge_scenario_pattern_load_dispatch_step, &deluge_scenario_pattern_load_step_done,
     &deluge_scenario_pattern_load_failures},
    {"preset_load", nullptr, &deluge_scenario_preset_load_begin, &deluge_scenario_preset_load_complete,
     &deluge_scenario_preset_load_dispatch_step, &deluge_scenario_preset_load_step_done,
     &deluge_scenario_preset_load_failures, ScenarioPacing::Virtual, ScenarioBoot::Full, ScenarioDrive::Interaction,
     manifest("tests/integration/scenarios/preset_load.card")},
    {"preset_roundtrip", nullptr, &deluge_scenario_preset_roundtrip_begin, &deluge_scenario_preset_roundtrip_complete,
     &deluge_scenario_preset_roundtrip_dispatch_step, &deluge_scenario_preset_roundtrip_step_done,
     &deluge_scenario_preset_roundtrip_failures},
    {"recorder_roundtrip", nullptr, &deluge_scenario_recorder_roundtrip_begin,
     &deluge_scenario_recorder_roundtrip_complete, &deluge_scenario_recorder_roundtrip_dispatch_step,
     &deluge_scenario_recorder_roundtrip_step_done, &deluge_scenario_recorder_roundtrip_failures,
     ScenarioPacing::Virtual, ScenarioBoot::Full, ScenarioDrive::OwnClock},
    {"recorder_take", nullptr, &deluge_scenario_recorder_take_begin, &deluge_scenario_recorder_take_complete,
     &deluge_scenario_recorder_take_dispatch_step, &deluge_scenario_recorder_take_step_done,
     &deluge_scenario_recorder_take_failures, ScenarioPacing::DevicePeriod},
    {"render_preset", nullptr, &deluge_scenario_render_preset_begin, &deluge_scenario_render_preset_complete,
     &deluge_scenario_render_preset_dispatch_step, &deluge_scenario_render_preset_step_done,
     &deluge_scenario_render_preset_failures, ScenarioPacing::DevicePeriod, ScenarioBoot::Full,
     ScenarioDrive::Interaction, fromEnv("RENDER_TREE")},
    {"render_song", nullptr, &deluge_scenario_render_song_begin, &deluge_scenario_render_song_complete,
     &deluge_scenario_render_song_dispatch_step, &deluge_scenario_render_song_step_done,
     &deluge_scenario_render_song_failures, ScenarioPacing::DevicePeriod, ScenarioBoot::Full,
     ScenarioDrive::Interaction, fromEnv("RENDER_TREE|corpus")},
    {"sample_claim", nullptr, &deluge_scenario_sample_claim_begin, &deluge_scenario_sample_claim_complete,
     &deluge_scenario_sample_claim_dispatch_step, &deluge_scenario_sample_claim_step_done,
     &deluge_scenario_sample_claim_failures, ScenarioPacing::Virtual, ScenarioBoot::Full, ScenarioDrive::Interaction,
     manifest("tests/integration/scenarios/sample_claim.card")},
    {"sample_collect", nullptr, &deluge_scenario_sample_collect_begin, &deluge_scenario_sample_collect_complete,
     &deluge_scenario_sample_collect_dispatch_step, &deluge_scenario_sample_collect_step_done,
     &deluge_scenario_sample_collect_failures, ScenarioPacing::Virtual, ScenarioBoot::Full, ScenarioDrive::Interaction,
     manifest("tests/integration/scenarios/sample_collect.card")},
    {"sample_load_tree", nullptr, &deluge_scenario_sample_load_tree_begin, &deluge_scenario_sample_load_tree_complete,
     &deluge_scenario_sample_load_tree_dispatch_step, &deluge_scenario_sample_load_tree_step_done,
     &deluge_scenario_sample_load_tree_failures, ScenarioPacing::Virtual, ScenarioBoot::Full,
     ScenarioDrive::Interaction, manifest("tests/integration/scenarios/sample_load_tree.card")},
    {"save_roundtrip", nullptr, &deluge_scenario_save_roundtrip_begin, &deluge_scenario_save_roundtrip_complete,
     &deluge_scenario_save_roundtrip_dispatch_step, &deluge_scenario_save_roundtrip_step_done,
     &deluge_scenario_save_roundtrip_failures},
    {"settings_channel", nullptr, &deluge_scenario_settings_channel_begin, &deluge_scenario_settings_channel_complete,
     &deluge_scenario_settings_channel_dispatch_step, &deluge_scenario_settings_channel_step_done,
     &deluge_scenario_settings_channel_failures},
    {"serializer_check", nullptr, &deluge_scenario_serializer_check_begin, &deluge_scenario_serializer_check_complete,
     &deluge_scenario_serializer_check_dispatch_step, &deluge_scenario_serializer_check_step_done,
     &deluge_scenario_serializer_check_failures},
    {"song_edit_roundtrip", nullptr, &deluge_scenario_song_edit_roundtrip_begin,
     &deluge_scenario_song_edit_roundtrip_complete, &deluge_scenario_song_edit_roundtrip_dispatch_step,
     &deluge_scenario_song_edit_roundtrip_step_done, &deluge_scenario_song_edit_roundtrip_failures},
    {"song_load_channel", nullptr, &deluge_scenario_song_load_channel_begin,
     &deluge_scenario_song_load_channel_complete, &deluge_scenario_song_load_channel_dispatch_step,
     &deluge_scenario_song_load_channel_step_done, &deluge_scenario_song_load_channel_failures, ScenarioPacing::Virtual,
     ScenarioBoot::Full, ScenarioDrive::Interaction, manifest("tests/integration/scenarios/song_load_channel.card")},
    {"song_preview_ab", nullptr, &deluge_scenario_song_preview_ab_begin, &deluge_scenario_song_preview_ab_complete,
     &deluge_scenario_song_preview_ab_dispatch_step, &deluge_scenario_song_preview_ab_step_done,
     &deluge_scenario_song_preview_ab_failures, ScenarioPacing::Virtual, ScenarioBoot::Full, ScenarioDrive::Interaction,
     builder("song_preview_ab")},
    {"song_roundtrip", nullptr, &deluge_scenario_song_roundtrip_begin, &deluge_scenario_song_roundtrip_complete,
     &deluge_scenario_song_roundtrip_dispatch_step, &deluge_scenario_song_roundtrip_step_done,
     &deluge_scenario_song_roundtrip_failures},
    {"song_save_channel", nullptr, &deluge_scenario_song_save_channel_begin,
     &deluge_scenario_song_save_channel_complete, &deluge_scenario_song_save_channel_dispatch_step,
     &deluge_scenario_song_save_channel_step_done, &deluge_scenario_song_save_channel_failures, ScenarioPacing::Virtual,
     ScenarioBoot::Full, ScenarioDrive::Interaction, manifest("tests/integration/scenarios/song_save_channel.card")},
    {"sound_lifetime", nullptr, &deluge_scenario_sound_lifetime_begin, &deluge_scenario_sound_lifetime_complete,
     &deluge_scenario_sound_lifetime_dispatch_step, &deluge_scenario_sound_lifetime_step_done,
     &deluge_scenario_sound_lifetime_failures},
    {"staging_release", nullptr, &deluge_scenario_staging_release_begin, &deluge_scenario_staging_release_complete,
     &deluge_scenario_staging_release_dispatch_step, &deluge_scenario_staging_release_step_done,
     &deluge_scenario_staging_release_failures},
    {"stale_ui", nullptr, &deluge_scenario_stale_ui_begin, &deluge_scenario_stale_ui_complete,
     &deluge_scenario_stale_ui_dispatch_step, &deluge_scenario_stale_ui_step_done, &deluge_scenario_stale_ui_failures,
     ScenarioPacing::DevicePeriod},
    // The offline stem export of the corpus fixture's song. The single-slot byte channel stays free for the export:
    // the settings prelude, whose reads (and its write when MIDI_FOLLOW.XML is absent) would occupy it, is skipped.
    // No gate observes the difference: a full boot exports the same audio and prints the same markers.
    {"stem_export", nullptr, &deluge_scenario_stem_export_begin, &deluge_scenario_stem_export_complete,
     &deluge_scenario_stem_export_dispatch_step, &deluge_scenario_stem_export_step_done,
     &deluge_scenario_stem_export_failures, ScenarioPacing::Virtual, ScenarioBoot::NoSettingsPrelude,
     ScenarioDrive::OwnClock, kCorpusFixture},
    {"menu_dump", nullptr, &deluge_scenario_menu_dump_begin, &deluge_scenario_menu_dump_complete,
     &deluge_scenario_menu_dump_dispatch_step, &deluge_scenario_menu_dump_step_done,
     &deluge_scenario_menu_dump_failures},
    {"scenes_spec", &deluge_scenario_scenes_spec_on_app_init, &deluge_scenario_scenes_spec_begin,
     &deluge_scenario_scenes_spec_complete, &deluge_scenario_scenes_spec_dispatch_step,
     &deluge_scenario_scenes_spec_step_done, &deluge_scenario_scenes_spec_failures, ScenarioPacing::Virtual,
     ScenarioBoot::Full, ScenarioDrive::Interaction, fromEnv("SCENES_SPEC_CARD|tests/fixtures/card")},
    {"ui_script", nullptr, &deluge_scenario_ui_script_begin, &deluge_scenario_ui_script_complete,
     &deluge_scenario_ui_script_dispatch_step, &deluge_scenario_ui_script_step_done,
     &deluge_scenario_ui_script_failures, ScenarioPacing::DevicePeriod, ScenarioBoot::Full, ScenarioDrive::Interaction,
     fromEnv("UI_CARD|tests/fixtures/card")},
    {"startup_failsafe", nullptr, &deluge_scenario_startup_failsafe_begin, &deluge_scenario_startup_failsafe_complete,
     &deluge_scenario_startup_failsafe_dispatch_step, &deluge_scenario_startup_failsafe_step_done,
     &deluge_scenario_startup_failsafe_failures},
    {"ui_timer_deadline", nullptr, &deluge_scenario_ui_timer_deadline_begin,
     &deluge_scenario_ui_timer_deadline_complete, &deluge_scenario_ui_timer_deadline_dispatch_step,
     &deluge_scenario_ui_timer_deadline_step_done, &deluge_scenario_ui_timer_deadline_failures,
     ScenarioPacing::DevicePeriod},
    {"xml_replay_diff", nullptr, &deluge_scenario_xml_replay_diff_begin, &deluge_scenario_xml_replay_diff_complete,
     &deluge_scenario_xml_replay_diff_dispatch_step, &deluge_scenario_xml_replay_diff_step_done,
     &deluge_scenario_xml_replay_diff_failures},
};

/// A step is an interaction-tier entry into the app, as an app-loop job is: like deluge_app_service(), it first
/// does what the render has asked of the interaction tier (deluge::playback::applyRenderRequests()).
template <std::size_t I>
void dispatchStepAsInteractionEntry() {
	deluge::playback::applyRenderRequests();
	kScenarios[I].dispatchStep();
}

/// Row @p I's step: an interaction-tier entry, or the scenario's own for an OwnClock row.
template <std::size_t I>
constexpr StepFn interactionEntry() {
	return kScenarios[I].drive == ScenarioDrive::Interaction ? &dispatchStepAsInteractionEntry<I>
	                                                         : kScenarios[I].dispatchStep;
}

template <std::size_t... I>
constexpr std::array<DelugeScenario, sizeof...(I)> withInteractionEntries(std::index_sequence<I...>) {
	return {DelugeScenario{kScenarios[I].name, kScenarios[I].onAppInit, kScenarios[I].begin, kScenarios[I].complete,
	                       interactionEntry<I>(), kScenarios[I].stepDone, kScenarios[I].failures, kScenarios[I].pacing,
	                       kScenarios[I].boot, kScenarios[I].drive, kScenarios[I].card}...};
}

/// Every row has its five step functions.
constexpr bool hasSteps(const DelugeScenario& row) {
	return row.begin != nullptr && row.complete != nullptr && row.dispatchStep != nullptr && row.stepDone != nullptr
	       && row.failures != nullptr;
}
static_assert(std::ranges::all_of(kScenarios, hasSteps), "every scenario row has its five step functions");

// golden_vt_render's `Scenario` mirrors this layout and asserts the same offsets: seven pointers, the pacing, boot and
// drive bytes, then the card at the next pointer boundary, on the 64-bit host and the 32-bit ARM leg alike.
static_assert(sizeof(ScenarioPacing) == 1 && sizeof(ScenarioCard::Kind) == 1);
static_assert(offsetof(DelugeScenario, pacing) == 7 * sizeof(void*));
static_assert(offsetof(DelugeScenario, boot) == 7 * sizeof(void*) + 1);
static_assert(offsetof(DelugeScenario, drive) == 7 * sizeof(void*) + 2);
static_assert(sizeof(ScenarioBoot) == 1 && sizeof(ScenarioDrive) == 1);
static_assert(offsetof(DelugeScenario, card) == 8 * sizeof(void*));

/// The rows the driver is given.
constexpr auto kEntries = withInteractionEntries(std::make_index_sequence<std::size(kScenarios)>{});

} // namespace

extern "C" const DelugeScenario* deluge_scenario_find(const char* name) {
	for (const DelugeScenario& scenario : kEntries) {
		if (std::strcmp(scenario.name, name) == 0) {
			return &scenario;
		}
	}
	return nullptr;
}
