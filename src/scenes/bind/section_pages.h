#pragma once

/// @file
/// @brief The focused owner's pages, assembled from its published pages and the view its records hold.

#include "controls/page.h"
#include "deluge-project/song.h"
#include "effects/state.h"
#include "scenes/bind/knob_focus.h"

namespace deluge::scenes::bind {

/// @brief What changes an owner's pages beyond the song's records.
struct PageInputs {
	effects::EffectStates effects{}; ///< the chain's effects: its mod FX type picks which mod FX pages exist
	bool fm{false};                  ///< the sound is FM: its filter knob does not cycle
	bool alt_delay{false};           ///< the alternative delay knobs: the delay presses step the sync settings
};

/// @brief The page @p focus's knobs are on now, in the mode its owner's records hold: what a turn and a mod button's
///        popup read. It builds no cycle and allocates nothing, so the knobs' indicators can read it every tick.
/// @param song   The song, whose records hold the mode, a sound's controls, a chain's view and a MIDI track's CCs.
/// @param focus  What the knobs point at; its compressor view picks the song's compressor page.
/// @param inputs What changes the page beyond the records.
/// @return A sound's saved knobs; a chain's page of the view its record holds, even a mod FX view its type has no param
///         for (the turn still moves that view's param); a MIDI track's CCs, each pressing to pick its CC, and with
///         no clip nothing to move. No focus or a CV track has an empty page.
[[nodiscard]] controls::Page current_page(const project::Song& song, const KnobFocus& focus, const PageInputs& inputs);

/// @brief The pages @p focus's knobs are on now, in the mode its owner's records hold.
/// @param song   The song, whose records hold the mode, a sound's controls, a chain's view and a MIDI track's CCs.
/// @param focus  What the knobs point at; its compressor view picks the song's compressor page.
/// @param inputs What changes the pages beyond the records.
/// @return current_page and the section's cycle: a sound's cycle is the filter knob's, on whichever knob holds the
///         cutoff; a chain's is its section's pages, and on the song's reverb button its compressor's group. A MIDI
///         track, a CV track and no focus have no cycle.
[[nodiscard]] controls::SectionPages section_pages(const project::Song& song, const KnobFocus& focus,
                                                   const PageInputs& inputs);

} // namespace deluge::scenes::bind
