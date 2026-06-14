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

// Scaffold translation unit for libdeluge-dsp-float (synth untangling Phase 1, S0).
// The f32 edge-track primitives migrate here per docs/dev/synth_phase1_l0_plan.md;
// this track is stood up structurally now and populated later (the core stays q31
// for bit-exactness — see Part 2 decision 1). Delete this file once the first real
// source lands. Namespace is `fp` because `float` is a C++ keyword.
#include "dsp-next/core/dsp.h"

namespace deluge::dsp::fp {} // namespace deluge::dsp::fp
