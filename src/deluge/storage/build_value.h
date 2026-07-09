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

#include "storage/deserializer.h"
#include "storage/value.h"

namespace deluge::storage {

/// Reconstructs a `Value` tree by generically, schema-free-ly, driving any `Deserializer`'s
/// streaming interface.
///
/// `reader` must already be positioned *inside* an object box -- i.e. a prior `match('{')` (the
/// caller's own, or the previous stack frame's) has descended into the map this call reads names
/// from, exactly as real `readFromFile(Deserializer&)` bodies expect. buildValue() then loops
/// `readNextTagOrAttributeName()` until it returns "" (the interface's end-of-object signal),
/// building one Map entry per name:
///   - `match('{')` succeeds  -> the value is itself an object; recurse, then `exitTag(nullptr,
///     true)` to pop the frame the `match('{')` pushed.
///   - `match('[')` succeeds  -> the value is an array; loop `match('{')` over its elements
///     (each one a recursive buildValue() + `exitTag(nullptr, true)` to pop that element's frame),
///     then `match(']')` to leave the array frame.
///   - otherwise              -> a scalar; `readTagOrAttributeValue()` then `exitTag()`.
///
/// This is schema-free: it doesn't know or care what any name means, only whether the
/// Deserializer says the next thing is an object, an array, or a scalar. That makes it the
/// throwaway/generic path suitable for driving a real `XMLDeserializer` (deferred to Step 2's
/// golden load/save); here it is validated by round-tripping through the `ValueTreeDeserializer`
/// adapter, which needs no file.
[[nodiscard]] Value buildValue(Deserializer& reader);

} // namespace deluge::storage
