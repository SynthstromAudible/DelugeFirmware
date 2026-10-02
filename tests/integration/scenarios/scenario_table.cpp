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

#include "scenarios/audio_header_dump_scenario.h"
#include "scenarios/audio_load_ab_scenario.h"
#include "scenarios/card_reinsert_scenario.h"
#include "scenarios/clear_song_undo_scenario.h"
#include "scenarios/clip_lifetime_scenario.h"
#include "scenarios/clock_input_scenario.h"
#include "scenarios/container_guard_scenario.h"
#include "scenarios/document_save_channel_scenario.h"
#include "scenarios/favourites_channel_scenario.h"
#include "scenarios/idle_job_rates_scenario.h"
#include "scenarios/load_cancel_scenario.h"
#include "scenarios/load_policy_scenario.h"
#include "scenarios/offset_io_scenario.h"
#include "scenarios/path_operations_scenario.h"
#include "scenarios/pattern_load_scenario.h"
#include "scenarios/preset_load_scenario.h"
#include "scenarios/recorder_take_scenario.h"
#include "scenarios/render_preset_scenario.h"
#include "scenarios/render_song_scenario.h"
#include "scenarios/sample_claim_scenario.h"
#include "scenarios/sample_collect_scenario.h"
#include "scenarios/sample_load_tree_scenario.h"
#include "scenarios/save_roundtrip_scenario.h"
#include "scenarios/settings_channel_scenario.h"
#include "scenarios/song_load_channel_scenario.h"
#include "scenarios/song_preview_ab_scenario.h"
#include "scenarios/song_save_channel_scenario.h"
#include "scenarios/sound_lifetime_scenario.h"
#include "scenarios/staging_release_scenario.h"
#include "scenarios/startup_failsafe_scenario.h"
#include "scenarios/ui_timer_deadline_scenario.h"

#include <cstring>

