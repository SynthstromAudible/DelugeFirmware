#pragma once

/// @brief The FX subtree as data: `sound_fx` and its seven pages (EQ, delay, reverb, stutter,
///        mod FX, distortion) plus the noise level, which `sound_osc.h` declares.
///
/// `inline constexpr` for the reason `sound_filters.h` explains. From
/// `origin/main:src/deluge/gui/ui/menus.cpp`'s `soundFXMenu` literal and the page literals it names.
///
// clang-format off
/// | node | legacy global | legacy class | kind |
/// |---|---|---|---|
/// | `eq_bass` | `bassMenu` | `eq::EqUnpatchedParam`, `UNPATCHED_BASS` | Int (param) |
/// | `eq_treble` | `trebleMenu` | same, `UNPATCHED_TREBLE` | Int (param) |
/// | `eq_bass_freq` | `bassFreqMenu` | same, `UNPATCHED_BASS_FREQ` | Int (param) |
/// | `eq_treble_freq` | `trebleFreqMenu` | same, `UNPATCHED_TREBLE_FREQ` | Int (param) |
/// | `eq` | `eqMenu` | `eq::EqMenu` (`origin/main:src/deluge/gui/menu_item/eq/eq_menu.h`), a `HorizontalMenu` | Submenu (Horizontal) |
/// | `delay_amount` | `delayFeedbackMenu` | `delay::Amount` (`origin/main:src/deluge/gui/menu_item/delay/amount.h`), `GLOBAL_DELAY_FEEDBACK` | Int (param) |
/// | `delay_ping_pong` | `delayPingPongMenu` | `delay::PingPong` | Enum (toggle) |
/// | `delay_sync` | `delaySyncMenu` | `delay::Sync` | SyncLevel |
/// | `delay_rate` | `delayRateMenu` | `patched_param::Integer`, `GLOBAL_DELAY_RATE` | Int (param) |
/// | `delay_type` | `delayAnalogMenu` | `delay::Analog` (`origin/main:src/deluge/gui/menu_item/delay/analog.h`) | Enum |
/// | `delay` | `delayMenu` | `HorizontalMenu` | Submenu (Horizontal) |
/// | `reverb_amount` | `reverbAmountMenu` | `reverb::Amount` (`origin/main:src/deluge/gui/menu_item/reverb/amount.h`), `GLOBAL_REVERB_AMOUNT` | Int (param) |
/// | `reverb_room_size` | `reverbRoomSizeMenu` | `reverb::RoomSize` | Int |
/// | `reverb_damping` | `reverbDampingMenu` | `reverb::Damping` | Int |
/// | `reverb_width` | `reverbWidthMenu` | `reverb::Width` | Int |
/// | `reverb_model` | `reverbModelMenu` | `reverb::Model` (`origin/main:src/deluge/gui/menu_item/reverb/model.h`) | Enum |
/// | `reverb_pan` | `reverbPanMenu` | `reverb::Pan` (`origin/main:src/deluge/gui/menu_item/reverb/pan.h`) | Int (pan scaling) |
/// | `reverb_hpf` | `reverbHPFMenu` | `reverb::HPF` (`origin/main:src/deluge/gui/menu_item/reverb/hpf.h`) | Int |
/// | `reverb_lpf` | `reverbLPFMenu` | `reverb::LPF` | Int |
/// | `reverb_sidechain_volume` | `reverbSidechainVolumeMenu` | `reverb::sidechain::Volume` | Int |
/// | `reverb_sidechain_shape` | `reverbSidechainShapeMenu` | `reverb::sidechain::Shape` | Int |
/// | `reverb_sidechain_attack` | `reverbSidechainAttackMenu` | `sidechain::Attack`, reverb spelling | Int |
/// | `reverb_sidechain_release` | `reverbSidechainReleaseMenu` | `sidechain::Release`, reverb spelling | Int |
/// | `reverb_sidechain_sync` | `reverbSidechainSyncMenu` | `sidechain::Sync`, reverb spelling | SyncLevel, 10 options |
/// | `reverb_sidechain` | `reverbSidechainMenu` | `HorizontalMenu`, `FIXED` | Submenu (Horizontal) |
/// | `reverb` | `reverbMenu` | `HorizontalMenu` | Submenu (Horizontal) |
/// | `stutter_rate` | `stutterRateMenu` | `stutter::Rate` (`origin/main:src/deluge/gui/menu_item/stutter/rate.h`), `UNPATCHED_STUTTER_RATE` | Int (param) |
/// | `stutter_direction` | `stutterDirectionMenu` | `stutter::StutterDirection` | Enum (dynamic) |
/// | `stutter_quantize` | `stutterQuantizedMenu` | `stutter::QuantizedStutter` | Toggle |
/// | `stutter` | `stutterMenu` | `HorizontalMenu`, `FIXED` | Submenu (Horizontal) |
/// | `mod_fx_type` | `modFXTypeMenu` | `mod_fx::Type` | Enum (dynamic), 4 slots |
/// | `mod_fx_depth` | `modFXDepthMenu` | `mod_fx::Depth_Patched`, `GLOBAL_MOD_FX_DEPTH` | Int (param) |
/// | `mod_fx_rate` | `modFXRateMenu` | `mod_fx::Rate` (`origin/main:src/deluge/gui/menu_item/mod_fx/rate.h`), `GLOBAL_MOD_FX_RATE` | Int (param) |
/// | `mod_fx_feedback` | `modFXFeedbackMenu` | `mod_fx::Feedback`, `UNPATCHED_MOD_FX_FEEDBACK` | Int (param) |
/// | `mod_fx_offset` | `modFXOffsetMenu` | `mod_fx::Offset`, `UNPATCHED_MOD_FX_OFFSET` | Int (param) |
/// | `mod_fx` | `modFXMenu` | `submenu::ModFxHorizontalMenu` | Submenu (Horizontal) |
/// | `saturation` | `clippingMenu` | `fx::Clipping` | Int |
/// | `bitcrush` | `bitcrushMenu` | `UnpatchedParam`, `UNPATCHED_BITCRUSHING`, BAR | Int (param) |
/// | `decimation` | `srrMenu` | `UnpatchedParam`, `UNPATCHED_SAMPLE_RATE_REDUCTION`, BAR | Int (param) |
/// | `wavefold` | `foldMenu` | `patched_param::Integer`, `LOCAL_FOLD`, BAR | Int (param) |
/// | `distortion` | `soundDistortionMenu` | `HorizontalMenu` | Submenu (Horizontal) |
/// | `noise` | `noiseMenu` | `patched_param::IntegerNonFM` -- declared in `sound_osc.h` | Int (param) |
/// | `sound_fx` | `soundFXMenu` | `Submenu` | Submenu (Vertical) |
// clang-format on
///
/// @note **The reverb is engine-wide, not per-clip.** Every `reverb::*` item but the amount reads and writes
///       `AudioEngine::reverb` / `AudioEngine::reverbPan` / `AudioEngine::reverbSidechain*` directly
///       (`RoomSize::readCurrentValue` in `origin/main:src/deluge/gui/menu_item/reverb/room_size.h`,
///       `Volume::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/reverb/sidechain/volume.h`)), so their hooks
///       ignore the `MenuContext` entirely. That is legacy's design, not a simplification: one reverb serves the whole
///       song.
///
/// @note **Five dynamic names, two dynamic option lists.** `mod_fx::Depth_Patched`, `Feedback` and `Offset` take their
///       `getName()` from `l10n::mod_fx_param_name(type, ...)` (`Depth_Patched::getName` in
///       `origin/main:src/deluge/gui/menu_item/mod_fx/depth_patched.h`), and `reverb::RoomSize` / `Width` read "TIME" /
///       "DIFFUSION" on the two newer models (`RoomSize::getName` in
///       `origin/main:src/deluge/gui/menu_item/reverb/room_size.h`): `column_hooks.h`'s hooks answer the column label
///       (`value_text`) and the name and title (`name_text`); `Node::name` keeps the base string, which `tables_2_spec`
///       exempts by node. `mod_fx::Type`'s and `StutterDirection`'s option *lists* are genuinely dynamic and use
///       `dynamic_enumeration`.
///
/// @note `stutterMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`) and `reverbSidechainMenu` are
///       `HorizontalMenu::Layout::FIXED`: `with_fixed_columns`.
///
/// @note **Columns.** The items here that override `getColumnLabel()` with a fixed string (AMNT, Modl, Ducking, SHPE,
///       ATK, REL, Bfrq, Tfrq) are `with_column_name`; the ping-pong switch, the reverb sidechain's AUTO and the
///       dynamic labels are `column_hooks.h`'s hooks; `mod_fx::Type` is `without_column_label`/`without_notification`
///       (`Type::showColumnLabel` and `Type::showNotification` in
///       `origin/main:src/deluge/gui/menu_item/mod_fx/type.h`).

