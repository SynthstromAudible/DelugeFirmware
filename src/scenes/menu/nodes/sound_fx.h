#pragma once

/// @brief The FX subtree as data: `sound_fx` and its seven pages (EQ, delay, reverb, stutter,
///        mod FX, distortion) plus the noise level, which is task 6's own node.
///
/// `inline constexpr` for the reason `sound_filters.h` explains (task-6 fix round 1, I3). From
/// `gui/ui/menus.cpp`'s `soundFXMenu` literal (`:1749-1760`) and the page literals it names.
///
// clang-format off
/// | node | legacy global | legacy class | kind |
/// |---|---|---|---|
/// | `eq_bass` | `bassMenu` (`:745`) | `eq::EqUnpatchedParam`, `UNPATCHED_BASS` | Int (param) |
/// | `eq_treble` | `trebleMenu` (`:746`) | same, `UNPATCHED_TREBLE` | Int (param) |
/// | `eq_bass_freq` | `bassFreqMenu` (`:747`) | same, `UNPATCHED_BASS_FREQ` | Int (param) |
/// | `eq_treble_freq` | `trebleFreqMenu` (`:749`) | same, `UNPATCHED_TREBLE_FREQ` | Int (param) |
/// | `eq` | `eqMenu` (`:752`) | `eq::EqMenu` (`eq/eq_menu.h:12`), a `HorizontalMenu` | Submenu (Horizontal) |
/// | `delay_amount` | `delayFeedbackMenu` (`:763`) | `delay::Amount` (`delay/amount.h:9`), `GLOBAL_DELAY_FEEDBACK` | Int (param) |
/// | `delay_ping_pong` | `delayPingPongMenu` (`:766`) | `delay::PingPong` (`delay/ping_pong.h:5`) | Enum (toggle) |
/// | `delay_sync` | `delaySyncMenu` (`:768`) | `delay::Sync` (`delay/sync.h:4`) | SyncLevel |
/// | `delay_rate` | `delayRateMenu` (`:765`) | `patched_param::Integer`, `GLOBAL_DELAY_RATE` | Int (param) |
/// | `delay_type` | `delayAnalogMenu` (`:767`) | `delay::Analog` (`delay/analog.h:5`) | Enum |
/// | `delay` | `delayMenu` (`:770`) | `HorizontalMenu` | Submenu (Horizontal) |
/// | `reverb_amount` | `reverbAmountMenu` (`:847`) | `reverb::Amount` (`reverb/amount.h:5`), `GLOBAL_REVERB_AMOUNT` | Int (param) |
/// | `reverb_room_size` | `reverbRoomSizeMenu` (`:849`) | `reverb::RoomSize` (`reverb/room_size.h:5`) | Int |
/// | `reverb_damping` | `reverbDampingMenu` (`:850`) | `reverb::Damping` (`reverb/damping.h:4`) | Int |
/// | `reverb_width` | `reverbWidthMenu` (`:851`) | `reverb::Width` (`reverb/width.h:4`) | Int |
/// | `reverb_model` | `reverbModelMenu` (`:853`) | `reverb::Model` (`reverb/model.h:10`) | Enum |
/// | `reverb_pan` | `reverbPanMenu` (`:852`) | `reverb::Pan` (`reverb/pan.h:5`) | Int (pan scaling) |
/// | `reverb_hpf` | `reverbHPFMenu` (`:854`) | `reverb::HPF` (`reverb/hpf.h:4`) | Int |
/// | `reverb_lpf` | `reverbLPFMenu` (`:855`) | `reverb::LPF` (`reverb/lpf.h:4`) | Int |
/// | `reverb_sidechain_volume` | `reverbSidechainVolumeMenu` (`:827`) | `reverb::sidechain::Volume` (`reverb/sidechain/volume.h:5`) | Int |
/// | `reverb_sidechain_shape` | `reverbSidechainShapeMenu` (`:833`) | `reverb::sidechain::Shape` (`reverb/sidechain/shape.h:5`) | Int |
/// | `reverb_sidechain_attack` | `reverbSidechainAttackMenu` (`:829`) | `sidechain::Attack`, reverb spelling | Int |
/// | `reverb_sidechain_release` | `reverbSidechainReleaseMenu` (`:831`) | `sidechain::Release`, reverb spelling | Int |
/// | `reverb_sidechain_sync` | `reverbSidechainSyncMenu` (`:828`) | `sidechain::Sync`, reverb spelling | SyncLevel, 10 options |
/// | `reverb_sidechain` | `reverbSidechainMenu` (`:835`) | `HorizontalMenu`, `FIXED` | Submenu (Horizontal) |
/// | `reverb` | `reverbMenu` (`:857`) | `HorizontalMenu` | Submenu (Horizontal) |
/// | `stutter_rate` | `stutterRateMenu` (`:784`) | `stutter::Rate` (`stutter/rate.h:5`), `UNPATCHED_STUTTER_RATE` | Int (param) |
/// | `stutter_direction` | `stutterDirectionMenu` (`:782`) | `stutter::StutterDirection` (`stutter/direction.h:5`) | Enum (dynamic) |
/// | `stutter_quantize` | `stutterQuantizedMenu` (`:783`) | `stutter::QuantizedStutter` (`stutter/quantized.h:5`) | Toggle |
/// | `stutter` | `stutterMenu` (`:786`) | `HorizontalMenu`, `FIXED` | Submenu (Horizontal) |
/// | `mod_fx_type` | `modFXTypeMenu` (`:724`) | `mod_fx::Type` (`mod_fx/type.h:5`) | Enum (dynamic), 4 slots |
/// | `mod_fx_depth` | `modFXDepthMenu` (`:728`) | `mod_fx::Depth_Patched` (`depth_patched.h:4`), `GLOBAL_MOD_FX_DEPTH` | Int (param) |
/// | `mod_fx_rate` | `modFXRateMenu` (`:725`) | `mod_fx::Rate` (`mod_fx/rate.h:4`), `GLOBAL_MOD_FX_RATE` | Int (param) |
/// | `mod_fx_feedback` | `modFXFeedbackMenu` (`:726`) | `mod_fx::Feedback` (`mod_fx/feedback.h:4`), `UNPATCHED_MOD_FX_FEEDBACK` | Int (param) |
/// | `mod_fx_offset` | `modFXOffsetMenu` (`:730`) | `mod_fx::Offset` (`mod_fx/offset.h:5`), `UNPATCHED_MOD_FX_OFFSET` | Int (param) |
/// | `mod_fx` | `modFXMenu` (`:733`) | `submenu::ModFxHorizontalMenu` (`submenu/mod_fx.h:8`) | Submenu (Horizontal) |
/// | `saturation` | `clippingMenu` (`:883`) | `fx::Clipping` (`fx/clipping.h:5`) | Int |
/// | `bitcrush` | `bitcrushMenu` (`:886`) | `UnpatchedParam`, `UNPATCHED_BITCRUSHING`, BAR | Int (param) |
/// | `decimation` | `srrMenu` (`:884`) | `UnpatchedParam`, `UNPATCHED_SAMPLE_RATE_REDUCTION`, BAR | Int (param) |
/// | `wavefold` | `foldMenu` (`:887`) | `patched_param::Integer`, `LOCAL_FOLD`, BAR | Int (param) |
/// | `distortion` | `soundDistortionMenu` (`:890`) | `HorizontalMenu` | Submenu (Horizontal) |
/// | `noise` | `noiseMenu` | `patched_param::IntegerNonFM` -- **task 6's**, `sound_osc.h` | Int (param) |
/// | `sound_fx` | `soundFXMenu` (`:1749`) | `Submenu` | Submenu (Vertical) |
// clang-format on
///
/// @note **The reverb is engine-wide, not per-clip.** Every `reverb::*` item but the amount reads
///       and writes `AudioEngine::reverb` / `AudioEngine::reverbPan` /
///       `AudioEngine::reverbSidechain*` directly (`reverb/room_size.h:22-23`,
///       `reverb/sidechain/volume.h:22-27`), so their hooks ignore the `MenuContext` entirely. That
///       is legacy's design, not a simplification: one reverb serves the whole song.
///
/// @note **Three dynamic names, one dynamic option list.** `mod_fx::Depth_Patched`, `Feedback` and
///       `Offset` take their `getName()` from `modfx::getParamName(type, ...)`
///       (`mod_fx/depth_patched.h:29-31`), and `reverb::RoomSize` / `Width` read "TIME" /
///       "DIFFUSION" on the two newer models (`room_size.h:29-38`). A `Node::name` is one static id,
///       so all five carry the base string and `tables_2_spec` exempts them by name, exactly as it
///       does batch 1's `FormattedTitle` items. `mod_fx::Type`'s and `StutterDirection`'s option
///       *lists* are genuinely dynamic and use `dynamic_enumeration`.
///
/// @note **`HorizontalMenu::Layout::FIXED` has no `Node::Layout` spelling**, so `stutterMenu`
///       (`menus.cpp:788`) and `reverbSidechainMenu` (`:841`) lose their fixed paging here, exactly
///       as `unisonMenu` and `voiceMenu` do (`sound_unison.h`, `sound_voice.h` -- the same note,
///       now in all three files rather than two; review round 1, M11). `Node::Layout` carries
///       Vertical / Horizontal only, and the paging mode belongs to E2's interpreter work.
///
/// @note **Column labels have no `Node` spelling.** Eleven items here override `getColumnLabel()`
///       to abbreviate ("AMOUNT" -> "AMT", "WIDTH" -> "DIFFUSION", ...). That is the general gap
///       `sound_filters.h` already records; nothing in this file works around it.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/sound_lfo.h"       // kSyncLevelRange
#include "scenes/menu/nodes/sound_osc.h"       // the noise level
#include "scenes/menu/nodes/sound_sidechain.h" // kSidechainSyncRange, kSidechainRateRange
#include "scenes/menu/relevance.h"

