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

/// @brief The pages @p focus's knobs are on now, in the mode its owner's records hold.
/// @param song   The song, whose records hold the mode, a sound's controls, a chain's view and a MIDI track's CCs.
/// @param focus  What the knobs point at; its compressor view picks the song's compressor page.
/// @param inputs What changes the pages beyond the records.
/// @return The current page and the section's cycle. A sound's current page is its saved knobs, and its cycle the
///         filter knob's, on whichever knob holds the cutoff; a chain's current page is the page of the view its
///         record holds, even a mod FX view its type has no param for (the turn still moves that view's param). A
///         MIDI track's knobs press to pick their CC, and with no clip move nothing. No focus or a CV track has an
///         empty page.
[[nodiscard]] controls::SectionPages section_pages(const project::Song& song, const KnobFocus& focus,
                                                   const PageInputs& inputs);

} // namespace deluge::scenes::bind