#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/column_hooks.h"
#include "ui/scenes/menu/nodes/detail.h"
#include "ui/scenes/menu/nodes/param_shows.h"
#include "ui/scenes/menu/nodes/patch_cables.h"    // with_patch_chain -- every PATCHED param chains
#include "ui/scenes/menu/nodes/sound_lfo.h"       // kSyncLevelRange
#include "ui/scenes/menu/nodes/sound_osc.h"       // the noise level
#include "ui/scenes/menu/nodes/sound_sidechain.h" // kSidechainSyncRange, kSidechainRateRange
#include "ui/scenes/menu/relevance.h"

#include "plugin/types/display_mapping.h"

#include <span>

namespace deluge::ui::scenes::menu {
struct MenuContext;
} // namespace deluge::ui::scenes::menu

namespace deluge::ui::scenes::menu::nodes {

int32_t get_reverb_sidechain_volume(const MenuContext& ctx);
void set_reverb_sidechain_volume(const MenuContext& ctx, int32_t value);
int32_t get_reverb_sidechain_sync(const MenuContext& ctx);
void set_reverb_sidechain_sync(const MenuContext& ctx, int32_t value);

size_t stutter_direction_names(const MenuContext& ctx, std::span<std::string_view> out);
int32_t get_stutter_direction(const MenuContext& ctx);
void set_stutter_direction(const MenuContext& ctx, int32_t value);
int32_t get_stutter_quantize(const MenuContext& ctx);
void set_stutter_quantize(const MenuContext& ctx, int32_t value);
/// `stutter::Rate::isStutterQuantized` (`origin/main:src/deluge/gui/menu_item/stutter/rate.cpp`).
bool stutter_rate_quantized(const MenuContext& ctx);
/// `stutter::Rate`'s quantized label (`origin/main:src/deluge/gui/menu_item/stutter/rate.cpp`): its column, banner and
/// full-screen value.
ValueText stutter_rate_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `stutter::Rate::selectEncoderAction` (`origin/main:src/deluge/gui/menu_item/stutter/rate.cpp`): quantized, the next
/// option value along.
std::optional<int32_t> stutter_rate_turn(const MenuContext& ctx, int32_t value, int32_t offset);
/// `stutter::StutterDirection::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/stutter/direction.h`):
/// "song", or the arrow with ping-pong dots.
std::optional<deluge::ui::widgets::oled::ArtColumn> stutter_direction_art(const MenuContext& ctx, int32_t value);
/// `stutter::StutterDirection::getNotificationValue` (`origin/main:src/deluge/gui/menu_item/stutter/direction.h`): the
/// short option.
ValueText stutter_direction_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);