#include <span>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @name The FX pages' hooks. Bodies (and their citations) in the `.cpp`.
/// @{
int32_t get_delay_ping_pong(const MenuContext& ctx);
void set_delay_ping_pong(const MenuContext& ctx, int32_t value);
int32_t get_delay_sync(const MenuContext& ctx);
void set_delay_sync(const MenuContext& ctx, int32_t value);
int32_t get_delay_type(const MenuContext& ctx);
void set_delay_type(const MenuContext& ctx, int32_t value);

int32_t get_reverb_room_size(const MenuContext& ctx);
void set_reverb_room_size(const MenuContext& ctx, int32_t value);
int32_t get_reverb_damping(const MenuContext& ctx);
void set_reverb_damping(const MenuContext& ctx, int32_t value);
int32_t get_reverb_width(const MenuContext& ctx);
void set_reverb_width(const MenuContext& ctx, int32_t value);
int32_t get_reverb_model(const MenuContext& ctx);
void set_reverb_model(const MenuContext& ctx, int32_t value);
int32_t get_reverb_pan(const MenuContext& ctx);
void set_reverb_pan(const MenuContext& ctx, int32_t value);
int32_t get_reverb_hpf(const MenuContext& ctx);
void set_reverb_hpf(const MenuContext& ctx, int32_t value);
int32_t get_reverb_lpf(const MenuContext& ctx);
void set_reverb_lpf(const MenuContext& ctx, int32_t value);
int32_t get_reverb_sidechain_volume(const MenuContext& ctx);
void set_reverb_sidechain_volume(const MenuContext& ctx, int32_t value);
int32_t get_reverb_sidechain_shape(const MenuContext& ctx);
void set_reverb_sidechain_shape(const MenuContext& ctx, int32_t value);
int32_t get_reverb_sidechain_attack(const MenuContext& ctx);
void set_reverb_sidechain_attack(const MenuContext& ctx, int32_t value);
int32_t get_reverb_sidechain_release(const MenuContext& ctx);
void set_reverb_sidechain_release(const MenuContext& ctx, int32_t value);
int32_t get_reverb_sidechain_sync(const MenuContext& ctx);
void set_reverb_sidechain_sync(const MenuContext& ctx, int32_t value);

