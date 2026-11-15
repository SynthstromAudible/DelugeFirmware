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

#include "model/global_effectable/global_effectable.h"
#include "definitions_cxx.hpp"
#include "deluge-project/param_automation.h"
#include "dsp-next/core/stereo_sample.h"
#include "effects/codec.h"
#include "effects/compressor.h"
#include "effects/global_effectable/params.h"
#include "effects/plugin.h"
#include "effects/words.h"
#include "gui/l10n/l10n.h"
#include "gui/views/performance_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/led/indicator_leds.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/edits/change_effect.h"
#include "model/fx/chain_params.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/param_projection.h"
#include "model/project_bridge/param_serialization.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/reverb_presets.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/render_scratch.h"
#include "storage/storage_manager.h"
#include "util/etl_string.h"
#include "util/firmware_version.h"
#include "util/functions.h"
#include "util/notify.h"
#include <limits>

using namespace deluge;
namespace params = deluge::modulation::params;
