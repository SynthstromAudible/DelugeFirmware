/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "model/mod_controllable/mod_controllable_audio.h"
#include "definitions_cxx.hpp"
#include "deluge-project/param_automation.h"
#include "deluge/model/settings/runtime_feature_settings.h"
#include "dsp-next/core/stereo_sample.h"
#include "effects/codec.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "instruments/deluge/sound_state.h"
#include "io/debug/log.h"
#include "io/midi/midi_engine.h"
#include "mem_functions.h"
#include "memory/general_memory_allocator.h"
#include "memory/heaps.h"
#include "memory/stack_guard.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/change_effect.h"
#include "model/fx/chain_params.h"
#include "model/fx/channel_setup.h"
#include "model/fx/song_stutter.h"
#include "model/mod_controllable/delay_arena_app.h"
#include "model/mod_controllable/delay_type.h"
#include "model/mod_controllable/mod_fx_allocator.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/midi_command.h"
#include "model/project_bridge/param_projection.h"
#include "model/project_bridge/param_serialization.h"
#include "model/song/reverb_presets.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "modulation/sidechain/sidechain.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "storage/storage_manager.h"
#include "storage/value_tree_deserializer.h"
#include "util/etl_string.h"
#include "util/functions.h"
#include "util/structural_edit_probe.h"
#include <algorithm>
#include <span>

namespace params = deluge::modulation::params;

ModControllableAudio::ModControllableAudio() = default;

ModControllableAudio::~ModControllableAudio() = default;

void ModControllableAudio::setEffects(const deluge::effects::EffectStates& next) {
	deluge::model::storeEffects(*effectsOwner(), next);
}

void ModControllableAudio::bindEffects(deluge::effects::EffectStates* record) {
	deluge::model::bindEffects(*effectsOwner(), record);
}

void ModControllableAudio::projectModControllableSnapshot() {
	deluge::model::projectEffects(*effectsOwner());
}