size_t stutter_direction_names(const MenuContext& ctx, std::span<std::string_view> out);
int32_t get_stutter_direction(const MenuContext& ctx);
void set_stutter_direction(const MenuContext& ctx, int32_t value);
int32_t get_stutter_quantize(const MenuContext& ctx);
void set_stutter_quantize(const MenuContext& ctx, int32_t value);

size_t mod_fx_type_names(const MenuContext& ctx, std::span<std::string_view> out);
int32_t get_mod_fx_type(const MenuContext& ctx);
void set_mod_fx_type(const MenuContext& ctx, int32_t value);

int32_t get_saturation(const MenuContext& ctx);
void set_saturation(const MenuContext& ctx, int32_t value);
/// @}

/// `delay::Analog::getOptions` (`delay/analog.h:31-35`), FULL spelling.
inline constexpr uint16_t kDelayTypeNames[] = {
    sid(deluge::l10n::String::STRING_FOR_DIGITAL),
    sid(deluge::l10n::String::STRING_FOR_ANALOG),
};
/// `delay::PingPong::getOptions` (`delay/ping_pong.h:34-41`).
inline constexpr uint16_t kDelayPingPongNames[] = {
    sid(deluge::l10n::String::STRING_FOR_OFF),
    sid(deluge::l10n::String::STRING_FOR_ON),
};
/// `reverb::Model::getOptions` (`reverb/model.h:19-23`).
inline constexpr uint16_t kReverbModelNames[] = {
    sid(deluge::l10n::String::STRING_FOR_FREEVERB),
    sid(deluge::l10n::String::STRING_FOR_MUTABLE),
    sid(deluge::l10n::String::STRING_FOR_DIGITAL),
};

