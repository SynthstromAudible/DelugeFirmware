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

// libdeluge-dsp — the clean L0 DSP library (synth untangling, Phase 1).
//
// This is the strangler-fig replacement for src/deluge/dsp/: primitives migrate
// here one at a time as idiomatic C++23 (std::span buffers, injected
// scratch/quality/allocator, strong numeric aliases, no globals), their callers
// switch over under golden-master cover, and the old implementation is deleted.
// See docs/dev/synth_phase1_l0_plan.md.
//
// Layout:
//   core/   numeric-agnostic interfaces, std::span block types, strong numeric
//           aliases, q31<->f32 conversion. Header-only. No globals, no upward
//           includes (enforced by scripts/check_dsp_boundary.py — no allowlist).
//   fixed/  q31 backend   -> libdeluge-dsp-fixed   (namespace deluge::dsp::fixed)
//   float/  f32 backend   -> libdeluge-dsp-float   (namespace deluge::dsp::fp;
//                                                    `float` is a C++ keyword)
//
// Umbrella include for the numeric-agnostic core vocabulary. Backends include
// this; app/migration sites can include the specific headers directly.
//
// Layering: sample.h is the scalar vocabulary (q31/f32 + SampleType + conversions);
// types.h builds the block/aggregate vocabulary on it (Sample/Buffer/Signal/StereoSample
// + the fixed::/fp:: backend aliases); concepts.h + generator/processor/mixer are the
// component framework (static render() hot path, virtual renderBlock at block granularity).

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/convert.h"
#include "dsp-next/core/generator.h"
#include "dsp-next/core/mixer.h"
#include "dsp-next/core/phasor.h"
#include "dsp-next/core/processor.h"
#include "dsp-next/core/sample.h"
#include "dsp-next/core/types.h"
