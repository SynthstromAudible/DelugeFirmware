#pragma once

/// @brief The randomizer page as data: `randomizer` and its twenty-four columns.
///
/// `inline constexpr` for the reason `sound_filters.h` explains (task-6 fix round 1, I3). From
/// `gui/ui/menus.cpp`'s `randomizerMenu` literal (`:599-624`) and the globals it names
/// (`:559-597`).
///
/// **The page is eleven variant pairs plus two singletons.** Every randomizer setting exists twice:
/// once as a `RandomizerUnpatchedParam` (or one of its two narrower siblings) reading an unpatched
/// parameter on a sound, and once as a `randomizer::midi_cv::*` `Integer` reading the same field of
/// `ArpeggiatorSettings` directly, for a MIDI or CV clip that has no param set. The two carry the
/// same name and title and are never relevant at once -- see `validate.cpp`'s `co_visible`, which
/// is why the pairs are not `DuplicateName` reports.
///
// clang-format off
/// | node | legacy global | legacy class | param / field |
/// |---|---|---|---|
/// | `randomizer_lock` | `randomizerLockMenu` (`:543`) | `randomizer::RandomizerLock` (`randomizer_lock.h:26`) | `randomizerLock` |
/// | `spread_gate` | `spreadGateMenu` (`:545`) | `RandomizerUnpatchedParam`, BAR | `UNPATCHED_ARP_SPREAD_GATE` |
/// | `spread_gate_midi` | `spreadGateMenuMIDIOrCV` (`:547`) | `midi_cv::SpreadGate` (`midi_cv/spread_gate.h:24`) | `spreadGate` |
/// | `spread_octave` | `spreadOctaveMenu` (`:549`) | `RandomizerSoundOnlyUnpatchedParam`, BAR | `UNPATCHED_ARP_SPREAD_OCTAVE` |
/// | `spread_octave_midi` | `spreadOctaveMenuMIDIOrCV` (`:551`) | `midi_cv::SpreadOctave` (`spread_octave.h:24`) | `spreadOctave` |
/// | `spread_velocity` | `spreadVelocityMenu` (`:553`) | `RandomizerUnpatchedParam`, BAR | `UNPATCHED_SPREAD_VELOCITY` |
/// | `spread_velocity_midi` | `spreadVelocityMenuMIDIOrCV` (`:555`) | `midi_cv::SpreadVelocity` (`spread_velocity.h:24`) | `spreadVelocity` |
/// | `ratchet_amount` | `ratchetAmountMenu` (`:557`) | `RandomizerUnpatchedParam`, BAR | `UNPATCHED_ARP_RATCHET_AMOUNT` |
/// | `ratchet_amount_midi` | `ratchetAmountMenuMIDIOrCV` (`:559`) | `midi_cv::RatchetAmount` (`ratchet_amount.h:24`) | `ratchetAmount` |
/// | `ratchet_probability` | `ratchetProbabilityMenu` (`:561`) | `RandomizerUnpatchedParam`, PERCENT | `UNPATCHED_ARP_RATCHET_PROBABILITY` |
/// | `ratchet_probability_midi` | `ratchetProbabilityMenuMIDIOrCV` (`:564`) | `midi_cv::RatchetProbability` (`ratchet_probability.h:24`) | `ratchetProbability` |
/// | `chord_polyphony` | `chordPolyphonyMenu` (`:566`) | `RandomizerNonKitSoundUnpatchedParam`, BAR | `UNPATCHED_ARP_CHORD_POLYPHONY` |
/// | `chord_polyphony_midi` | `chordPolyphonyMenuMIDIOrCV` (`:568`) | `midi_cv::ChordPolyphony` (`chord_polyphony.h:24`) | `chordPolyphony` |
/// | `chord_probability` | `chordProbabilityMenu` (`:570`) | `RandomizerNonKitSoundUnpatchedParam`, PERCENT | `UNPATCHED_ARP_CHORD_PROBABILITY` |
/// | `chord_probability_midi` | `chordProbabilityMenuMIDIOrCV` (`:573`) | `midi_cv::ChordProbability` (`chord_probability.h:25`) | `chordProbability` |
/// | `note_probability` | `randomizerNoteProbabilityMenu` (`:575`) | `RandomizerUnpatchedParam`, PERCENT | `UNPATCHED_NOTE_PROBABILITY` |
/// | `note_probability_midi` | `randomizerNoteProbabilityMenuMIDIOrCV` (`:577`) | `midi_cv::NoteProbability` (`note_probability.h:24`) | `noteProbability` |
/// | `swap_probability` | `swapProbabilityMenu` (`:579`) | `RandomizerUnpatchedParam`, PERCENT | `UNPATCHED_ARP_SWAP_PROBABILITY` |
/// | `swap_probability_midi` | `swapProbabilityMenuMIDIOrCV` (`:582`) | `midi_cv::SwapProbability` (`swap_probability.h:24`) | `swapProbability` |
/// | `bass_probability` | `bassProbabilityMenu` (`:584`) | `RandomizerUnpatchedParam`, PERCENT | `UNPATCHED_ARP_BASS_PROBABILITY` |
/// | `bass_probability_midi` | `bassProbabilityMenuMIDIOrCV` (`:587`) | `midi_cv::BassProbability` (`bass_probability.h:25`) | `bassProbability` |
/// | `glide_probability` | `glideProbabilityMenu` (`:590`) | `RandomizerUnpatchedParam`, PERCENT | `UNPATCHED_ARP_GLIDE_PROBABILITY` |
/// | `glide_probability_midi` | `glideProbabilityMenuMIDIOrCV` (`:593`) | `midi_cv::GlideProbability` (`glide_probability.h:25`) | `glideProbability` |
/// | `reverse_probability` | `reverseProbabilityMenu` (`:595`) | `RandomizerUnpatchedParam`, PERCENT | `UNPATCHED_REVERSE_PROBABILITY` |
/// | `randomizer` | `randomizerMenu` (`:599`) | `HorizontalMenu` | Submenu (Horizontal) |
// clang-format on
///
/// @note **The `midi_cv` items use the *unsigned* value scaling** --
///       `computeCurrentValueForUnsignedMenuItem` / `computeFinalValueForUnsignedMenuItem`
///       (`gui/menu_item/value_scaling.cpp:18-20`, `:60-62`) -- because the `ArpeggiatorSettings`
///       fields they read are `uint32_t` (`modulation/arpeggiator.h:139-147`). That is a *third*
///       scaling, distinct from `Scaling::Standard` and `Scaling::HalfPrecision`, but it is not a
///       new `Scaling` enumerator: `Scaling` only ever applies to a `ParamRef` binding
///       (`node.h`'s doc), and none of these is one. The hooks call
///       `bindings::unsigned_to_menu` / `menu_to_unsigned` instead.
///
/// @note **`RandomizerLock` draws a padlock icon** rather than a checkbox
///       (`randomizer_lock.h:52-56`), and is an `isToggle()` `Selection` like the arpeggiator's own
///       mode toggle. Same call as `sound_arp.h`'s: `Kind::Enum` with the two options, no `Hint`.
///       Unlike the arpeggiator's, `randomizer` is a `Layout::Horizontal` page, so
///       `randomizer_lock` sets `Node::selection_is_toggle` -- see its own definition below and
///       `node.h`'s doc.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/column_hooks.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/param_shows.h"
#include "scenes/menu/nodes/sound_arp.h" // arp_row
#include "scenes/menu/relevance.h"