/// @}

// --- EQ ----------------------------------------------------------------------------------------
//
// `EqUnpatchedParam`'s three-argument constructor takes `(name, columnLabel, p)` and forwards `UnpatchedParam(name, p)`
// (`origin/main:src/deluge/gui/menu_item/eq/eq_unpatched_param.h`) -- so the second string is the *column label*, not a
// title, and each node's title is its name. No `getRenderingStyle` override, so the `IntegerContinuous` default, KNOB.

inline constexpr Node eq_bass =
    param(sid(deluge::l10n::String::STRING_FOR_BASS), sound_param(deluge::instruments::classic::param::kBass));
inline constexpr Node eq_treble =
    param(sid(deluge::l10n::String::STRING_FOR_TREBLE), sound_param(deluge::instruments::classic::param::kTreble));
inline constexpr Node eq_bass_freq =
    with_column_name(param(sid(deluge::l10n::String::STRING_FOR_BASS_FREQUENCY),
                           sound_param(deluge::instruments::classic::param::kBassFreq)),
                     sid(deluge::l10n::String::STRING_FOR_BASS_FREQUENCY_SHORT));
inline constexpr Node eq_treble_freq =
    with_column_name(param(sid(deluge::l10n::String::STRING_FOR_TREBLE_FREQUENCY),
                           sound_param(deluge::instruments::classic::param::kTrebleFreq)),
                     sid(deluge::l10n::String::STRING_FOR_TREBLE_FREQUENCY_SHORT));

