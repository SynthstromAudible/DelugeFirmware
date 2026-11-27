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

/// @file
/// @brief The params' display names, in the current language, keyed by the neutral kinds and ids.

#include "effects/words.h"
#include "plugin/types/param.h"
#include "plugin/types/param_kind.h"
#include "project/owner_plugin.h"

#include "synth/modulation/envelope_state.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::l10n {

/// @brief A sound's four envelopes' modes, which name some of their params.
using EnvelopeModes = std::array<synth::EnvelopeMode, 4>;

/// @brief The name of an envelope param its envelope's mode gives another job: an AHD's decay is its hold and its
///        release its decay; an AR's or MPE's sustain is its level.
/// @param kind  The param's collection.
/// @param index The param's index in its collection.
/// @param modes The sound's envelopes' modes.
/// @param brief The short form, as a cable's destination shows.
/// @return The name; nullopt where the param does its usual job.
[[nodiscard]] std::optional<std::string_view> envelope_param_name_at(plugin::ParamKind kind, uint32_t index,
                                                                     const EnvelopeModes& modes, bool brief = false);

/// @brief The name of the instrument's envelope param @p id where its envelope's mode gives it another job: an AHD's
///        decay is its hold and its release its decay; an AR's or MPE's sustain is its level.
/// @param id    A param id of the instrument.
/// @param modes The sound's envelopes' modes.
/// @param brief The short form, as a cable's destination shows.
/// @return The name; nullopt where the param does its usual job.
[[nodiscard]] std::optional<std::string_view> envelope_param_name(plugin::ParamId id, const EnvelopeModes& modes,
                                                                  bool brief = false);

/// @brief The display name of the param @p id in @p plugin's table.
/// @param plugin Whose table the id is in.
/// @param id     The param's id.
/// @return The name; "None" for an id the table does not hold.
[[nodiscard]] std::string_view param_name(project::OwnerPlugin plugin, plugin::ParamId id);

/// @brief As the plugin form of @c param_name, with the morph params named after what the sound's filter modes make
///        them.
/// @param plugin Whose table the id is in.
/// @param id     The param's id.
/// @param lpf    The low-pass filter's mode.
/// @param hpf    The high-pass filter's mode.
/// @return The name.
[[nodiscard]] std::string_view param_name(project::OwnerPlugin plugin, plugin::ParamId id, effects::FilterMode lpf,
                                          effects::FilterMode hpf);

/// @brief The display name of a param.
/// @param kind The param's collection.
/// @param id   The param's id, as @c instruments::classic::runtime_param_id numbers it (an unpatched param's id starts
/// at 90).
/// @return The name; "None" for an id the kind does not hold and for a kind with no names.
[[nodiscard]] std::string_view param_name(plugin::ParamKind kind, plugin::ParamId id);

/// @brief As @c param_name, with the morph params named after what the sound's filter modes make them: the ladder
///        filters drive and FM where the SVF modes morph.
/// @param kind The param's collection.
/// @param id   The param's id.
/// @param lpf  The low-pass filter's mode.
/// @param hpf  The high-pass filter's mode.
/// @return The name.
[[nodiscard]] std::string_view param_name(plugin::ParamKind kind, plugin::ParamId id, effects::FilterMode lpf,
                                          effects::FilterMode hpf);

/// @brief The display name of the param at a runtime index.
/// @param kind  The param's collection.
/// @param index The param's index in its collection, as the runtime numbers it; an index no param has (a cast
///              negative one, a patch cable's packed word) names none.
/// @return The name; "None" where there is none.
[[nodiscard]] std::string_view param_name_at(plugin::ParamKind kind, uint32_t index);

/// @brief As @c param_name_at, with the morph params named after the filter modes.
/// @param kind  The param's collection.
/// @param index The param's index in its collection.
/// @param lpf   The low-pass filter's mode.
/// @param hpf   The high-pass filter's mode.
/// @return The name.
[[nodiscard]] std::string_view param_name_at(plugin::ParamKind kind, uint32_t index, effects::FilterMode lpf,
                                             effects::FilterMode hpf);

/// @brief A patched param's short name, as the mod matrix's display shows it (at most ten characters).
/// @param id The param's id.
/// @return The name; empty for an id that is no patched param.
[[nodiscard]] std::string_view patched_param_short_name(plugin::ParamId id);

/// @brief As @c patched_param_short_name, with the morph params named after the filter modes.
/// @param id  The param's id.
/// @param lpf The low-pass filter's mode.
/// @param hpf The high-pass filter's mode.
/// @return The name.
[[nodiscard]] std::string_view patched_param_short_name(plugin::ParamId id, effects::FilterMode lpf,
                                                        effects::FilterMode hpf);

/// @brief The short name of the patched param at a runtime index.
/// @param index The param's index in the patched set.
/// @return The name; empty for an index no patched param has.
[[nodiscard]] std::string_view patched_param_short_name_at(uint32_t index);

/// @brief As @c patched_param_short_name_at, with the morph params named after the filter modes.
/// @param index The param's index in the patched set.
/// @param lpf   The low-pass filter's mode.
/// @param hpf   The high-pass filter's mode.
/// @return The name.
[[nodiscard]] std::string_view patched_param_short_name_at(uint32_t index, effects::FilterMode lpf,
                                                           effects::FilterMode hpf);

} // namespace deluge::l10n
