#pragma once

/// @brief The randomizer page as data: `randomizer` and its twenty-four columns.
///
/// `inline constexpr` for the reason `sound_filters.h` explains. From
/// `origin/main:src/deluge/gui/ui/menus.cpp`'s `randomizerMenu` literal and the globals it names.
///
/// **The page is eleven variant pairs plus two singletons.** Every randomizer setting exists twice:
/// once as a `RandomizerUnpatchedParam` (or one of its two narrower siblings) reading an unpatched
/// parameter on a sound, and once as a `randomizer::midi_cv::*` `Integer` reading the same field of
/// `ArpeggiatorSettings` directly, for a MIDI or CV clip that has no param set. The two carry the
/// same name and title and are never relevant at once -- see `validate.cpp`'s `co_visible`, which
/// is why the pairs are not `DuplicateName` reports.
///
// clang-format off
/// | node | legacy global | legacy class | legacy param / field |
/// |---|---|---|---|
/// | `randomizer_lock` | `randomizerLockMenu` | `randomizer::RandomizerLock` (`origin/main:src/deluge/gui/menu_item/randomizer/randomizer_lock.h`) | `randomizerLock` |
/// | `spread_gate` | `spreadGateMenu` | `RandomizerUnpatchedParam`, BAR | `UNPATCHED_ARP_SPREAD_GATE` |
/// | `spread_gate_midi` | `spreadGateMenuMIDIOrCV` | `midi_cv::SpreadGate` (`origin/main:src/deluge/gui/menu_item/randomizer/midi_cv/spread_gate.h`) | `spreadGate` |
/// | `spread_octave` | `spreadOctaveMenu` | `RandomizerSoundOnlyUnpatchedParam`, BAR | `UNPATCHED_ARP_SPREAD_OCTAVE` |
/// | `spread_octave_midi` | `spreadOctaveMenuMIDIOrCV` | `midi_cv::SpreadOctave` (`origin/main:src/deluge/gui/menu_item/randomizer/midi_cv/spread_octave.h`) | `spreadOctave` |
/// | `spread_velocity` | `spreadVelocityMenu` | `RandomizerUnpatchedParam`, BAR | `UNPATCHED_SPREAD_VELOCITY` |
/// | `spread_velocity_midi` | `spreadVelocityMenuMIDIOrCV` | `midi_cv::SpreadVelocity` (`origin/main:src/deluge/gui/menu_item/randomizer/midi_cv/spread_velocity.h`) | `spreadVelocity` |
/// | `ratchet_amount` | `ratchetAmountMenu` | `RandomizerUnpatchedParam`, BAR | `UNPATCHED_ARP_RATCHET_AMOUNT` |
/// | `ratchet_amount_midi` | `ratchetAmountMenuMIDIOrCV` | `midi_cv::RatchetAmount` (`origin/main:src/deluge/gui/menu_item/randomizer/midi_cv/ratchet_amount.h`) | `ratchetAmount` |
/// | `ratchet_probability` | `ratchetProbabilityMenu` | `RandomizerUnpatchedParam`, PERCENT | `UNPATCHED_ARP_RATCHET_PROBABILITY` |
/// | `ratchet_probability_midi` | `ratchetProbabilityMenuMIDIOrCV` | `midi_cv::RatchetProbability` (`origin/main:src/deluge/gui/menu_item/randomizer/midi_cv/ratchet_probability.h`) | `ratchetProbability` |
/// | `chord_polyphony` | `chordPolyphonyMenu` | `RandomizerNonKitSoundUnpatchedParam`, BAR | `UNPATCHED_ARP_CHORD_POLYPHONY` |
/// | `chord_polyphony_midi` | `chordPolyphonyMenuMIDIOrCV` | `midi_cv::ChordPolyphony` (`origin/main:src/deluge/gui/menu_item/randomizer/midi_cv/chord_polyphony.h`) | `chordPolyphony` |
/// | `chord_probability` | `chordProbabilityMenu` | `RandomizerNonKitSoundUnpatchedParam`, PERCENT | `UNPATCHED_ARP_CHORD_PROBABILITY` |
/// | `chord_probability_midi` | `chordProbabilityMenuMIDIOrCV` | `midi_cv::ChordProbability` (`origin/main:src/deluge/gui/menu_item/randomizer/midi_cv/chord_probability.h`) | `chordProbability` |
/// | `note_probability` | `randomizerNoteProbabilityMenu` | `RandomizerUnpatchedParam`, PERCENT | `UNPATCHED_NOTE_PROBABILITY` |
/// | `note_probability_midi` | `randomizerNoteProbabilityMenuMIDIOrCV` | `midi_cv::NoteProbability` (`origin/main:src/deluge/gui/menu_item/randomizer/midi_cv/note_probability.h`) | `noteProbability` |
/// | `swap_probability` | `swapProbabilityMenu` | `RandomizerUnpatchedParam`, PERCENT | `UNPATCHED_ARP_SWAP_PROBABILITY` |
/// | `swap_probability_midi` | `swapProbabilityMenuMIDIOrCV` | `midi_cv::SwapProbability` (`origin/main:src/deluge/gui/menu_item/randomizer/midi_cv/swap_probability.h`) | `swapProbability` |
/// | `bass_probability` | `bassProbabilityMenu` | `RandomizerUnpatchedParam`, PERCENT | `UNPATCHED_ARP_BASS_PROBABILITY` |
/// | `bass_probability_midi` | `bassProbabilityMenuMIDIOrCV` | `midi_cv::BassProbability` (`origin/main:src/deluge/gui/menu_item/randomizer/midi_cv/bass_probability.h`) | `bassProbability` |
/// | `glide_probability` | `glideProbabilityMenu` | `RandomizerUnpatchedParam`, PERCENT | `UNPATCHED_ARP_GLIDE_PROBABILITY` |
/// | `glide_probability_midi` | `glideProbabilityMenuMIDIOrCV` | `midi_cv::GlideProbability` (`origin/main:src/deluge/gui/menu_item/randomizer/midi_cv/glide_probability.h`) | `glideProbability` |
/// | `reverse_probability` | `reverseProbabilityMenu` | `RandomizerUnpatchedParam`, PERCENT | `UNPATCHED_REVERSE_PROBABILITY` |
/// | `randomizer` | `randomizerMenu` | `HorizontalMenu` | Submenu (Horizontal) |
// clang-format on
///
/// @note **The `midi_cv` items use the *unsigned* value scaling** --
///       `computeCurrentValueForUnsignedMenuItem` / `computeFinalValueForUnsignedMenuItem`
///       (`util/value_scaling.cpp`) -- because the `ArpeggiatorSettings`
///       fields they read are `uint32_t` (`modulation/arpeggiator.h`). That is a *third*
///       scaling, distinct from `Scaling::Standard` and `Scaling::HalfPrecision`, but it is not a
///       new `Scaling` enumerator: `Scaling` only ever applies to a `ParamRef` binding
///       (`node.h`'s doc), and none of these is one. The hooks call
///       `bindings::unsigned_to_menu` / `menu_to_unsigned` instead.
///
/// @note **`RandomizerLock` draws a padlock icon** rather than a checkbox (`RandomizerLock::renderInHorizontalMenu` in
///       `origin/main:src/deluge/gui/menu_item/randomizer/randomizer_lock.h`), and is an `isToggle()` `Selection` like
///       the arpeggiator's own mode toggle. Same call as `sound_arp.h`'s: `Kind::Enum` with the two options, no `Hint`.
///       Unlike the arpeggiator's, `randomizer` is a `Layout::Horizontal` page, so `randomizer_lock` sets
///       `Node::selection_is_toggle` -- see its own definition below and `node.h`'s doc.