#include <span>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @brief One MIDI/CV randomizer column: the arp's host row at @p path, which a MIDI or CV arp holds as a setting and
///        legacy's items write on the selected arp alone (`randomizer/midi_cv/randomizer_integer.h:22-32`).
#define DELUGE_RANDOMIZER_MIDI(node_name, name_id, title_id, path, hint)                                               \
	inline constexpr Node node_name = with_short_column_label(                                                         \
	    with_title(setting_item(sid(deluge::l10n::String::name_id), Kind::Int, arp_row(path, false), hint),            \
	               sid(deluge::l10n::String::title_id)))

/// @brief One sound-side randomizer column: an unpatched `ParamRef`.
#define DELUGE_RANDOMIZER_PARAM(node_name, name_id, title_id, param_id, rel, hint)                                     \
	inline constexpr Node node_name = with_short_column_label(                                                         \
	    param(sid(deluge::l10n::String::name_id),                                                                      \
	          {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::param_id}, hint, rel,    \
	          sid(deluge::l10n::String::title_id)))

inline constexpr Node randomizer_lock =
    with_column_art(with_short_column_label(with_selection_is_toggle(
                        with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_RANDOMIZER_LOCK), Kind::Enum,
                                                arp_row("arpeggiator/randomizerLock")),
                                   sid(deluge::l10n::String::STRING_FOR_ARP_RANDOMIZER_LOCK_TITLE)))),
                    lock_art);

