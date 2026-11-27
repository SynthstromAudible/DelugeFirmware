#pragma once

/// @brief The voice page as data: `voice` and its five columns, the last of which is the
///        unison page.
///
/// `inline constexpr` for the reason `sound_filters.h` explains. From
/// `origin/main:src/deluge/gui/ui/menus.cpp`'s `voiceMenu` literal and the globals it names:
///
// clang-format off
/// | node | legacy global | legacy class | kind |
/// |---|---|---|---|
/// | `voice_priority` | `priorityMenu` | `voice::Priority` (`origin/main:src/deluge/gui/menu_item/voice/priority.h`) | Enum |
/// | `polyphony` | `polyphonyMenu` | `voice::PolyphonyType` (`origin/main:src/deluge/gui/menu_item/voice/polyphony.h`) | Enum (dynamic) |
/// | `voice_count` | `voice::polyphonicVoiceCountMenu` | `voice::VoiceCount` (`origin/main:src/deluge/gui/menu_item/voice/polyphony.h`) | Int |
/// | `portamento` | `portaMenu` | `voice::Portamento` (`origin/main:src/deluge/gui/menu_item/voice/portamento.h`), `UNPATCHED_PORTAMENTO` | Int (param) |
/// | `unison` | `unisonMenu` | `HorizontalMenu` -- `sound_unison.h` | Submenu (Horizontal) |
/// | `voice` | `voiceMenu` | `HorizontalMenu`, `Layout::FIXED` | Submenu (Horizontal) |
// clang-format on
///
/// @note **`PolyphonyType`'s option list is dynamic**: CHOKE is offered only on a kit (`PolyphonyType::getOptions` in
///       `origin/main:src/deluge/gui/menu_item/voice/polyphony.h`), so the node uses `dynamic_enumeration` rather than
///       a static `names` span.
///
/// @note **Columns.** `VoiceCount`, `PolyphonyType` and `Portamento` label their columns VCNT, MODE and PRTA
///       (`VoiceCount::getColumnLabel` in `origin/main:src/deluge/gui/menu_item/voice/polyphony.h`,
///       `PolyphonyType::getColumnLabel`, `Portamento::getColumnLabel`
///       (`origin/main:src/deluge/gui/menu_item/voice/portamento.h`)); `VoiceCount` draws infinity at zero and its
///       notification reads OFF there (`VoiceCount::renderInHorizontalMenu` and `VoiceCount::getNotificationValue` in
///       `origin/main:src/deluge/gui/menu_item/voice/polyphony.h`). `Portamento`'s glide drawing
///       (`Portamento::renderInHorizontalMenu` in `origin/main:src/deluge/gui/menu_item/voice/portamento.h`) is not
///       carried: its column draws the number.
///
/// @note `voiceMenu` is built `HorizontalMenu::Layout::FIXED` (`origin/main:src/deluge/gui/ui/menus.cpp`):
/// `with_fixed_columns`.
///
/// @note **`Portamento` draws bespoke art** (a glide line between two notes, `Portamento::renderInHorizontalMenu` in
///       `origin/main:src/deluge/gui/menu_item/voice/portamento.h`) with no `getRenderingStyle()` override, so legacy's
///       declared style is KNOB while what it *draws* is neither. `Hint::None` is the honest answer, the convention for
///       a column whose legacy style does not match what it draws.

#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/column_hooks.h"
#include "ui/scenes/menu/nodes/detail.h"
#include "ui/scenes/menu/nodes/fan_out_rules.h"
#include "ui/scenes/menu/nodes/sound_unison.h" // the fifth column
#include "ui/scenes/menu/relevance.h"

#include "instruments/classic/words.h"
#include "ui/gfx/oled/icons.h"

#include <optional>
#include <span>

namespace deluge::ui::scenes::menu {
struct MenuContext;
} // namespace deluge::ui::scenes::menu

