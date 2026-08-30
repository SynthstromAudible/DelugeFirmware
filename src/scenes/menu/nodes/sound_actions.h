#pragma once

/// @brief The sound-editor root's actions page as data, plus the two root children that are single
///        leaves rather than subtrees (the play direction and the output-MIDI page).
///
/// `inline constexpr` for the reason `sound_filters.h` explains (task-6 fix round 1, I3). From
/// `gui/ui/menus.cpp`:
///
// clang-format off
/// | node | legacy global | legacy class | kind |
/// |---|---|---|---|
/// | `edit_track_name` | `audioOutputNameEditMenu` (`:1127`) | `EditName` (`gui/menu_item/edit_name.h:25`) | Custom (`EditNameItem`) |
/// | `edit_clip_name` | `audioClipNameEditMenu` (`:1129`) | `EditName` | Custom (`EditNameItem`) |
/// | `edit_name` | `editNameMenu` (`:1134`) | `Submenu` | Submenu (Vertical) |
/// | `sound_actions` | `soundEditorRootActionsMenu` (`:1764`) | `Submenu` | Submenu (Vertical) |
/// | `sequence_direction` | `sequenceDirectionMenu` (`:935`) | `sequence::Direction` (`sequence/direction.h:22`) | Enum (dynamic) |
/// | `output_midi_channel` | `outputMidiChannelMenu` (`:901`) | `midi::sound::OutputMidiChannel` (`midi/sound/channel.h:21`) | Int |
/// | `output_midi_note` | `outputMidiNoteForDrumMenu` (`:902`) | `midi::sound::OutputMidiNoteForDrum` (`midi/sound/note_for_drum.h:21`) | Int |
/// | `output_midi` | `outputMidiSubmenu` (`:903`) | `Submenu` | Submenu (Vertical) |
// clang-format on
///
/// `soundEditorRootActionsMenu`'s other two children are `sample0RecorderMenu` /
/// `sample1RecorderMenu`, already declared by task 6 (`sound_osc.h`'s `sample0_recorder` /
/// `sample1_recorder`) as the recorder page's columns. They are the *same* legacy objects, so this
/// file names the same nodes rather than declaring second copies -- which is also what keeps
/// `validate_tree`'s `TooManyCustom` count honest (it counts custom *nodes*, so a second copy of a
/// node would be a second count).
///
/// @note **Both `EditName` items are real.** `EditName::beginSession` opens `RenameUI`
///       (`gui/menu_item/edit_name.cpp`); spec E §4 lists "edit-name" among the bespoke items, and
///       nothing about pushing another UI fits any `Kind`. They are `nodes::EditNameItem`s
///       (`edit_name.h`), which open `rename::RenameScene` on the matching `rename::TargetId` --
///       `edit_track_name` on `Output` (legacy's `Target::AUDIO_OUTPUT` -> `renameOutputUI`) and
///       `edit_clip_name` on `Clip` (`Target::CLIP` -> `renameClipUI`).
///
/// @note **`sequence::Direction` is transcribed as far as this layer reaches.** Legacy resolves an
///       *individual note row* first (`sequence/direction.h:29-52`): on a kit row it asks the clip
///       for the selected drum's note row, which is model work this context can do; off a kit it
///       consults `soundEditor.selectedNoteRow` and `instrumentClipView.lastAuditionedYDisplay`,
///       both UI state `src/scenes` may not read, and creates a note row if none exists. The hooks
///       below do the kit arm and legacy's own no-note-row fallback (the clip's
///       `sequenceDirectionMode`, `:56-61`), and the audition arm belongs to spec E's
///       `NoteClipScene`. The option list is dynamic for the same reason: the fourth option, NONE,
///       is offered only when a note row was resolved (`:83-86`).
///
/// @note **`sequence::Direction::checkPermissionToBeginSession`** (`:93-104`) refuses on a kit row
///       with no selected drum and otherwise latches `soundEditor.selectedNoteRow`. The refusal is
///       already what `context_for` resolves to `MenuContext::resolved == false`
///       (`sound_editor.cpp:1677-1684`); the latch is the UI state above.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/edit_name.h" // kEditOutputName, kEditClipName
#include "scenes/menu/nodes/sound_osc.h" // sample0_recorder, sample1_recorder
#include "scenes/menu/relevance.h"

#include <span>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @name Hooks. Bodies (and their citations) in the `.cpp`.
/// @{
size_t sequence_direction_names(const MenuContext& ctx, std::span<std::string_view> out);
int32_t get_sequence_direction(const MenuContext& ctx);
void set_sequence_direction(const MenuContext& ctx, int32_t value);
int32_t get_output_midi_channel(const MenuContext& ctx);
void set_output_midi_channel(const MenuContext& ctx, int32_t value);
int32_t get_output_midi_note(const MenuContext& ctx);
void set_output_midi_note(const MenuContext& ctx, int32_t value);
/// @}

inline constexpr Node edit_track_name = custom(sid(deluge::l10n::String::STRING_FOR_EDIT_TRACK_NAME), &kEditOutputName);
inline constexpr Node edit_clip_name = custom(sid(deluge::l10n::String::STRING_FOR_EDIT_CLIP_NAME), &kEditClipName);

inline constexpr const Node* kEditNameChildren[] = {&edit_track_name, &edit_clip_name};
inline constexpr Node edit_name = submenu(sid(deluge::l10n::String::STRING_FOR_EDIT_NAME), kEditNameChildren);

inline constexpr const Node* kActionsChildren[] = {&edit_name, &sample0_recorder, &sample1_recorder};
inline constexpr Node sound_actions = submenu(sid(deluge::l10n::String::STRING_FOR_ACTIONS), kActionsChildren);

inline constexpr Node sequence_direction =
    dynamic_enumeration(sid(deluge::l10n::String::STRING_FOR_PLAY_DIRECTION), sequence_direction_names,
                        get_sequence_direction, set_sequence_direction);

// `OutputMidiChannel::getMinValue`/`getMaxValue` (`midi/sound/channel.h:22-23`) and
// `OutputMidiNoteForDrum`'s (`note_for_drum.h:22-23`). Both render a plain number
// (`Integer::drawInteger`), so `Hint::None`.
inline constexpr Node output_midi_channel =
    int_item(sid(deluge::l10n::String::STRING_FOR_CHANNEL), get_output_midi_channel, set_output_midi_channel,
             IntRange{0, 16, 1}, Hint::None);
inline constexpr Node output_midi_note =
    int_item(sid(deluge::l10n::String::STRING_FOR_NOTE), get_output_midi_note, set_output_midi_note,
             IntRange{0, kMaxMIDIValue, 1}, Hint::None, relevance::output_midi_note);

inline constexpr const Node* kOutputMidiChildren[] = {&output_midi_channel, &output_midi_note};
inline constexpr Node output_midi = submenu(sid(deluge::l10n::String::STRING_FOR_MIDI), kOutputMidiChildren);

/// @brief This file's contributions to the sound-editor root, in the order the root lists them.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundActionsChildren;

} // namespace deluge::scenes::menu::nodes