// `RandomizerUnpatchedParam::isRelevant` (`randomizer_unpatched_param.h:31-42`) -- the non-global
// ids need the arpeggiator on, the three global ones do not.
DELUGE_RANDOMIZER_PARAM(spread_gate, STRING_FOR_SPREAD_GATE, STRING_FOR_ARP_SPREAD_GATE_MENU_TITLE,
                        UNPATCHED_ARP_SPREAD_GATE, param_shows::kRandomizer, Hint::Bar);
DELUGE_RANDOMIZER_MIDI(spread_gate_midi, STRING_FOR_SPREAD_GATE, STRING_FOR_ARP_SPREAD_GATE_MENU_TITLE,
                       "arpeggiator/spreadGate", Hint::Bar);

// `RandomizerSoundOnlyUnpatchedParam::isRelevant` (`randomizer_unpatched_param.h:37-40`).
DELUGE_RANDOMIZER_PARAM(spread_octave, STRING_FOR_SPREAD_OCTAVE, STRING_FOR_ARP_SPREAD_OCTAVE_MENU_TITLE,
                        UNPATCHED_ARP_SPREAD_OCTAVE, param_shows::kRandomizerSoundOnly, Hint::Bar);
DELUGE_RANDOMIZER_MIDI(spread_octave_midi, STRING_FOR_SPREAD_OCTAVE, STRING_FOR_ARP_SPREAD_OCTAVE_MENU_TITLE,
                       "arpeggiator/spreadOctave", Hint::Bar);

DELUGE_RANDOMIZER_PARAM(spread_velocity, STRING_FOR_SPREAD_VELOCITY, STRING_FOR_SPREAD_VELOCITY_MENU_TITLE,
                        UNPATCHED_SPREAD_VELOCITY, ParamShows{}, Hint::Bar);
DELUGE_RANDOMIZER_MIDI(spread_velocity_midi, STRING_FOR_SPREAD_VELOCITY, STRING_FOR_SPREAD_VELOCITY_MENU_TITLE,
                       "arpeggiator/spreadVelocity", Hint::Bar);

DELUGE_RANDOMIZER_PARAM(ratchet_amount, STRING_FOR_NUMBER_OF_RATCHETS, STRING_FOR_ARP_RATCHETS_MENU_TITLE,
                        UNPATCHED_ARP_RATCHET_AMOUNT, param_shows::kRandomizer, Hint::Bar);
DELUGE_RANDOMIZER_MIDI(ratchet_amount_midi, STRING_FOR_NUMBER_OF_RATCHETS, STRING_FOR_ARP_RATCHETS_MENU_TITLE,
                       "arpeggiator/ratchetAmount", Hint::Bar);

DELUGE_RANDOMIZER_PARAM(ratchet_probability, STRING_FOR_RATCHET_PROBABILITY,
                        STRING_FOR_ARP_RATCHET_PROBABILITY_MENU_TITLE, UNPATCHED_ARP_RATCHET_PROBABILITY,
                        param_shows::kRandomizer, Hint::Percent);
DELUGE_RANDOMIZER_MIDI(ratchet_probability_midi, STRING_FOR_RATCHET_PROBABILITY,
                       STRING_FOR_ARP_RATCHET_PROBABILITY_MENU_TITLE, "arpeggiator/ratchetProbability", Hint::Percent);

// `RandomizerNonKitSoundUnpatchedParam::isRelevant` (`randomizer_unpatched_param.h:52-55`).
DELUGE_RANDOMIZER_PARAM(chord_polyphony, STRING_FOR_CHORD_POLYPHONY, STRING_FOR_ARP_CHORD_POLYPHONY_MENU_TITLE,
                        UNPATCHED_ARP_CHORD_POLYPHONY, param_shows::kRandomizerNonKit, Hint::Bar);
DELUGE_RANDOMIZER_MIDI(chord_polyphony_midi, STRING_FOR_CHORD_POLYPHONY, STRING_FOR_ARP_CHORD_POLYPHONY_MENU_TITLE,
                       "arpeggiator/chordPolyphony", Hint::Bar);

