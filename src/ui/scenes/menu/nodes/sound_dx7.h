#pragma once

/// @brief The DX7 page as data: `sound_dx7`, the sound's own page in DX7 mode, in the oscillators' place.
///
/// The DX7 is a sound's engine, not an oscillator's type, so the controls `main` showed on oscillator 1's pages for a
/// DX7 (`origin/main:src/deluge/gui/ui/menus.cpp`'s `source0Menu` and `oscMixerMenu`) are this page's, and the
/// oscillators' pages hide in DX7 mode:
///
// clang-format off
/// | node | what it edits | `main`'s place for a DX7 |
/// |---|---|---|
/// | `dx_menu` | the DX7 pages (`nodes/dx.h`) | SELECT on oscillator 1's type |
/// | `dx7_transpose` | `dx7/transpose` and `dx7/cents` | oscillator 1's transpose |
/// | `dx7_phase` | `dx7/retrigPhase` | oscillator 1's retrigger phase |
/// | `dx7_amp_mod` | oscillator A's pulse width, the DX7's amplitude modulation | oscillator 1's pulse width |
/// | `dx7_level` | oscillator A's volume, the DX7's level | the mixer's oscillator 1 level |
// clang-format on
///
/// @note The DX7 reads oscillator A's volume and pulse width until it has params of its own.

#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/detail.h"
#include "ui/scenes/menu/nodes/dx.h"
#include "ui/scenes/menu/nodes/patch_cables.h" // with_patch_chain -- every PATCHED param chains
#include "ui/scenes/menu/nodes/sound_osc.h"    // retrigger_phase_screen, the phase's screen
#include "ui/scenes/menu/relevance.h"

namespace deluge::ui::scenes::menu::nodes {

inline constexpr Node dx7_transpose = with_patch_chain(
    with_learn_param(with_decimal_places(setting_item(sid(deluge::l10n::String::STRING_FOR_TRANSPOSE), Kind::Decimal,
                                                      {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,
                                                       .path = "dx7/transpose",
                                                       .whole_kit = false}),
                                         2),
                     {deluge::plugin::ParamKind::PATCHED,
                      deluge::instruments::classic::runtime_index(deluge::instruments::classic::param::kOscAPitch)}));

inline constexpr Node dx7_phase = retrigger_phase_screen(
    with_horizontal_step(with_hint(setting_item(sid(deluge::l10n::String::STRING_FOR_RETRIGGER_PHASE), Kind::Decimal,
                                                {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,
                                                 .path = "dx7/retrigPhase",
                                                 .whole_kit = false}),
                                   Hint::Slider),
                         HorizontalStep{.coarse = 10, .fine = 1, .restart_from_negative = true}));

inline constexpr Node dx7_amp_mod = with_patch_chain(
    param(sid(deluge::l10n::String::STRING_FOR_PULSE_WIDTH),
          {deluge::plugin::ParamKind::PATCHED,
           deluge::instruments::classic::runtime_index(deluge::instruments::classic::param::kOscAPhaseWidth)},
          Hint::None, nullptr, 0, Scaling::HalfPrecision));

inline constexpr Node dx7_level = with_patch_chain(
    param(sid(deluge::l10n::String::STRING_FOR_VOLUME_LEVEL),
          {deluge::plugin::ParamKind::PATCHED,
           deluge::instruments::classic::runtime_index(deluge::instruments::classic::param::kOscAVolume)},
          Hint::Bar));

inline constexpr const Node* kSoundDx7Children[] = {&dx_menu, &dx7_transpose, &dx7_phase, &dx7_amp_mod, &dx7_level};
/// @brief The sound's DX7 page, shown in DX7 mode.
inline constexpr Node sound_dx7 =
    submenu(sid(deluge::l10n::String::STRING_FOR_DX7), kSoundDx7Children, Layout::Vertical, relevance::is_dx7);

} // namespace deluge::ui::scenes::menu::nodes
