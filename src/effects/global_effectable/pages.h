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
/// @brief The host effect chains' remote-controls pages: what the mod buttons' knobs move on a song, a clip's chain,
///        a kit's affect-entire and an audio track, and what pressing each does.

#include "effects/global_effectable/params.h"
#include "effects/setting_params.h"
#include "effects/words.h"
#include "plugin-api/remote_controls_publisher.h"
#include "plugin-api/types/remote_controls.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace deluge::global_effectable {

static_assert(std::ranges::all_of(kParams,
                                  [](const plugin::ParamInfo& p) { return p.id.value < effects::kFirstSettingId; }),
              "the chain's param ids stay below the setting ids");

/// @brief What changes a chain's pages.
struct PageInputs {
	bool song{false};                                         ///< the song's chain, not a clip's
	effects::ModFXType mod_fx_type{effects::ModFXType::NONE}; ///< the chain's mod FX type; which mod FX pages exist
	bool alt_delay{false};                                    ///< the alternative delay knobs
};

/// @brief The song's compressor settings the lower knob edits: one page each, ids 1 to kCompPages, after the reverb
///        page.
inline constexpr uint32_t kCompPages = 5;

/// @brief Whether a mod FX type has a param for a view.
/// @param type         The chain's mod FX type.
/// @param mod_fx_param The view: 0 depth, 1 feedback, 2 offset.
/// @return True when the type has the param.
[[nodiscard]] bool mod_fx_page_valid(effects::ModFXType type, uint32_t mod_fx_param);

/// @brief The mod FX page of a view, whether or not a type has its param: a chain whose saved view its type lacks
/// still turns that view's param.
/// @param mod_fx_param The view: 0 depth, 1 feedback, 2 offset; past the last, feedback.
/// @return The page of section 5, \c page_id the view.
[[nodiscard]] plugin::RemotePage mod_fx_page(uint32_t mod_fx_param);

/// @brief Page @p page_id of mod button @p section: one page, built with no allocation, for a turn that reads only the
///        page its knobs are on.
/// @param section The mod button.
/// @param page_id The page's id within the section (see section_pages).
/// @param in      What changes the pages.
/// @return The page, or none where the section has no page of that id (a mod FX view @p in's type lacks included).
[[nodiscard]] std::optional<plugin::RemotePage> section_page(uint32_t section, uint32_t page_id, const PageInputs& in);

/// @brief The chain's pages of mod button @p section, in cycle order (one page for a fixed section, none for the
/// envelopes' button 2). Section 1's pages are low-pass, high-pass and EQ; the song's section 4 holds its compressor
/// group (group 1, five pages) after the reverb group (group 0); section 5's are the mod FX params @p in's type has,
/// \c page_id the param's index.
/// @param section The mod button.
/// @param in      What changes the pages.
/// @return The pages.
[[nodiscard]] std::vector<plugin::RemotePage> section_pages(uint32_t section, const PageInputs& in);

/// @brief The chains' remote-controls record: the pages in section order, the mod FX type read from the owner's
/// DelugeEffects state (none without one). Its find reaches a mod FX view the type lacks; its decorate gives a param
/// the slot the pages give it.
extern const plugin::RemoteControls
    kRemoteControls; // NOLINT(bugprone-dynamic-static-initializers): constant-initialized

} // namespace deluge::global_effectable
