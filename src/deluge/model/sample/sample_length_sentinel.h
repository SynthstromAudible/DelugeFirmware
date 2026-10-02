/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include <cstdint>

// NOTE: deliberately NOT `deluge::sample` -- that collides with the unrelated
// `deluge::gui::menu_item::sample` namespace (repeat.h et al.) and makes unqualified `sample::`
// references in menus.cpp ambiguous wherever both are reachable via a `using namespace deluge;`.
namespace deluge::sample_length {

/// @brief Sentinel value for `Sample::audioDataLengthBytes` (and its `lengthInSamples`) meaning
///        "not yet known" -- set while a recording is in progress and its final length hasn't been
///        determined yet.
///
/// Standalone so light, non-`Sample` consumers (e.g. the region-port cursor) can test against it
/// without pulling in the full `Sample` class -- which, among other things,
/// declares a `SampleStream` member built from the REAL `SampleStream(Sample&)` constructor,
/// something a differential-test harness shadowing `SampleStream` with a test double can't satisfy.
/// `Sample::kUnknownLengthSentinel` (sample.h) is this same value, re-exported as a class member for
/// existing call sites.
inline constexpr uint64_t kUnknownLengthSentinel = 0x8FFFFFFFFFFFFFFFull;

} // namespace deluge::sample_length
