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
#pragma once

#include "model/mod_controllable/filters/filter_config.h" // FilterMode, FilterRoute
#include <cstdint>

/// @brief A per-sound snapshot of the render config the voice kernel consumes — built by the adapter
/// (`Sound::render`) and handed to `Voice::render`. The first step of severing `Voice::render`'s `Sound&`
/// coupling (Phase 4, see `docs/dev/synth_phase4_voice_kernel.md`): config the render used to read inline off
/// `sound` now arrives through this struct, populated once at the adapter edge.
///
/// Grows cluster-by-cluster across Phase-4 slices (this slice: the filter-set config). App-side for now — its
/// filter-enum fields live under `model/`, which `synth/` may not include; it relocates to `synth/` once the
/// boundary types are numeric-agnostic.
struct VoiceConfig {
	FilterMode lpfMode;                  ///< Sound::lpfMode (the render still gates this with its doLPF arg)
	FilterMode hpfMode;                  ///< Sound::hpfMode (gated with doHPF)
	FilterRoute filterRoute;             ///< Sound::filterRoute
	int32_t volumeNeutralValueForUnison; ///< Sound::volumeNeutralValueForUnison
};