// --- EQ ----------------------------------------------------------------------------------------
//
// `EqUnpatchedParam`'s three-argument constructor takes `(name, columnLabel, p)` and forwards
// `UnpatchedParam(name, p)` (`eq/eq_unpatched_param.h:22`) -- so the second string is the *column
// label*, not a title, and each node's title is its name. No `getRenderingStyle` override, so the
// `IntegerContinuous` default, KNOB.

inline constexpr Node eq_bass =
    param(sid(deluge::l10n::String::STRING_FOR_BASS),
          {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_BASS});
inline constexpr Node eq_treble =
    param(sid(deluge::l10n::String::STRING_FOR_TREBLE),
          {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_TREBLE});
inline constexpr Node eq_bass_freq =
    param(sid(deluge::l10n::String::STRING_FOR_BASS_FREQUENCY),
          {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_BASS_FREQ});
inline constexpr Node eq_treble_freq =
    param(sid(deluge::l10n::String::STRING_FOR_TREBLE_FREQUENCY),
          {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_TREBLE_FREQ});

inline constexpr const Node* kEqChildren[] = {&eq_bass, &eq_treble, &eq_bass_freq, &eq_treble_freq};
inline constexpr Node eq = submenu(sid(deluge::l10n::String::STRING_FOR_EQ), kEqChildren, Layout::Horizontal);

// --- Delay -------------------------------------------------------------------------------------

inline constexpr Node delay_amount =
    param(sid(deluge::l10n::String::STRING_FOR_AMOUNT),
          {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::GLOBAL_DELAY_FEEDBACK}, Hint::Bar,
          nullptr, sid(deluge::l10n::String::STRING_FOR_DELAY_AMOUNT));
inline constexpr Node delay_ping_pong =
    with_title(enumeration(sid(deluge::l10n::String::STRING_FOR_PINGPONG), kDelayPingPongNames, get_delay_ping_pong,
                           set_delay_ping_pong),
               sid(deluge::l10n::String::STRING_FOR_DELAY_PINGPONG));
inline constexpr Node delay_sync =
    sync_level_item(sid(deluge::l10n::String::STRING_FOR_SYNC), sid(deluge::l10n::String::STRING_FOR_DELAY_SYNC),
                    get_delay_sync, set_delay_sync, kSyncLevelRange);
inline constexpr Node delay_rate =
    param(sid(deluge::l10n::String::STRING_FOR_RATE),
          {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::GLOBAL_DELAY_RATE}, Hint::Knob,
          nullptr, sid(deluge::l10n::String::STRING_FOR_DELAY_RATE));
inline constexpr Node delay_type =
    with_title(enumeration(sid(deluge::l10n::String::STRING_FOR_TYPE), kDelayTypeNames, get_delay_type, set_delay_type),
               sid(deluge::l10n::String::STRING_FOR_DELAY_TYPE));

inline constexpr const Node* kDelayChildren[] = {&delay_amount, &delay_ping_pong, &delay_sync, &delay_rate,
                                                 &delay_type};
inline constexpr Node delay = submenu(sid(deluge::l10n::String::STRING_FOR_DELAY), kDelayChildren, Layout::Horizontal);

// --- Reverb ------------------------------------------------------------------------------------
//
// The four float-backed items share `{0, kMaxMenuValue}` (`reverb/room_size.h:24` and siblings);
// the pan is -25..25 (`reverb/pan.h:26-27`); the sidechain volume starts at -1, which the item
// draws as AUTO (`reverb/sidechain/volume.h:26-27,30-39`).

inline constexpr Node reverb_amount =
    param(sid(deluge::l10n::String::STRING_FOR_AMOUNT),
          {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::GLOBAL_REVERB_AMOUNT}, Hint::Bar,
          nullptr, sid(deluge::l10n::String::STRING_FOR_REVERB_AMOUNT));