/// `EqMenu::ensureCorrectItemsOrderAndGetValues` (`origin/main:src/deluge/gui/menu_item/eq/eq_menu.h`) orders the four
/// as the graph's handles run, left to right: bass, bass frequency, treble frequency, treble.
inline constexpr const Node* kEqChildren[] = {&eq_bass, &eq_bass_freq, &eq_treble_freq, &eq_treble};
void eq_page_draw(deluge::ui::widgets::oled::OledFrame& panel, const MenuContext& ctx, int32_t focused);
inline constexpr Node eq =
    with_page_draw(submenu(sid(deluge::l10n::String::STRING_FOR_EQ), kEqChildren, Layout::Horizontal), eq_page_draw);

// --- Delay -------------------------------------------------------------------------------------

inline constexpr Node delay_amount = with_column_draw(
    with_column_name(with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_AMOUNT),
                                            sound_param(deluge::instruments::classic::param::kDelayFeedback), Hint::Bar,
                                            nullptr, sid(deluge::l10n::String::STRING_FOR_DELAY_AMOUNT))),
                     sid(deluge::l10n::String::STRING_FOR_AMOUNT_SHORT)),
    delay_amount_draw);
inline constexpr Node delay_ping_pong = with_affect_entire(with_column_art(
    with_selection_is_toggle(
        with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_PINGPONG), Kind::Enum,
                                {.kind = deluge::ui::scenes::bind::Target::Kind::EFFECT, .path = "delay/pingPong"}),
                   sid(deluge::l10n::String::STRING_FOR_DELAY_PINGPONG))),
    switch_art));
inline constexpr Node delay_sync = with_affect_entire(
    with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_SYNC), Kind::SyncLevel,
                            {.kind = deluge::ui::scenes::bind::Target::Kind::EFFECT, .path = "delay/syncLevel"}),
               sid(deluge::l10n::String::STRING_FOR_DELAY_SYNC)));
inline constexpr Node delay_rate = with_patch_chain(
    param(sid(deluge::l10n::String::STRING_FOR_RATE), sound_param(deluge::instruments::classic::param::kDelayRate),
          Hint::Knob, nullptr, sid(deluge::l10n::String::STRING_FOR_DELAY_RATE)));
inline constexpr Node delay_type = with_affect_entire(
    with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_TYPE), Kind::Enum,
                            {.kind = deluge::ui::scenes::bind::Target::Kind::EFFECT, .path = "delay/type"}),
               sid(deluge::l10n::String::STRING_FOR_DELAY_TYPE)));

inline constexpr const Node* kDelayChildren[] = {&delay_amount, &delay_ping_pong, &delay_sync, &delay_rate,
                                                 &delay_type};
inline constexpr Node delay = submenu(sid(deluge::l10n::String::STRING_FOR_DELAY), kDelayChildren, Layout::Horizontal);

// --- Reverb ------------------------------------------------------------------------------------
//
// The four float-backed items share `{0, kMaxMenuValue}` (`reverb::RoomSize::readCurrentValue` and siblings); the pan
// is -25..25 (`Pan::getMaxValue` and `Pan::getMinValue` in `origin/main:src/deluge/gui/menu_item/reverb/pan.h`); the
// sidechain volume starts at -1, which the item draws as AUTO (`Volume::getMinValue` and `Volume::drawValue` in
// `origin/main:src/deluge/gui/menu_item/reverb/sidechain/volume.h`).