namespace deluge::ui::scenes::menu::nodes {

/// @}

/// A sound's, or where the menu edits an audio clip, the clip's own.
inline constexpr Node voice_priority = with_affect_entire(setting_item(
    sid(deluge::l10n::String::STRING_FOR_PRIORITY), Kind::Enum,
    {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND, .path = "voicePriority", .audio_clip_path = "priority"}));
// `VoiceCount::getMinValue`/`getMaxValue` (`origin/main:src/deluge/gui/menu_item/voice/polyphony.h`), rendered as a
// plain NUMBER (`VoiceCount::getRenderingStyle`). Declared before `polyphony`, which chains to it; `kVoiceChildren`
// below still fixes the on-screen order.
//
// The node-id order happens to be unchanged here too -- but by luck, not by the children span:
// `assign_node_ids` numbers a chain target while visiting the node that chains to it, so
// `voice_count` is numbered right after `polyphony` either way, because it is already `polyphony`'s
// next sibling. `sound_unison.h`'s equivalent pair is not so lucky and says so.
/// @brief `VoiceCount::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/voice/polyphony.h`): infinity at
/// 0, else the number.
inline std::optional<deluge::ui::widgets::oled::ArtColumn> voice_count_art(const MenuContext&, int32_t value) {
	if (value != 0) {
		return std::nullopt;
	}
	return deluge::ui::widgets::oled::ArtColumn{.icon = &deluge::ui::gfx::oled::icons::infinity, .dy = 1};
}

inline constexpr Node voice_count = with_affect_entire(with_column_name(
    with_value_text(
        with_column_art(with_value_style(setting_item(sid(deluge::l10n::String::STRING_FOR_MAX_VOICES), Kind::Int,
                                                      {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,
                                                       .path = "maxVoices",
                                                       .fan_out_when = fan_out::kPolyphonic}),
                                         deluge::ui::widgets::oled::ValueStyle{.off_at_zero = true}),
                        voice_count_art),
        off_at_zero_text),
    sid(deluge::l10n::String::STRING_FOR_MAX_VOICES_SHORT)));

/// @brief `PolyphonyType::selectButtonPress()` (`origin/main:src/deluge/gui/menu_item/voice/polyphony.h`): a select
///        press opens the voice count when the mode is POLY, and otherwise falls back to
///        `Selection::selectButtonPress()`: from the VOICE page that stays on the page
///        (`Node::chain_falls_back_to_selection`), and on the item's own screen it goes up.
inline constexpr const Node* kPolyphonyChain[] = {&voice_count};
inline constexpr deluge::ui::scenes::bind::Target kPolyphonic{.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,
                                                              .path = "polyphonic"};
inline const Node* polyphony_chain(const MenuContext& ctx, const MenuState&) {
	return bindings::stored(kPolyphonic, ctx) == static_cast<int32_t>(instruments::classic::PolyphonyMode::POLY)
	           ? &voice_count
	           : nullptr;
}

inline constexpr Node polyphony = with_affect_entire(
    with_column_name(with_chain_falls_back_to_selection(with_select_chain(
                         setting_item(sid(deluge::l10n::String::STRING_FOR_POLYPHONY), Kind::Enum, kPolyphonic),
                         polyphony_chain, kPolyphonyChain)),
                     sid(deluge::l10n::String::STRING_FOR_POLYPHONY_SHORT)));
inline constexpr Node portamento =
    with_column_draw(with_column_name(param(sid(deluge::l10n::String::STRING_FOR_PORTAMENTO),
                                            sound_param(deluge::instruments::classic::param::kPortamento), Hint::None),
                                      sid(deluge::l10n::String::STRING_FOR_PORTAMENTO_SHORT)),
                     glide_draw);

inline constexpr const Node* kVoiceChildren[] = {&voice_priority, &polyphony, &voice_count, &portamento, &unison};
inline constexpr Node voice =
    with_fixed_columns(submenu(sid(deluge::l10n::String::STRING_FOR_VOICE), kVoiceChildren, Layout::Horizontal));

/// @brief This file's contribution to the sound-editor root: `voiceMenu`.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundVoiceChildren;

} // namespace deluge::ui::scenes::menu::nodes
