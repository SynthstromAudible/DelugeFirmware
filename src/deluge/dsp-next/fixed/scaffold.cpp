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

// Scaffold translation unit for libdeluge-dsp-fixed (synth untangling Phase 1, S0).
// The q31 DSP primitives migrate here per docs/dev/synth_phase1_l0_plan.md.
// Including the core umbrella exercises the include wiring at build time; delete
// this file once the first real source lands.
#include "dsp-next/core/dsp.h"

namespace deluge::dsp::fixed {} // namespace anchor