inline constexpr Node reverb_amount =
    with_column_name(with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_AMOUNT),
                                            sound_param(deluge::instruments::classic::param::kReverbAmount), Hint::Bar,
                                            nullptr, sid(deluge::l10n::String::STRING_FOR_REVERB_AMOUNT))),
                     sid(deluge::l10n::String::STRING_FOR_AMOUNT_SHORT));
inline constexpr Node reverb_room_size =
    with_name_text(with_value_text(setting_item(sid(deluge::l10n::String::STRING_FOR_ROOM_SIZE), Kind::Int,
                                                {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                                 .path = "reverb/roomSize",
                                                 .group = deluge::project::SettingGroupId::SONG},
                                                Hint::Knob),
                                   reverb_room_size_text),
                   reverb_room_size_text);
inline constexpr Node reverb_damping = setting_item(sid(deluge::l10n::String::STRING_FOR_DAMPING), Kind::Int,
                                                    {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                                     .path = "reverb/dampening",
                                                     .group = deluge::project::SettingGroupId::SONG},
                                                    Hint::Knob);
inline constexpr Node reverb_width =
    with_name_text(with_value_text(setting_item(sid(deluge::l10n::String::STRING_FOR_WIDTH), Kind::Int,
                                                {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                                 .path = "reverb/width",
                                                 .group = deluge::project::SettingGroupId::SONG},
                                                Hint::Knob),
                                   reverb_width_text),
                   reverb_width_text);
inline constexpr Node reverb_model =
    with_column_name(setting_item(sid(deluge::l10n::String::STRING_FOR_MODEL), Kind::Enum,
                                  {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                   .path = "reverb/model",
                                   .group = deluge::project::SettingGroupId::SONG}),
                     sid(deluge::l10n::String::STRING_FOR_MODEL_SHORT));
inline constexpr Node reverb_pan = with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_PAN), Kind::Int,
                                                           {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                                            .path = "reverb/pan",
                                                            .group = deluge::project::SettingGroupId::SONG},
                                                           Hint::Pan),
                                              sid(deluge::l10n::String::STRING_FOR_REVERB_PAN));
inline constexpr Node reverb_hpf = setting_item(sid(deluge::l10n::String::STRING_FOR_HPF), Kind::Int,
                                                {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                                 .path = "reverb/hpf",
                                                 .group = deluge::project::SettingGroupId::SONG},
                                                Hint::Hpf);
inline constexpr Node reverb_lpf = setting_item(sid(deluge::l10n::String::STRING_FOR_LPF), Kind::Int,
                                                {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                                 .path = "reverb/lpf",
                                                 .group = deluge::project::SettingGroupId::SONG},
                                                Hint::Lpf);

inline constexpr Node reverb_sidechain_volume =
    with_leaf_text(with_column_name(with_value_text(int_item(sid(deluge::l10n::String::STRING_FOR_VOLUME_DUCKING),
                                                             get_reverb_sidechain_volume, set_reverb_sidechain_volume,
                                                             IntRange{-1, plugin::kMaxMenuSteps, 1}, Hint::Ducking),
                                                    ducking_auto_text),
                                    sid(deluge::l10n::String::STRING_FOR_VOLUME_DUCKING_SHORT)),
                   ducking_auto_text);
inline constexpr Node reverb_sidechain_shape =
    with_column_name(with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_SHAPE), Kind::Int,
                                             {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                              .path = "reverb/compressor/shape",
                                              .group = deluge::project::SettingGroupId::SONG},
                                             Hint::Knob),
                                sid(deluge::l10n::String::STRING_FOR_SIDECH_SHAPE_MENU_TITLE)),
                     sid(deluge::l10n::String::STRING_FOR_SHAPE_SHORT));