#include "arp/params.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/column_hooks.h"
#include "ui/scenes/menu/nodes/detail.h"
#include "ui/scenes/menu/nodes/param_shows.h"
#include "ui/scenes/menu/nodes/sound_arp.h" // arp_row
#include "ui/scenes/menu/relevance.h"

#include <span>

namespace deluge::ui::scenes::menu {
struct MenuContext;
} // namespace deluge::ui::scenes::menu

namespace deluge::ui::scenes::menu::nodes {

/// @brief One MIDI/CV randomizer column: the arp's host row at @p path, which a MIDI or CV arp holds as a setting and
///        legacy's items write on the selected arp alone (each item's `writeCurrentValue` under
///        `origin/main:src/deluge/gui/menu_item/randomizer/midi_cv/`).
#define DELUGE_RANDOMIZER_MIDI(node_name, name_id, title_id, path, hint)                                               \
	inline constexpr Node node_name = with_short_column_label(                                                         \
	    with_title(setting_item(sid(deluge::l10n::String::name_id), Kind::Int, arp_row(path, false), hint),            \
	               sid(deluge::l10n::String::title_id)))

/// @brief One sound-side randomizer column: an unpatched `ParamRef`, never with a MIDI or gate drum selected, which
///        the kit's own effects keep selected (`RandomizerUnpatchedParam::isRelevant`'s `editingNonAudioDrumRow()`).
#define DELUGE_RANDOMIZER_PARAM(node_name, name_id, title_id, param_id, rel, hint)                                     \
	inline constexpr Node node_name =                                                                                  \
	    with_relevant(with_short_column_label(param(sid(deluge::l10n::String::name_id), arp_param(param_id), hint,     \
	                                                rel, sid(deluge::l10n::String::title_id))),                        \
	                  relevance::not_non_audio_drum_row)

inline constexpr Node randomizer_lock =
    with_affect_entire(with_column_art(with_short_column_label(with_flips_in_list(with_selection_is_toggle(with_title(
                                           setting_item(sid(deluge::l10n::String::STRING_FOR_RANDOMIZER_LOCK),
                                                        Kind::Enum, arp_row("arpeggiator/randomizerLock")),
                                           sid(deluge::l10n::String::STRING_FOR_ARP_RANDOMIZER_LOCK_TITLE))))),
                                       lock_art));

// `RandomizerUnpatchedParam::isRelevant`
// (`origin/main:src/deluge/gui/menu_item/randomizer/randomizer_unpatched_param.h`) -- the non-global ids need the
// arpeggiator on, the three global ones do not.
DELUGE_RANDOMIZER_PARAM(spread_gate, STRING_FOR_SPREAD_GATE, STRING_FOR_ARP_SPREAD_GATE_MENU_TITLE,
                        deluge::arp::param::kSpreadGate, param_shows::kRandomizer, Hint::Bar);
DELUGE_RANDOMIZER_MIDI(spread_gate_midi, STRING_FOR_SPREAD_GATE, STRING_FOR_ARP_SPREAD_GATE_MENU_TITLE,
                       "arpeggiator/spreadGate", Hint::Bar);

// `RandomizerSoundOnlyUnpatchedParam::isRelevant`
// (`origin/main:src/deluge/gui/menu_item/randomizer/randomizer_unpatched_param.h`).
DELUGE_RANDOMIZER_PARAM(spread_octave, STRING_FOR_SPREAD_OCTAVE, STRING_FOR_ARP_SPREAD_OCTAVE_MENU_TITLE,
                        deluge::arp::param::kSpreadOctave, param_shows::kRandomizerSoundOnly, Hint::Bar);
DELUGE_RANDOMIZER_MIDI(spread_octave_midi, STRING_FOR_SPREAD_OCTAVE, STRING_FOR_ARP_SPREAD_OCTAVE_MENU_TITLE,
                       "arpeggiator/spreadOctave", Hint::Bar);

/// A global id, so with the arp off too; not on a MIDI or gate drum row, which the kit's own effects keep selected.
inline constexpr Node spread_velocity =
    with_relevant(with_short_column_label(param(sid(deluge::l10n::String::STRING_FOR_SPREAD_VELOCITY),
                                                arp_param(deluge::arp::param::kSpreadVelocity), Hint::Bar, ParamShows{},
                                                sid(deluge::l10n::String::STRING_FOR_SPREAD_VELOCITY_MENU_TITLE))),
                  relevance::not_non_audio_drum_row);
DELUGE_RANDOMIZER_MIDI(spread_velocity_midi, STRING_FOR_SPREAD_VELOCITY, STRING_FOR_SPREAD_VELOCITY_MENU_TITLE,
                       "arpeggiator/spreadVelocity", Hint::Bar);

DELUGE_RANDOMIZER_PARAM(ratchet_amount, STRING_FOR_NUMBER_OF_RATCHETS, STRING_FOR_ARP_RATCHETS_MENU_TITLE,
                        deluge::arp::param::kRatchetAmount, param_shows::kRandomizer, Hint::Bar);
DELUGE_RANDOMIZER_MIDI(ratchet_amount_midi, STRING_FOR_NUMBER_OF_RATCHETS, STRING_FOR_ARP_RATCHETS_MENU_TITLE,
                       "arpeggiator/ratchetAmount", Hint::Bar);

DELUGE_RANDOMIZER_PARAM(ratchet_probability, STRING_FOR_RATCHET_PROBABILITY,
                        STRING_FOR_ARP_RATCHET_PROBABILITY_MENU_TITLE, deluge::arp::param::kRatchetProbability,
                        param_shows::kRandomizer, Hint::Percent);
DELUGE_RANDOMIZER_MIDI(ratchet_probability_midi, STRING_FOR_RATCHET_PROBABILITY,
                       STRING_FOR_ARP_RATCHET_PROBABILITY_MENU_TITLE, "arpeggiator/ratchetProbability", Hint::Percent);

// `RandomizerNonKitSoundUnpatchedParam::isRelevant`
// (`origin/main:src/deluge/gui/menu_item/randomizer/randomizer_unpatched_param.h`).
DELUGE_RANDOMIZER_PARAM(chord_polyphony, STRING_FOR_CHORD_POLYPHONY, STRING_FOR_ARP_CHORD_POLYPHONY_MENU_TITLE,
                        deluge::arp::param::kChordPolyphony, param_shows::kRandomizerNonKit, Hint::Bar);
DELUGE_RANDOMIZER_MIDI(chord_polyphony_midi, STRING_FOR_CHORD_POLYPHONY, STRING_FOR_ARP_CHORD_POLYPHONY_MENU_TITLE,
                       "arpeggiator/chordPolyphony", Hint::Bar);

DELUGE_RANDOMIZER_PARAM(chord_probability, STRING_FOR_CHORD_PROBABILITY, STRING_FOR_ARP_CHORD_PROBABILITY_MENU_TITLE,
                        deluge::arp::param::kChordProbability, param_shows::kRandomizerNonKit, Hint::Percent);
DELUGE_RANDOMIZER_MIDI(chord_probability_midi, STRING_FOR_CHORD_PROBABILITY,
                       STRING_FOR_ARP_CHORD_PROBABILITY_MENU_TITLE, "arpeggiator/chordProbability", Hint::Percent);

DELUGE_RANDOMIZER_PARAM(note_probability, STRING_FOR_NOTE_PROBABILITY, STRING_FOR_NOTE_PROBABILITY_MENU_TITLE,
                        deluge::arp::param::kNoteProbability, ParamShows{}, Hint::Percent);
DELUGE_RANDOMIZER_MIDI(note_probability_midi, STRING_FOR_NOTE_PROBABILITY, STRING_FOR_NOTE_PROBABILITY_MENU_TITLE,
                       "arpeggiator/noteProbability", Hint::Percent);

DELUGE_RANDOMIZER_PARAM(swap_probability, STRING_FOR_SWAP_PROBABILITY, STRING_FOR_ARP_SWAP_PROBABILITY_MENU_TITLE,
                        deluge::arp::param::kSwapProbability, param_shows::kRandomizer, Hint::Percent);
DELUGE_RANDOMIZER_MIDI(swap_probability_midi, STRING_FOR_SWAP_PROBABILITY, STRING_FOR_ARP_SWAP_PROBABILITY_MENU_TITLE,
                       "arpeggiator/swapProbability", Hint::Percent);

DELUGE_RANDOMIZER_PARAM(bass_probability, STRING_FOR_BASS_PROBABILITY, STRING_FOR_ARP_BASS_PROBABILITY_MENU_TITLE,
                        deluge::arp::param::kBassProbability, param_shows::kRandomizer, Hint::Percent);
DELUGE_RANDOMIZER_MIDI(bass_probability_midi, STRING_FOR_BASS_PROBABILITY, STRING_FOR_ARP_BASS_PROBABILITY_MENU_TITLE,
                       "arpeggiator/bassProbability", Hint::Percent);

DELUGE_RANDOMIZER_PARAM(glide_probability, STRING_FOR_GLIDE_PROBABILITY, STRING_FOR_ARP_GLIDE_PROBABILITY_MENU_TITLE,
                        deluge::arp::param::kGlideProbability, param_shows::kRandomizer, Hint::Percent);
DELUGE_RANDOMIZER_MIDI(glide_probability_midi, STRING_FOR_GLIDE_PROBABILITY,
                       STRING_FOR_ARP_GLIDE_PROBABILITY_MENU_TITLE, "arpeggiator/glideProbability", Hint::Percent);

// The one setting with no MIDI/CV twin (`reverseProbabilityMenu` in `origin/main:src/deluge/gui/ui/menus.cpp`).
DELUGE_RANDOMIZER_PARAM(reverse_probability, STRING_FOR_REVERSE_PROBABILITY, STRING_FOR_REVERSE_PROBABILITY_MENU_TITLE,
                        deluge::arp::param::kReverseProbability, ParamShows{}, Hint::Percent);

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

} // namespace deluge::ui::scenes::menu::nodes