inline constexpr Node reverb_room_size = int_item(sid(deluge::l10n::String::STRING_FOR_ROOM_SIZE), get_reverb_room_size,
                                                  set_reverb_room_size, IntRange{0, kMaxMenuValue, 1}, Hint::Knob);
inline constexpr Node reverb_damping = int_item(sid(deluge::l10n::String::STRING_FOR_DAMPING), get_reverb_damping,
                                                set_reverb_damping, IntRange{0, kMaxMenuValue, 1}, Hint::Knob);
inline constexpr Node reverb_width = with_title(int_item(sid(deluge::l10n::String::STRING_FOR_WIDTH), get_reverb_width,
                                                         set_reverb_width, IntRange{0, kMaxMenuValue, 1}, Hint::Knob),
                                                sid(deluge::l10n::String::STRING_FOR_REVERB_WIDTH));
inline constexpr Node reverb_model =
    enumeration(sid(deluge::l10n::String::STRING_FOR_MODEL), kReverbModelNames, get_reverb_model, set_reverb_model);
inline constexpr Node reverb_pan =
    with_title(int_item(sid(deluge::l10n::String::STRING_FOR_PAN), get_reverb_pan, set_reverb_pan,
                        IntRange{kMinMenuRelativeValue, kMaxMenuRelativeValue, 1}, Hint::Pan),
               sid(deluge::l10n::String::STRING_FOR_REVERB_PAN));
inline constexpr Node reverb_hpf = int_item(sid(deluge::l10n::String::STRING_FOR_HPF), get_reverb_hpf, set_reverb_hpf,
                                            IntRange{0, kMaxMenuValue, 1}, Hint::Hpf, relevance::reverb_filters);
inline constexpr Node reverb_lpf = int_item(sid(deluge::l10n::String::STRING_FOR_LPF), get_reverb_lpf, set_reverb_lpf,
                                            IntRange{0, kMaxMenuValue, 1}, Hint::Lpf, relevance::reverb_filters);

inline constexpr Node reverb_sidechain_volume =
    int_item(sid(deluge::l10n::String::STRING_FOR_VOLUME_DUCKING), get_reverb_sidechain_volume,
             set_reverb_sidechain_volume, IntRange{-1, kMaxMenuValue, 1}, Hint::Ducking);
inline constexpr Node reverb_sidechain_shape = with_title(
    int_item(sid(deluge::l10n::String::STRING_FOR_SHAPE), get_reverb_sidechain_shape, set_reverb_sidechain_shape,
             IntRange{0, kMaxMenuValue, 1}, Hint::Knob, relevance::reverb_sidechain_on),
    sid(deluge::l10n::String::STRING_FOR_SIDECH_SHAPE_MENU_TITLE));
inline constexpr Node reverb_sidechain_attack =
    with_title(int_item(sid(deluge::l10n::String::STRING_FOR_ATTACK), get_reverb_sidechain_attack,
                        set_reverb_sidechain_attack, kSidechainRateRange, Hint::Attack, relevance::reverb_sidechain_on),
               sid(deluge::l10n::String::STRING_FOR_SIDECH_ATTACK_MENU_TITLE));
inline constexpr Node reverb_sidechain_release = with_title(
    int_item(sid(deluge::l10n::String::STRING_FOR_RELEASE), get_reverb_sidechain_release, set_reverb_sidechain_release,
             kSidechainRateRange, Hint::Release, relevance::reverb_sidechain_on),
    sid(deluge::l10n::String::STRING_FOR_SIDECH_RELEASE_MENU_TITLE));
inline constexpr Node reverb_sidechain_sync = sync_level_item(
    sid(deluge::l10n::String::STRING_FOR_SYNC), sid(deluge::l10n::String::STRING_FOR_SIDECHAIN_SYNC),
    get_reverb_sidechain_sync, set_reverb_sidechain_sync, kSidechainSyncRange, relevance::reverb_sidechain_on);

inline constexpr const Node* kReverbSidechainChildren[] = {&reverb_sidechain_volume, &reverb_sidechain_shape,
                                                           &reverb_sidechain_attack, &reverb_sidechain_release,
                                                           &reverb_sidechain_sync};
inline constexpr Node reverb_sidechain =
    submenu(sid(deluge::l10n::String::STRING_FOR_REVERB_SIDECHAIN), kReverbSidechainChildren, Layout::Horizontal,
            nullptr, sid(deluge::l10n::String::STRING_FOR_REVERB_SIDECH_MENU_TITLE));