inline constexpr Node reverb_sidechain_attack =
    with_affect_entire(with_column_name(with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_ATTACK), Kind::Int,
                                                                {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                                                 .path = "reverb/compressor/attack",
                                                                 .group = deluge::project::SettingGroupId::SONG},
                                                                Hint::Attack),
                                                   sid(deluge::l10n::String::STRING_FOR_SIDECH_ATTACK_MENU_TITLE)),
                                        sid(deluge::l10n::String::STRING_FOR_ATTACK_SHORT)));
inline constexpr Node reverb_sidechain_release = with_affect_entire(
    with_column_name(with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_RELEASE), Kind::Int,
                                             {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                              .path = "reverb/compressor/release",
                                              .group = deluge::project::SettingGroupId::SONG},
                                             Hint::Release),
                                sid(deluge::l10n::String::STRING_FOR_SIDECH_RELEASE_MENU_TITLE)),
                     sid(deluge::l10n::String::STRING_FOR_RELEASE_SHORT)));
inline constexpr Node reverb_sidechain_sync = with_affect_entire(sync_level_item(
    sid(deluge::l10n::String::STRING_FOR_SYNC), sid(deluge::l10n::String::STRING_FOR_SIDECHAIN_SYNC),
    get_reverb_sidechain_sync, set_reverb_sidechain_sync, kSidechainSyncRange, relevance::reverb_sidechain_on));

inline constexpr const Node* kReverbSidechainChildren[] = {&reverb_sidechain_volume, &reverb_sidechain_shape,
                                                           &reverb_sidechain_attack, &reverb_sidechain_release,
                                                           &reverb_sidechain_sync};
inline constexpr Node reverb_sidechain = with_fixed_columns(
    submenu(sid(deluge::l10n::String::STRING_FOR_REVERB_SIDECHAIN), kReverbSidechainChildren, Layout::Horizontal,
            nullptr, sid(deluge::l10n::String::STRING_FOR_REVERB_SIDECH_MENU_TITLE)));

inline constexpr const Node* kReverbChildren[] = {&reverb_amount, &reverb_room_size, &reverb_damping,
                                                  &reverb_width,  &reverb_model,     &reverb_pan,
                                                  &reverb_hpf,    &reverb_lpf,       &reverb_sidechain};
inline constexpr Node reverb =
    submenu(sid(deluge::l10n::String::STRING_FOR_REVERB), kReverbChildren, Layout::Horizontal);

// --- Stutter -----------------------------------------------------------------------------------

inline constexpr Node stutter_rate = with_turn(
    with_leaf_text(with_value_text(param(sid(deluge::l10n::String::STRING_FOR_RATE),
                                         sound_param(deluge::instruments::classic::param::kStutterRate), Hint::Knob,
                                         nullptr, sid(deluge::l10n::String::STRING_FOR_STUTTER_RATE)),
                                   stutter_rate_text),
                   stutter_rate_text),
    stutter_rate_turn);
inline constexpr Node stutter_direction = with_affect_entire(with_value_text(
    with_column_art(dynamic_enumeration(sid(deluge::l10n::String::STRING_FOR_DIRECTION), stutter_direction_names,
                                        get_stutter_direction, set_stutter_direction),
                    stutter_direction_art),
    stutter_direction_text));
inline constexpr Node stutter_quantize =
    with_affect_entire(toggle_item(sid(deluge::l10n::String::STRING_FOR_QUANTIZE), get_stutter_quantize,
                                   set_stutter_quantize, relevance::stutter_quantize_allowed));

inline constexpr const Node* kStutterChildren[] = {&stutter_rate, &stutter_direction, &stutter_quantize};
inline constexpr Node stutter =
    with_fixed_columns(submenu(sid(deluge::l10n::String::STRING_FOR_STUTTER), kStutterChildren, Layout::Horizontal));

// --- Mod FX ------------------------------------------------------------------------------------

inline constexpr Node mod_fx_type =
    with_affect_entire(with_option_list_column(without_notification(without_column_label(with_slots(
        with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_TYPE), Kind::Enum,
                                {.kind = deluge::ui::scenes::bind::Target::Kind::EFFECT, .path = "modFXType"}),
                   sid(deluge::l10n::String::STRING_FOR_MODFX_TYPE)),
        4))))); // `origin/main:src/deluge/gui/menu_item/mod_fx/type.h`
