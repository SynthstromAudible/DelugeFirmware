#pragma once

/// @brief The DX7's columns: what OSC1's page (`source0`) shows in DX7 mode, after its oscillator columns, which hide
///        there.
///
/// The DX7 is a sound's engine, so a DX7-mode sound has no oscillators: OSC1's place in the sound menu is the DX7's,
/// named DX7 (`source0_name`), with the columns `main` showed for a DX7 on oscillator 1
/// (`origin/main:src/deluge/gui/ui/menus.cpp`'s `source0Menu` and `oscMixerMenu`):
///
// clang-format off
/// | node | what it edits | `main`'s column for a DX7 |
/// |---|---|---|
/// | `dx7_engine` | the synth mode; SELECT enters "DX7 source" (`dx_menu`) | oscillator 1's type, DX7 |
/// | `dx7_transpose` | `dx7/transpose` and `dx7/cents`; MIDI learn takes the DX7's pitch adjust | oscillator 1's transpose |
/// | `dx7_amp_mod` | the DX7's amplitude modulation (`dx7AmpMod`) | oscillator 1's pulse width |
/// | `dx7_phase` | `dx7/retrigPhase` | oscillator 1's retrigger phase |
/// | `dx7_level` | the DX7's level (`dx7Level`) | the mixer's oscillator 1 level |
// clang-format on
///
/// @note The DX7's level, amplitude modulation and pitch adjust are its own params, which `main`'s DX7 oscillator took
///       from oscillator 1's volume, pulse width and pitch adjust. It starts each operator's
///       phase itself, so the retrigger phase plays nothing (a retriggered DX7 renders as a free-running one,
///       `dx7_engine_spec`); `main` showed the column for a DX7 oscillator all the same, and so does this page. Each
///       column shows in DX7 mode alone, and appends to OSC1's children, so no column of `main`'s moves.

#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/column_hooks.h"
#include "ui/scenes/menu/nodes/detail.h"
#include "ui/scenes/menu/nodes/dx.h"
#include "ui/scenes/menu/nodes/fan_out_rules.h"
#include "ui/scenes/menu/nodes/param_shows.h"
#include "ui/scenes/menu/nodes/patch_cables.h" // with_patch_chain -- every PATCHED param chains
#include "ui/scenes/menu/relevance.h"

namespace deluge::ui::scenes::menu::nodes {

/// The mode, drawn DX7 in its column, as oscillator 1's type was on `main`; SELECT enters "DX7 source".
inline constexpr Node dx7_engine = with_select_chain(
    with_wrap_in_horizontal(with_column_art(
        without_column_label(with_relevant(setting_item(sid(deluge::l10n::String::STRING_FOR_SYNTH_MODE), Kind::Enum,
                                                        {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,
                                                         .path = "mode",
                                                         .fan_out_when = fan_out::kRingable}),
                                           relevance::is_dx7)),
        dx7_engine_art)),
    &mode_chain, kModeChain);

inline constexpr Node dx7_transpose = with_patch_chain(
    with_learn_param(with_decimal_places(setting_item(sid(deluge::l10n::String::STRING_FOR_TRANSPOSE), Kind::Decimal,
                                                      {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,
                                                       .path = "dx7/transpose",
                                                       .whole_kit = false}),
                                         2),
                     sound_param(deluge::instruments::classic::param::kDx7Pitch)));

inline constexpr Node dx7_amp_mod =
    with_column_draw(with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_PULSE_WIDTH),
                                            sound_param(deluge::instruments::classic::param::kDx7AmpMod), Hint::None,
                                            param_shows::kOnDx7, 0, Scaling::HalfPrecision)),
                     pulse_width_draw);

// The retrigger phase's screen, as `retrigger_phase_screen` (`sound_osc.h`) builds the oscillators'.
inline constexpr Node dx7_phase = with_default_edit_pos(
    with_decimal_restart_from_negative(with_decimal_word(
        with_horizontal_step(
            with_column_name(
                with_value_text(
                    with_hint(setting_item(sid(deluge::l10n::String::STRING_FOR_RETRIGGER_PHASE), Kind::Decimal,
                                           {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,
                                            .path = "dx7/retrigPhase",
                                            .whole_kit = false}),
                              Hint::Slider),
                    retrigger_phase_text),
                sid(deluge::l10n::String::STRING_FOR_RETRIGGER_PHASE_SHORT)),
            HorizontalStep{.coarse = 10, .fine = 1, .restart_from_negative = true}),
        retrigger_phase_text)),
    1);

inline constexpr Node dx7_level = with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_VOLUME_LEVEL),
                                                         sound_param(deluge::instruments::classic::param::kDx7Level),
                                                         Hint::Bar, param_shows::kOnDx7));

} // namespace deluge::ui::scenes::menu::nodes