inline constexpr const Node* kReverbChildren[] = {&reverb_amount, &reverb_room_size, &reverb_damping,
                                                  &reverb_width,  &reverb_model,     &reverb_pan,
                                                  &reverb_hpf,    &reverb_lpf,       &reverb_sidechain};
inline constexpr Node reverb =
    submenu(sid(deluge::l10n::String::STRING_FOR_REVERB), kReverbChildren, Layout::Horizontal);

// --- Stutter -----------------------------------------------------------------------------------

inline constexpr Node stutter_rate =
    param(sid(deluge::l10n::String::STRING_FOR_RATE),
          {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_STUTTER_RATE},
          Hint::Knob, nullptr, sid(deluge::l10n::String::STRING_FOR_STUTTER_RATE));
inline constexpr Node stutter_direction =
    dynamic_enumeration(sid(deluge::l10n::String::STRING_FOR_DIRECTION), stutter_direction_names, get_stutter_direction,
                        set_stutter_direction);
inline constexpr Node stutter_quantize =
    toggle_item(sid(deluge::l10n::String::STRING_FOR_QUANTIZE), get_stutter_quantize, set_stutter_quantize,
                relevance::stutter_quantize_allowed);

inline constexpr const Node* kStutterChildren[] = {&stutter_rate, &stutter_direction, &stutter_quantize};
inline constexpr Node stutter =
    submenu(sid(deluge::l10n::String::STRING_FOR_STUTTER), kStutterChildren, Layout::Horizontal);

// --- Mod FX ------------------------------------------------------------------------------------

inline constexpr Node mod_fx_type =
    with_slots(with_title(dynamic_enumeration(sid(deluge::l10n::String::STRING_FOR_TYPE), mod_fx_type_names,
                                              get_mod_fx_type, set_mod_fx_type),
                          sid(deluge::l10n::String::STRING_FOR_MODFX_TYPE)),
               4); // `mod_fx/type.h:60-63`
inline constexpr Node mod_fx_depth =
    param(sid(deluge::l10n::String::STRING_FOR_DEPTH),
          {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::GLOBAL_MOD_FX_DEPTH}, Hint::Bar,
          relevance::mod_fx_has_depth, sid(deluge::l10n::String::STRING_FOR_MODFX_DEPTH));
inline constexpr Node mod_fx_rate =
    param(sid(deluge::l10n::String::STRING_FOR_RATE),
          {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::GLOBAL_MOD_FX_RATE}, Hint::Knob,
          relevance::mod_fx_on, sid(deluge::l10n::String::STRING_FOR_MODFX_RATE));
inline constexpr Node mod_fx_feedback =
    param(sid(deluge::l10n::String::STRING_FOR_FEEDBACK),
          {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_MOD_FX_FEEDBACK},
          Hint::Knob, relevance::mod_fx_has_feedback, sid(deluge::l10n::String::STRING_FOR_MODFX_FEEDBACK));
inline constexpr Node mod_fx_offset =
    param(sid(deluge::l10n::String::STRING_FOR_OFFSET),
          {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_MOD_FX_OFFSET},
          Hint::Knob, relevance::mod_fx_has_offset, sid(deluge::l10n::String::STRING_FOR_MODFX_OFFSET));

inline constexpr const Node* kModFxChildren[] = {&mod_fx_type, &mod_fx_depth, &mod_fx_rate, &mod_fx_feedback,
                                                 &mod_fx_offset};
inline constexpr Node mod_fx =
    submenu(sid(deluge::l10n::String::STRING_FOR_MOD_FX), kModFxChildren, Layout::Horizontal);

// --- Distortion --------------------------------------------------------------------------------

inline constexpr Node saturation = int_item(sid(deluge::l10n::String::STRING_FOR_SATURATION), get_saturation,
                                            set_saturation, IntRange{0, 15, 1}, Hint::Bar); // `fx/clipping.h:44-45`
inline constexpr Node bitcrush = param(
    sid(deluge::l10n::String::STRING_FOR_BITCRUSH),
    {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_BITCRUSHING}, Hint::Bar);
inline constexpr Node decimation = param(
    sid(deluge::l10n::String::STRING_FOR_DECIMATION),
    {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_SAMPLE_RATE_REDUCTION},
    Hint::Bar);
inline constexpr Node wavefold =
    param(sid(deluge::l10n::String::STRING_FOR_WAVEFOLD),
          {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_FOLD}, Hint::Bar);

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

} // namespace deluge::scenes::menu::nodes
