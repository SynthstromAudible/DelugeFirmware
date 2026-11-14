#pragma once

/// @file
/// @brief What the app's gold knobs assemble their pages from: the owners' published records and the device facts.

#include "controls/facts.h"
#include "controls/pages.h"
#include "plugin-api/remote_controls_publisher.h"

#include <span>

namespace deluge::scenes::bind {

/// @brief The owners' remote-controls records: the Deluge instrument's (a sound's pages) and the global effectable's
///        (a chain's).
/// @return The records, one per plugin id (see plugin::remote_controls_for).
[[nodiscard]] std::span<const plugin::RemoteControls> app_publishers();

/// @brief The Deluge's reading of a chain's view: the filter button is on the filter type the chain's record holds (EQ
///        past it), the mod FX button on its mod FX view, and the song's reverb button on the compressor setting the
///        knobs' compressor view edits; every other section is on page 0. TOGGLE_GROUP enters the reverb page and that
///        compressor setting's page.
/// @param chain   The chain's record; nullptr for a new chain's view.
/// @param comp    The knobs' compressor view.
/// @param song    The chain is the song's.
/// @param section The mod button.
/// @return The page the knobs are on, and each group's entry.
[[nodiscard]] controls::ChainView chain_view(const project::FxChain* chain, const controls::CompView& comp, bool song,
                                             uint32_t section);

/// @brief The app's page sources for a turn: its publishers and its reading of a chain's view, the alternative delay
///        knobs off (only a press reads them).
/// @param board The board the knobs are on.
/// @return The sources controls::current_page reads for a turn.
[[nodiscard]] controls::PageSources app_turn_sources(const controls::Board& board);

/// @brief The app's page sources: its publishers, its reading of a chain's view, and the device facts @p facts
///        answers.
/// @param facts The host's facts, which hold the alternative delay-knob setting.
/// @param board The board the knobs are on.
/// @return The sources controls::current_page and controls::section_pages read.
[[nodiscard]] controls::PageSources app_page_sources(const controls::Facts& facts, const controls::Board& board);

} // namespace deluge::scenes::bind
