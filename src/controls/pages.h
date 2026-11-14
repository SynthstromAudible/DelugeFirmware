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
/// @brief The focused owner's pages, assembled from the records its owner publishes and the view the song's records
///        hold.

#include "controls/board.h"
#include "controls/knob_focus.h"
#include "controls/page.h"
#include "plugin-api/remote_controls_publisher.h"

#include <span>

namespace deluge::project {
class Song;
} // namespace deluge::project

namespace deluge::controls {

/// @brief What assembling the focused owner's pages reads beyond the song: the publishers, the device facts and the
///        board.
struct PageSources {
	std::span<const plugin::RemoteControls> publishers;
	bool alt_delay = false;
	Board board; ///< the board's slots per section: a MIDI track names a CC for each
};

/// @brief The page the knobs are on now. Allocation-free: the indicators read it every tick.
/// @param song    The song, whose records hold the mode, a sound's controls and state, a chain's view and state, and
///                a MIDI track's CCs.
/// @param focus   What the knobs point at; its compressor view picks the song's compressor page.
/// @param sources The publishers (a sound's pages are the Deluge instrument's, a chain's the global effectable's) and
///                the device facts.
/// @return A sound's saved knobs, each decorated by its publisher; a chain's page of the view its record holds, even a
///         mod FX view its type has no param for (the turn still moves that view's param); a MIDI track's CCs, each
///         pressing to pick its CC, and with no clip nothing to move. No focus, a CV track or an owner with no
///         publisher has an empty page.
[[nodiscard]] Page current_page(const project::Song& song, const KnobFocus& focus, const PageSources& sources);

/// @brief The current section's pages, in NEXT_PAGE order, and each group's entry page.
/// @param song    The song, as for current_page.
/// @param focus   What the knobs point at.
/// @param sources The publishers and the device facts.
/// @return current_page and the section's cycle: a sound's cycle is the pages its publisher finds on the section (the
///         filter knob's two groups, on whichever knob holds the cutoff); a chain's is its publisher's device pages of
///         the section, and on the song's reverb button its compressor's group. A MIDI track, a CV track and no focus
///         have no cycle.
[[nodiscard]] SectionPages section_pages(const project::Song& song, const KnobFocus& focus, const PageSources& sources);

} // namespace deluge::controls
