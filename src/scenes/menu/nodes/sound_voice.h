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
/// | `voice_priority` | `priorityMenu` (`:705`) | `voice::Priority` (`voice/priority.h:29`) | Enum |
/// | `polyphony` | `polyphonyMenu` (`:702`) | `voice::PolyphonyType` (`voice/polyphony.h:99`) | Enum (dynamic) |
/// | `voice_count` | `voice::polyphonicVoiceCountMenu` (`:703`) | `voice::VoiceCount` (`voice/polyphony.h:31`) | Int |
/// | `portamento` | `portaMenu` (`:704`) | `voice::Portamento` (`voice/portamento.h:24`), `UNPATCHED_PORTAMENTO` | Int (param) |
/// | `unison` | `unisonMenu` | `HorizontalMenu` -- **task 6's**, `sound_unison.h` | Submenu (Horizontal) |
/// | `voice` | `voiceMenu` (`:707`) | `HorizontalMenu`, `Layout::FIXED` | Submenu (Horizontal) |
// clang-format on
///
/// @note **`PolyphonyType`'s option list is dynamic**: CHOKE is offered only on a kit
///       (`voice/polyphony.h:120-123`), so the node uses `dynamic_enumeration` rather than a static
///       `names` span.
///
/// @note **Columns.** `VoiceCount`, `PolyphonyType` and `Portamento` label their columns VCNT, MODE
///       and PRTA (`polyphony.h:85-87`, `:156-158`, `portamento.h:30-32`); `VoiceCount` draws infinity
///       at zero and its notification reads OFF there (`polyphony.h:89-104`). `Portamento`'s glide
///       drawing (`portamento.h:34-66`) is not carried: its column draws the number.
///
/// @note `voiceMenu` is built `HorizontalMenu::Layout::FIXED` (`menus.cpp:710`): `with_fixed_columns`.
///
/// @note **`Portamento` draws bespoke art** (a glide line between two notes,
///       `voice/portamento.h:29-66`) with no `getRenderingStyle()` override, so legacy's declared
///       style is KNOB while what it *draws* is neither. `Hint::None` is the honest answer, on the
///       convention task 6 set (its concern 5).

#include "scenes/menu/bindings.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/column_hooks.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/fan_out_rules.h"
#include "scenes/menu/nodes/sound_unison.h" // the fifth column
#include "scenes/menu/relevance.h"

#include "definitions_cxx.hpp" // PolyphonyMode -- `polyphony_chain`'s POLY test
#include "gfx/oled/icons.h"

#include <optional>
#include <span>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @}

/// A sound's, or where the menu edits an audio clip, the clip's own.
inline constexpr Node voice_priority = setting_item(
    sid(deluge::l10n::String::STRING_FOR_PRIORITY), Kind::Enum,
    {.kind = deluge::scenes::bind::Target::Kind::SOUND, .path = "voicePriority", .audio_clip_path = "priority"});
// `VoiceCount::getMinValue`/`getMaxValue` (`voice/polyphony.h:56-57`), rendered as a plain NUMBER
// (`:58`). Declared before `polyphony`, which chains to it; `kVoiceChildren` below still fixes the
// on-screen order.
//
// The node-id order happens to be unchanged here too -- but by luck, not by the children span:
// `assign_node_ids` numbers a chain target while visiting the node that chains to it, so
// `voice_count` is numbered right after `polyphony` either way, because it is already `polyphony`'s
// next sibling. `sound_unison.h`'s equivalent pair is not so lucky and says so.
/// @brief `VoiceCount::renderInHorizontalMenu` (`voice/polyphony.h:98-104`): infinity at 0, else the number.
inline std::optional<deluge::ui::oled::ArtColumn> voice_count_art(const MenuContext&, int32_t value) {
	if (value != 0) {
		return std::nullopt;
	}
	return deluge::ui::oled::ArtColumn{.icon = &deluge::gfx::oled::icons::infinity, .dy = 1};
}

inline constexpr Node voice_count = with_column_name(
    with_value_text(
        with_column_art(with_value_style(setting_item(sid(deluge::l10n::String::STRING_FOR_MAX_VOICES), Kind::Int,
                                                      {.kind = deluge::scenes::bind::Target::Kind::SOUND,
                                                       .path = "maxVoices",
                                                       .fan_out_when = fan_out::kPolyphonic}),
                                         deluge::ui::oled::ValueStyle{.off_at_zero = true}),
                        voice_count_art),
        off_at_zero_text),
    sid(deluge::l10n::String::STRING_FOR_MAX_VOICES_SHORT));

/// @brief `PolyphonyType::selectButtonPress()` (`voice/polyphony.h:139-143`): a select press opens
///        the voice count when the mode is POLY, and otherwise falls through to
///        `Selection::selectButtonPress()`, which for an already-open menu is the base class's
///        "ascend" (`selection.h:54-64`).
inline constexpr const Node* kPolyphonyChain[] = {&voice_count};
inline constexpr deluge::scenes::bind::Target kPolyphonic{.kind = deluge::scenes::bind::Target::Kind::SOUND,
                                                          .path = "polyphonic"};
inline const Node* polyphony_chain(const MenuContext& ctx, const MenuState&) {
	return bindings::stored(kPolyphonic, ctx) == static_cast<int32_t>(PolyphonyMode::POLY) ? &voice_count : nullptr;
}

inline constexpr Node polyphony = with_column_name(
    with_select_chain(setting_item(sid(deluge::l10n::String::STRING_FOR_POLYPHONY), Kind::Enum, kPolyphonic),
                      polyphony_chain, kPolyphonyChain),
    sid(deluge::l10n::String::STRING_FOR_POLYPHONY_SHORT));
inline constexpr Node portamento = with_column_name(
    param(sid(deluge::l10n::String::STRING_FOR_PORTAMENTO),
          {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_PORTAMENTO},
          Hint::None),
    sid(deluge::l10n::String::STRING_FOR_PORTAMENTO_SHORT));

inline constexpr const Node* kVoiceChildren[] = {&voice_priority, &polyphony, &voice_count, &portamento, &unison};
inline constexpr Node voice =
    with_fixed_columns(submenu(sid(deluge::l10n::String::STRING_FOR_VOICE), kVoiceChildren, Layout::Horizontal));

/// @brief This file's contribution to the sound-editor root: `voiceMenu`.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundVoiceChildren;

} // namespace deluge::scenes::menu::nodes