namespace {

// Alphabetical. clip_lifetime's onAppInit clears the song before the render has run a block (CL-EPOCH0).
constexpr DelugeScenario kScenarios[] = {
    {"audio_header_dump", nullptr, &deluge_scenario_audio_header_dump_begin,
     &deluge_scenario_audio_header_dump_complete, &deluge_scenario_audio_header_dump_dispatch_step,
     &deluge_scenario_audio_header_dump_step_done, &deluge_scenario_audio_header_dump_failures},
    {"audio_load_ab", nullptr, &deluge_scenario_audio_load_ab_begin, &deluge_scenario_audio_load_ab_complete,
     &deluge_scenario_audio_load_ab_dispatch_step, &deluge_scenario_audio_load_ab_step_done,
     &deluge_scenario_audio_load_ab_failures},
    {"card_reinsert", nullptr, &deluge_scenario_card_reinsert_begin, &deluge_scenario_card_reinsert_complete,
     &deluge_scenario_card_reinsert_dispatch_step, &deluge_scenario_card_reinsert_step_done,
     &deluge_scenario_card_reinsert_failures},
    {"clear_song_undo", nullptr, &deluge_scenario_clear_song_undo_begin, &deluge_scenario_clear_song_undo_complete,
     &deluge_scenario_clear_song_undo_dispatch_step, &deluge_scenario_clear_song_undo_step_done,
     &deluge_scenario_clear_song_undo_failures},
    {"clip_lifetime", &deluge_scenario_clip_lifetime_boot, &deluge_scenario_clip_lifetime_begin,
     &deluge_scenario_clip_lifetime_complete, &deluge_scenario_clip_lifetime_dispatch_step,
     &deluge_scenario_clip_lifetime_step_done, &deluge_scenario_clip_lifetime_failures},
    {"clock_input", nullptr, &deluge_scenario_clock_input_begin, &deluge_scenario_clock_input_complete,
     &deluge_scenario_clock_input_dispatch_step, &deluge_scenario_clock_input_step_done,
     &deluge_scenario_clock_input_failures},
    {"container_guard", nullptr, &deluge_scenario_container_guard_begin, &deluge_scenario_container_guard_complete,
     &deluge_scenario_container_guard_dispatch_step, &deluge_scenario_container_guard_step_done,
     &deluge_scenario_container_guard_failures},
    {"document_save_channel", nullptr, &deluge_scenario_document_save_channel_begin,
     &deluge_scenario_document_save_channel_complete, &deluge_scenario_document_save_channel_dispatch_step,
     &deluge_scenario_document_save_channel_step_done, &deluge_scenario_document_save_channel_failures},
    {"favourites_channel", nullptr, &deluge_scenario_favourites_channel_begin,
     &deluge_scenario_favourites_channel_complete, &deluge_scenario_favourites_channel_dispatch_step,
     &deluge_scenario_favourites_channel_step_done, &deluge_scenario_favourites_channel_failures},
    {"idle_job_rates", nullptr, &deluge_scenario_idle_job_rates_begin, &deluge_scenario_idle_job_rates_complete,
     &deluge_scenario_idle_job_rates_dispatch_step, &deluge_scenario_idle_job_rates_step_done,
     &deluge_scenario_idle_job_rates_failures},
    {"load_cancel", nullptr, &deluge_scenario_load_cancel_begin, &deluge_scenario_load_cancel_complete,
     &deluge_scenario_load_cancel_dispatch_step, &deluge_scenario_load_cancel_step_done,
     &deluge_scenario_load_cancel_failures},
    {"load_policy", nullptr, &deluge_scenario_load_policy_begin, &deluge_scenario_load_policy_complete,
     &deluge_scenario_load_policy_dispatch_step, &deluge_scenario_load_policy_step_done,
     &deluge_scenario_load_policy_failures},
    {"offset_io", nullptr, &deluge_scenario_offset_io_begin, &deluge_scenario_offset_io_complete,
     &deluge_scenario_offset_io_dispatch_step, &deluge_scenario_offset_io_step_done,
     &deluge_scenario_offset_io_failures},
    {"path_operations", nullptr, &deluge_scenario_path_operations_begin, &deluge_scenario_path_operations_complete,
     &deluge_scenario_path_operations_dispatch_step, &deluge_scenario_path_operations_step_done,
     &deluge_scenario_path_operations_failures},
    {"pattern_load", nullptr, &deluge_scenario_pattern_load_begin, &deluge_scenario_pattern_load_complete,
     &deluge_scenario_pattern_load_dispatch_step, &deluge_scenario_pattern_load_step_done,
     &deluge_scenario_pattern_load_failures},
    {"preset_load", nullptr, &deluge_scenario_preset_load_begin, &deluge_scenario_preset_load_complete,
     &deluge_scenario_preset_load_dispatch_step, &deluge_scenario_preset_load_step_done,
     &deluge_scenario_preset_load_failures},
    {"recorder_take", nullptr, &deluge_scenario_recorder_take_begin, &deluge_scenario_recorder_take_complete,
     &deluge_scenario_recorder_take_dispatch_step, &deluge_scenario_recorder_take_step_done,
     &deluge_scenario_recorder_take_failures},
    {"render_preset", nullptr, &deluge_scenario_render_preset_begin, &deluge_scenario_render_preset_complete,
     &deluge_scenario_render_preset_dispatch_step, &deluge_scenario_render_preset_step_done,
     &deluge_scenario_render_preset_failures},
    {"render_song", nullptr, &deluge_scenario_render_song_begin, &deluge_scenario_render_song_complete,
     &deluge_scenario_render_song_dispatch_step, &deluge_scenario_render_song_step_done,
     &deluge_scenario_render_song_failures},
    {"sample_claim", nullptr, &deluge_scenario_sample_claim_begin, &deluge_scenario_sample_claim_complete,
     &deluge_scenario_sample_claim_dispatch_step, &deluge_scenario_sample_claim_step_done,
     &deluge_scenario_sample_claim_failures},
    {"sample_collect", nullptr, &deluge_scenario_sample_collect_begin, &deluge_scenario_sample_collect_complete,
     &deluge_scenario_sample_collect_dispatch_step, &deluge_scenario_sample_collect_step_done,
     &deluge_scenario_sample_collect_failures},
    {"sample_load_tree", nullptr, &deluge_scenario_sample_load_tree_begin, &deluge_scenario_sample_load_tree_complete,
     &deluge_scenario_sample_load_tree_dispatch_step, &deluge_scenario_sample_load_tree_step_done,
     &deluge_scenario_sample_load_tree_failures},
    {"save_roundtrip", nullptr, &deluge_scenario_save_roundtrip_begin, &deluge_scenario_save_roundtrip_complete,
     &deluge_scenario_save_roundtrip_dispatch_step, &deluge_scenario_save_roundtrip_step_done,
     &deluge_scenario_save_roundtrip_failures},
    {"settings_channel", nullptr, &deluge_scenario_settings_channel_begin, &deluge_scenario_settings_channel_complete,
     &deluge_scenario_settings_channel_dispatch_step, &deluge_scenario_settings_channel_step_done,
     &deluge_scenario_settings_channel_failures},
    {"song_load_channel", nullptr, &deluge_scenario_song_load_channel_begin,
     &deluge_scenario_song_load_channel_complete, &deluge_scenario_song_load_channel_dispatch_step,
     &deluge_scenario_song_load_channel_step_done, &deluge_scenario_song_load_channel_failures},
    {"song_preview_ab", nullptr, &deluge_scenario_song_preview_ab_begin, &deluge_scenario_song_preview_ab_complete,
     &deluge_scenario_song_preview_ab_dispatch_step, &deluge_scenario_song_preview_ab_step_done,
     &deluge_scenario_song_preview_ab_failures},
    {"song_save_channel", nullptr, &deluge_scenario_song_save_channel_begin,
     &deluge_scenario_song_save_channel_complete, &deluge_scenario_song_save_channel_dispatch_step,
     &deluge_scenario_song_save_channel_step_done, &deluge_scenario_song_save_channel_failures},
    {"sound_lifetime", nullptr, &deluge_scenario_sound_lifetime_begin, &deluge_scenario_sound_lifetime_complete,
     &deluge_scenario_sound_lifetime_dispatch_step, &deluge_scenario_sound_lifetime_step_done,
     &deluge_scenario_sound_lifetime_failures},
    {"staging_release", nullptr, &deluge_scenario_staging_release_begin, &deluge_scenario_staging_release_complete,
     &deluge_scenario_staging_release_dispatch_step, &deluge_scenario_staging_release_step_done,
     &deluge_scenario_staging_release_failures},
    {"startup_failsafe", nullptr, &deluge_scenario_startup_failsafe_begin, &deluge_scenario_startup_failsafe_complete,
     &deluge_scenario_startup_failsafe_dispatch_step, &deluge_scenario_startup_failsafe_step_done,
     &deluge_scenario_startup_failsafe_failures},
    {"ui_timer_deadline", nullptr, &deluge_scenario_ui_timer_deadline_begin,
     &deluge_scenario_ui_timer_deadline_complete, &deluge_scenario_ui_timer_deadline_dispatch_step,
     &deluge_scenario_ui_timer_deadline_step_done, &deluge_scenario_ui_timer_deadline_failures},
};

} // namespace

extern "C" const DelugeScenario* deluge_scenario_find(const char* name) {
	for (const DelugeScenario& scenario : kScenarios) {
		if (std::strcmp(scenario.name, name) == 0) {
			return &scenario;
		}
	}
	return nullptr;
}