inline constexpr Node mod_fx_depth =
    with_name_text(with_value_text(with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_DEPTH),
                                                          sound_param(deluge::instruments::classic::param::kModFXDepth),
                                                          Hint::Bar, param_shows::kModFxDepth,
                                                          sid(deluge::l10n::String::STRING_FOR_MODFX_DEPTH))),
                                   mod_fx_depth_text),
                   mod_fx_depth_text);
inline constexpr Node mod_fx_rate = with_patch_chain(
    param(sid(deluge::l10n::String::STRING_FOR_RATE), sound_param(deluge::instruments::classic::param::kModFXRate),
          Hint::Knob, param_shows::kModFxIsOn, sid(deluge::l10n::String::STRING_FOR_MODFX_RATE)));
inline constexpr Node mod_fx_feedback = with_name_text(
    with_value_text(param(sid(deluge::l10n::String::STRING_FOR_FEEDBACK),
                          sound_param(deluge::instruments::classic::param::kModFXFeedback), Hint::Knob,
                          param_shows::kModFxFeedback, sid(deluge::l10n::String::STRING_FOR_MODFX_FEEDBACK)),
                    mod_fx_feedback_text),
    mod_fx_feedback_text);
inline constexpr Node mod_fx_offset =
    with_name_text(with_value_text(param(sid(deluge::l10n::String::STRING_FOR_OFFSET),
                                         sound_param(deluge::instruments::classic::param::kModFXOffset), Hint::Knob,
                                         param_shows::kModFxOffset, sid(deluge::l10n::String::STRING_FOR_MODFX_OFFSET)),
                                   mod_fx_offset_text),
                   mod_fx_offset_text);

inline constexpr const Node* kModFxChildren[] = {&mod_fx_type, &mod_fx_depth, &mod_fx_rate, &mod_fx_feedback,
                                                 &mod_fx_offset};
inline constexpr Node mod_fx = with_page_title(
    submenu(sid(deluge::l10n::String::STRING_FOR_MOD_FX), kModFxChildren, Layout::Horizontal), mod_fx_page_title);

// --- Distortion --------------------------------------------------------------------------------

inline constexpr Node saturation = with_affect_entire(with_value_style(
    setting_item(sid(deluge::l10n::String::STRING_FOR_SATURATION), Kind::Int,
                 {.kind = deluge::ui::scenes::bind::Target::Kind::EFFECT, .path = "clippingAmount"}, Hint::Bar),
    deluge::ui::widgets::oled::ValueStyle{.off_at_zero = true}));
inline constexpr Node bitcrush = param(sid(deluge::l10n::String::STRING_FOR_BITCRUSH),
                                       sound_param(deluge::instruments::classic::param::kBitcrushAmount), Hint::Bar);
inline constexpr Node decimation =
    param(sid(deluge::l10n::String::STRING_FOR_DECIMATION),
          sound_param(deluge::instruments::classic::param::kSampleRateReduction), Hint::Bar);
inline constexpr Node wavefold =
    with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_WAVEFOLD),
                           sound_param(deluge::instruments::classic::param::kWaveFold), Hint::Bar));

inline constexpr const Node* kDistortionChildren[] = {&saturation, &bitcrush, &decimation, &wavefold};
inline constexpr Node distortion =
    submenu(sid(deluge::l10n::String::STRING_FOR_DISTORTION), kDistortionChildren, Layout::Horizontal);

// --- The page itself ---------------------------------------------------------------------------

inline constexpr const Node* kFxPageChildren[] = {&eq, &delay, &reverb, &stutter, &mod_fx, &distortion, &noise};
inline constexpr Node sound_fx = submenu(sid(deluge::l10n::String::STRING_FOR_FX), kFxPageChildren);

/// @brief This file's contribution to the sound-editor root: `soundFXMenu`.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundFxChildren;

} // namespace deluge::ui::scenes::menu::nodes
