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

#include "deluge-project/codec.h"
#include "deluge-project/groove.h"
#include "deluge-project/reverb_settings.h"
#include "deluge-project/scale_settings.h"
#include "deluge-project/transport.h"
#include "serialization/value.h"

#include <cstdint>
#include <expected>

namespace deluge::project {

// The song-level records. Each encode returns a Map of the record's entries, which the song's Map takes in; each
// decode reads them from the song's Map (the reverb from its own `reverb` Map), keeping any value the file lacks.

/// @brief The tempo: `timePerTimerTick` and `timerTickFraction` (the tick length's high and low 32 bits) and
/// `inputTickMagnitude` (the magnitude plus its sync-scaling offset).
/// @param transport The tempo to encode.
/// @return The record's entries, as a Map.
[[nodiscard]] serialization::Value encode(const Transport& transport);
[[nodiscard]] std::expected<void, LoadError> decode(const serialization::Value& song, Transport& transport);

/// @brief The swing: `swingAmount` (−49..49) and `swingInterval`, a sync level stored relative to
/// @p input_tick_magnitude.
/// @param groove               The swing to encode.
/// @param input_tick_magnitude The magnitude the swing interval's sync level is stored relative to.
/// @return The record's entries, as a Map.
[[nodiscard]] serialization::Value encode(const Groove& groove, int32_t input_tick_magnitude);
[[nodiscard]] std::expected<void, LoadError> decode(const serialization::Value& song, Groove& groove,
                                                    const DecodeContext& context);

/// @brief The key and scales: `rootNote`, the `modeNotes` list and `scales` (`userScale`,
/// `disabledPresetScales`, each as bits).
/// @param scale The key and scales to encode.
/// @return The record's entries, as a Map.
[[nodiscard]] serialization::Value encode(const ScaleSettings& scale);
[[nodiscard]] std::expected<void, LoadError> decode(const serialization::Value& song, ScaleSettings& scale);

/// @brief The reverb send, as the `reverb` Map's entries: the room, damping, width and filters as Q31 fractions,
/// `pan`, and the `compressor` (its sync level relative to @p input_tick_magnitude). The algorithm (`model`) is
/// the engine's and is not part of the record.
/// @param reverb               The reverb send to encode.
/// @param input_tick_magnitude The magnitude the compressor's sync level is stored relative to.
/// @return The record's entries, as a Map.
[[nodiscard]] serialization::Value encode(const ReverbSettings& reverb, int32_t input_tick_magnitude);
[[nodiscard]] std::expected<void, LoadError> decode(const serialization::Value& reverb, ReverbSettings& settings,
                                                    const DecodeContext& context);

} // namespace deluge::project
