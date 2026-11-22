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

#include "plugin/remote_controls_publisher.h"
#include "ui/controls/board.h"
#include "ui/controls/knob_focus.h"
#include "ui/controls/page.h"

#include <array>
#include <cstdint>
#include <span>

namespace deluge::project {
class Song;
struct FxChain;
} // namespace deluge::project

namespace deluge::ui::controls {

/// @brief Where a chain's view puts its knobs on one section.
struct ChainView {
	uint32_t current{0};                   ///< the page id the knobs are on
	std::array<uint32_t, 2> group_entry{}; ///< the page id TOGGLE_GROUP enters each group at
};

/// @brief The host's reading of a chain's view: which of the values the chain's record and the knobs' compressor
///        view hold names the page of @p section.
/// @param chain   The chain's record; nullptr for a chain that does not resolve (a new chain's view).
/// @param comp    The knobs' compressor view.
/// @param song    The chain is the song's.
/// @param section The section.
using ChainViewOf = ChainView (*)(const project::FxChain* chain, const CompView& comp, bool song, uint32_t section);

/// @brief What assembling the focused owner's pages reads beyond the song: the publishers, the device facts, the
///        board and the host's reading of a chain's view.
struct PageSources {
	std::span<const plugin::RemoteControls> publishers;
	bool alt_delay = false;
	Board board; ///< the board's sections and its slots per section; required (the mode panics on no section, a MIDI
	             ///< page on no slot)
	/// The host's reading of a chain's view; required (a chain's page panics on none).
	ChainViewOf chain_view = nullptr;
};

/// @brief The page the knobs are on now. Allocation-free: the indicators read it every tick.
/// @param song    The song, whose records hold the mode, a sound's controls and state, a chain's view and state, and
///                a MIDI track's CCs.
/// @param focus   What the knobs point at; its compressor view picks the song's compressor page.
/// @param sources The publishers (a sound's is the one its slot's plugin names, a chain's the global effectable's) and
///                the device facts.
/// @return A sound's saved knobs, each decorated by its publisher; a chain's page of the view its record holds (as
///         @p sources' chain_view reads it), even one the publisher serves outside its device pages; a MIDI track's
///         CCs, each pressing to pick its CC, and with no clip nothing to move. No focus, an owner with no controls
///         (a CV track, a MIDI or gate drum) or an owner with no publisher has an empty page.
[[nodiscard]] Page current_page(const project::Song& song, const KnobFocus& focus, const PageSources& sources);

/// @brief The current section's pages, in NEXT_PAGE order, and each group's entry page.
/// @param song    The song, as for current_page.
/// @param focus   What the knobs point at.
/// @param sources The publishers and the device facts.
/// @return current_page and the section's cycle: a sound's cycle is the pages its publisher finds on the section (the
///         filter knob's two groups, on whichever knob holds the cutoff); a chain's is its publisher's device pages of
///         the section, with the group entries @p sources' chain_view reads. A MIDI track, an owner with no controls
///         (a CV track, a MIDI or gate drum) and no focus have no cycle.
[[nodiscard]] SectionPages section_pages(const project::Song& song, const KnobFocus& focus, const PageSources& sources);

/// @brief Whether pressing section @p section keeps the focused owner's playing stutter running: one of its
///        publisher's device pages of that section says so. Allocation-free.
/// @param song    The song, as for current_page.
/// @param focus   What the knobs point at.
/// @param section The pressed section.
/// @param sources The publishers and the device facts.
/// @return False for no focus, a MIDI track, an owner with no controls (a CV track, a MIDI or gate drum), or an owner
///         with no publisher.
[[nodiscard]] bool section_keeps_stutter(const project::Song& song, const KnobFocus& focus, uint32_t section,
                                         const PageSources& sources);

} // namespace deluge::ui::controls
