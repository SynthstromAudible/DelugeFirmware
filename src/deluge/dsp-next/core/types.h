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

#include "dsp-next/core/sample.h" // q31, f32, SampleType
#include <span>

/// @file
/// @brief The signal vocabulary for dsp-next components: samples, blocks, stereo.
///
/// A @ref Buffer is a writable block a component fills; a @ref Signal is a read-only
/// block it consumes. Per decision Q1 the concrete sample type at component interfaces
/// is q31 (= int32_t) — zero friction with the app's buffers; FixedPoint<31> is a
/// later, layout-compatible type-safety layer.
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

// NOTE: a stereo sample type is deliberately *not* defined here yet — the app already
// has a concrete `deluge::dsp::StereoSample`, and a second (templated) one in this
// namespace would clash. dsp-next's stereo vocabulary lands when stereo components are
// built, reconciled with the app's type. The current (oscillator/VCA) path is mono.

/// @brief Concrete aliases for the bit-exact q31 backend (the Deluge core) — the same
/// namespace its kernels live in (deluge::dsp::fixed::sine, ::TableOscillator, ...).
namespace fixed {
using Sample = dsp::Sample<q31>;
using Buffer = dsp::Buffer<q31>;
using Signal = dsp::Signal<q31>;
} // namespace fixed

/// @brief Concrete aliases for the float32 edge backend (engines/CLAP, later). `fp`
/// because `float` is a C++ keyword.
namespace fp {
using Sample = dsp::Sample<f32>;
using Buffer = dsp::Buffer<f32>;
using Signal = dsp::Signal<f32>;
} // namespace fp

} // namespace deluge::dsp