DELUGE_RANDOMIZER_PARAM(chord_probability, STRING_FOR_CHORD_PROBABILITY, STRING_FOR_ARP_CHORD_PROBABILITY_MENU_TITLE,
                        UNPATCHED_ARP_CHORD_PROBABILITY, param_shows::kRandomizerNonKit, Hint::Percent);
DELUGE_RANDOMIZER_MIDI(chord_probability_midi, STRING_FOR_CHORD_PROBABILITY,
                       STRING_FOR_ARP_CHORD_PROBABILITY_MENU_TITLE, "arpeggiator/chordProbability", Hint::Percent);

DELUGE_RANDOMIZER_PARAM(note_probability, STRING_FOR_NOTE_PROBABILITY, STRING_FOR_NOTE_PROBABILITY_MENU_TITLE,
                        UNPATCHED_NOTE_PROBABILITY, ParamShows{}, Hint::Percent);
DELUGE_RANDOMIZER_MIDI(note_probability_midi, STRING_FOR_NOTE_PROBABILITY, STRING_FOR_NOTE_PROBABILITY_MENU_TITLE,
                       "arpeggiator/noteProbability", Hint::Percent);

DELUGE_RANDOMIZER_PARAM(swap_probability, STRING_FOR_SWAP_PROBABILITY, STRING_FOR_ARP_SWAP_PROBABILITY_MENU_TITLE,
                        UNPATCHED_ARP_SWAP_PROBABILITY, param_shows::kRandomizer, Hint::Percent);
DELUGE_RANDOMIZER_MIDI(swap_probability_midi, STRING_FOR_SWAP_PROBABILITY, STRING_FOR_ARP_SWAP_PROBABILITY_MENU_TITLE,
                       "arpeggiator/swapProbability", Hint::Percent);

DELUGE_RANDOMIZER_PARAM(bass_probability, STRING_FOR_BASS_PROBABILITY, STRING_FOR_ARP_BASS_PROBABILITY_MENU_TITLE,
                        UNPATCHED_ARP_BASS_PROBABILITY, param_shows::kRandomizer, Hint::Percent);
DELUGE_RANDOMIZER_MIDI(bass_probability_midi, STRING_FOR_BASS_PROBABILITY, STRING_FOR_ARP_BASS_PROBABILITY_MENU_TITLE,
                       "arpeggiator/bassProbability", Hint::Percent);

DELUGE_RANDOMIZER_PARAM(glide_probability, STRING_FOR_GLIDE_PROBABILITY, STRING_FOR_ARP_GLIDE_PROBABILITY_MENU_TITLE,
                        UNPATCHED_ARP_GLIDE_PROBABILITY, param_shows::kRandomizer, Hint::Percent);
DELUGE_RANDOMIZER_MIDI(glide_probability_midi, STRING_FOR_GLIDE_PROBABILITY,
                       STRING_FOR_ARP_GLIDE_PROBABILITY_MENU_TITLE, "arpeggiator/glideProbability", Hint::Percent);

// The one setting with no MIDI/CV twin (`menus.cpp:595-597`).
DELUGE_RANDOMIZER_PARAM(reverse_probability, STRING_FOR_REVERSE_PROBABILITY, STRING_FOR_REVERSE_PROBABILITY_MENU_TITLE,
                        UNPATCHED_REVERSE_PROBABILITY, ParamShows{}, Hint::Percent);

#undef DELUGE_RANDOMIZER_MIDI
#undef DELUGE_RANDOMIZER_PARAM

inline constexpr const Node* kRandomizerChildren[] = {
    &randomizer_lock,       &spread_gate,         &spread_gate_midi,         &spread_octave,
    &spread_octave_midi,    &spread_velocity,     &spread_velocity_midi,     &ratchet_amount,
    &ratchet_amount_midi,   &ratchet_probability, &ratchet_probability_midi, &chord_polyphony,
    &chord_polyphony_midi,  &chord_probability,   &chord_probability_midi,   &note_probability,
    &note_probability_midi, &swap_probability,    &swap_probability_midi,    &bass_probability,
    &bass_probability_midi, &glide_probability,   &glide_probability_midi,   &reverse_probability,
};
inline constexpr Node randomizer =
    submenu(sid(deluge::l10n::String::STRING_FOR_RANDOMIZER), kRandomizerChildren, Layout::Horizontal);

/// @brief This file's contribution to the sound-editor root: `randomizerMenu`.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundRandomizerChildren;

} // namespace deluge::scenes::menu::nodes
