#pragma once

/// @brief The voice page as data: `voice` and its five columns, the last of which is task 6's
///        unison page.
///
/// `inline constexpr` for the reason `sound_filters.h` explains (task-6 fix round 1, I3). From
/// `gui/ui/menus.cpp`'s `voiceMenu` literal (`:707-710`) and the globals it names (`:702-705`):
///
// clang-format off
/// | node | legacy global | legacy class | kind |
/// |---|---|---|---|
/// | `voice_priority` | `priorityMenu` (`:705`) | `voice::Priority` (`voice/priority.h:20`) | Enum |
/// | `polyphony` | `polyphonyMenu` (`:702`) | `voice::PolyphonyType` (`voice/polyphony.h:90`) | Enum (dynamic) |
/// | `voice_count` | `voice::polyphonicVoiceCountMenu` (`:703`) | `voice::VoiceCount` (`voice/polyphony.h:22`) | Int |
/// | `portamento` | `portaMenu` (`:704`) | `voice::Portamento` (`voice/portamento.h:20`), `UNPATCHED_PORTAMENTO` | Int (param) |
/// | `unison` | `unisonMenu` | `HorizontalMenu` -- **task 6's**, `sound_unison.h` | Submenu (Horizontal) |
/// | `voice` | `voiceMenu` (`:707`) | `HorizontalMenu`, `Layout::FIXED` | Submenu (Horizontal) |
// clang-format on
///
/// @note **`PolyphonyType`'s option list is dynamic**: CHOKE is offered only on a kit
///       (`voice/polyphony.h:120-123`), so the node uses `dynamic_enumeration` rather than a static
///       `names` span. `VoiceCount` reads OFF at zero and draws an infinity icon there
///       (`polyphony.h:59-61`, `:73-79`) -- neither is a `Node` field, and both are recorded rather
///       than dropped.
///
/// @note **Two legacy behaviours have no `Node` spelling.** `PolyphonyType::selectButtonPress`
///       jumps to the voice count when the mode is POLY (`polyphony.h:107-112`), and `voiceMenu` is
///       built `HorizontalMenu::Layout::FIXED` (`menus.cpp:710`), a paging mode `Node::Layout`
///       (Vertical / Horizontal) does not carry -- the same gap `sound_unison.h` records for
///       `unisonMenu`.
///
/// @note **`Portamento` draws bespoke art** (a glide line between two notes,
///       `voice/portamento.h:29-66`) with no `getRenderingStyle()` override, so legacy's declared
///       style is KNOB while what it *draws* is neither. `Hint::None` is the honest answer, on the
///       convention task 6 set (its concern 5).

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/sound_unison.h" // the fifth column
#include "scenes/menu/relevance.h"

#include <span>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @name The voice page's hooks. Bodies (and their citations) in the `.cpp`.
/// @{
int32_t get_voice_priority(const MenuContext& ctx);
void set_voice_priority(const MenuContext& ctx, int32_t value);
size_t polyphony_names(const MenuContext& ctx, std::span<std::string_view> out);
int32_t get_polyphony(const MenuContext& ctx);
void set_polyphony(const MenuContext& ctx, int32_t value);
int32_t get_voice_count(const MenuContext& ctx);
void set_voice_count(const MenuContext& ctx, int32_t value);
/// @}

/// `voice::Priority::getOptions` (`voice/priority.h:39-46`), FULL spelling.
inline constexpr uint16_t kVoicePriorityNames[] = {
    sid(deluge::l10n::String::STRING_FOR_LOW),
    sid(deluge::l10n::String::STRING_FOR_MEDIUM),
    sid(deluge::l10n::String::STRING_FOR_HIGH),
};

inline constexpr Node voice_priority = enumeration(sid(deluge::l10n::String::STRING_FOR_PRIORITY), kVoicePriorityNames,
                                                   get_voice_priority, set_voice_priority);
inline constexpr Node polyphony =
    dynamic_enumeration(sid(deluge::l10n::String::STRING_FOR_POLYPHONY), polyphony_names, get_polyphony, set_polyphony);
// `VoiceCount::getMinValue`/`getMaxValue` (`voice/polyphony.h:56-57`), rendered as a plain NUMBER
// (`:58`).
inline constexpr Node voice_count = int_item(sid(deluge::l10n::String::STRING_FOR_MAX_VOICES), get_voice_count,
                                             set_voice_count, IntRange{0, 16, 1}, Hint::None, relevance::is_polyphonic);
inline constexpr Node portamento = param(
    sid(deluge::l10n::String::STRING_FOR_PORTAMENTO),
    {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_PORTAMENTO}, Hint::None);

inline constexpr const Node* kVoiceChildren[] = {&voice_priority, &polyphony, &voice_count, &portamento, &unison};
inline constexpr Node voice = submenu(sid(deluge::l10n::String::STRING_FOR_VOICE), kVoiceChildren, Layout::Horizontal);

/// @brief This file's contribution to the sound-editor root: `voiceMenu`.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundVoiceChildren;

} // namespace deluge::scenes::menu::nodes
