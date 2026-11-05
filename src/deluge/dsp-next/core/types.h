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

#include "dsp-next/core/sample.h"        // q31, f32, SampleType
#include "dsp-next/core/stereo_sample.h" // StereoSample
#include <span>

/// @file
/// @brief The signal vocabulary for dsp-next components: samples, blocks, stereo.
///
/// A @ref Buffer is a writable block a component fills; a @ref Signal is a read-only
/// block it consumes. The fixed-point backend's sample is q31, fixed_point.h's Q<31>: layout-compatible with the
/// app's int32 buffers, which buffer_view.h views as it. Stereo blocks are spans of @ref StereoSample frames.
namespace deluge::dsp {

/// @brief A single audio/control sample of type @p T.
template <typename T>
using Sample = T;

/// @brief A writable, contiguous block of samples (a component's output region).
template <typename T>
using Buffer = std::span<T>;

/// @brief A read-only, contiguous block of samples (a component's input region).
template <typename T>
using Signal = std::span<const T>;

/// @brief Concrete aliases for the bit-exact q31 backend (the Deluge core) — the same
/// namespace its kernels live in (deluge::dsp::fixed::sine, ::TableOscillator, ...).
namespace fixed {
using Sample = dsp::Sample<q31>;
using Buffer = dsp::Buffer<q31>;
using Signal = dsp::Signal<q31>;
/// @brief A read-only block of interleaved stereo frames (Q31 left, Q31 right).
using StereoSignal = std::span<const StereoSample>;
/// @brief A writable block of interleaved stereo frames (Q31 left, Q31 right).
using StereoBuffer = std::span<StereoSample>;
} // namespace fixed

/// @brief Concrete aliases for the float32 edge backend (engines/CLAP, later). `fp`
/// because `float` is a C++ keyword.
namespace fp {
using Sample = dsp::Sample<f32>;
using Buffer = dsp::Buffer<f32>;
using Signal = dsp::Signal<f32>;
} // namespace fp

} // namespace deluge::dsp
