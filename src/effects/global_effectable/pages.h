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

#include "effects/words.h"
#include "plugin-api/types/remote_controls.h"

#include <cstdint>
#include <vector>

namespace deluge::global_effectable {

/// @brief What changes a chain's pages.
struct PageInputs {
	bool song{false};                                         ///< the song's chain, not a clip's
	effects::ModFXType mod_fx_type{effects::ModFXType::NONE}; ///< the chain's mod FX type; which mod FX pages exist
	bool alt_delay{false};                                    ///< the alternative delay knobs
};

/// @brief Whether a mod FX type has a param for a view.
/// @param type         The chain's mod FX type.
/// @param mod_fx_param The view: 0 depth, 1 feedback, 2 offset.
/// @return True when the type has the param.
[[nodiscard]] bool mod_fx_page_valid(effects::ModFXType type, uint32_t mod_fx_param);

/// @brief The chain's pages of mod button @p section, in cycle order (one page for a fixed section, none for the
/// envelopes' button 2). Section 1's pages are low-pass, high-pass and EQ; the song's section 4 holds its compressor
/// group (group 1, five pages) after the reverb group (group 0); section 5's are the mod FX params @p in's type has,
/// \c page_id the param's index.
/// @param section The mod button.
/// @param in      What changes the pages.
/// @return The pages.
[[nodiscard]] std::vector<plugin::RemotePage> section_pages(uint32_t section, const PageInputs& in);

} // namespace deluge::global_effectable
