#pragma once

/// @brief The nine non-settings roots as data (task 8, batch 3), plus the shared "global"
///        (`GlobalEffectable`) pages they hang off.
///
/// Batch 1 declared the six generated subtrees, batch 2 the sound-editor root. This file declares
/// everything `SoundEditor::setup` can open that is *not* that root and not the settings root
/// (`settings.h`): the kit-drum root, the MIDI/CV root, the MIDI- and gate-drum roots, the audio
/// clip root, the performance-view root, the song root and the kit global-FX root -- and the
/// pages they share, which are the `UnpatchedGlobal` twins of batch 2's patched/sound pages.
///
/// **Why the global pages are new nodes and not the batch-2 ones.** `params::UnpatchedGlobal` and
/// `params::UnpatchedSound` both start at `UNPATCHED_NUM_SHARED`
/// (`modulation/params/param.h:211-236`), so `UNPATCHED_LPF_FREQ` and `UNPATCHED_PORTAMENTO` are
/// the *same integer* in two different numbering spaces -- only `ParamRef::kind` separates them.
/// A page whose params are `GlobalEffectable`'s therefore needs its own nodes, carrying
/// `Kind::UNPATCHED_GLOBAL`. Where a legacy page uses a **shared** param
/// (`param.h:177-208` -- the stutter rate, the EQ four, the sidechain shape, the arp params, the
/// bitcrush and decimation) the id is identical on both sides and this file names batch 2's node
/// rather than declaring a second one, exactly as `sound_actions.h` names `sample0_recorder`.
///
// clang-format off
/// | node | legacy global | legacy class | kind |
/// |---|---|---|---|
/// | `global_level` | `globalLevelMenu` (`:940`) | `UnpatchedParam`, `UNPATCHED_VOLUME`, BAR | Int (param) |
/// | `global_pitch` | `globalPitchMenu` (`:943`) | `UnpatchedParam`, `UNPATCHED_PITCH_ADJUST` | Int (param) |
/// | `global_pan` | `globalPanMenu` (`:946`) | `unpatched_param::Pan`, `UNPATCHED_PAN` | Int (pan scaling) |
/// | `song_master` | `songMasterMenu` (`:948`) / `audioClipMasterMenu` (`:1154`) | `HorizontalMenu` | Submenu (Horizontal) |
/// | `kit_clip_master` | `kitClipMasterMenu` (`:956`) | `HorizontalMenu` | Submenu (Horizontal) |
/// | `global_lpf_freq/res/morph` | `globalLPF*Menu` (`:966,969,972`) | `filter::UnpatchedFilterParam` | Int (param) |
/// | `global_hpf_freq/res/morph` | `globalHPF*Menu` (`:979,982,985`) | `filter::UnpatchedFilterParam` | Int (param) |
/// | `global_lpf` / `global_hpf` | `globalLPFMenu` (`:975`) / `globalHPFMenu` (`:989`) | `HorizontalMenu` | Submenu (Horizontal) |
/// | `global_filters` | `globalFiltersMenu` (`:992`) | `Submenu` | Submenu (Vertical) |
/// | `global_delay_amount` | `globalDelayFeedbackMenu` (`:1016`) | `delay::Amount_Unpatched` | Int (param) |
/// | `global_delay_rate` | `globalDelayRateMenu` (`:1018`) | `UnpatchedParam` | Int (param) |
/// | `global_delay` | `globalDelayMenu` (`:1021`) | `HorizontalMenu` | Submenu (Horizontal) |
/// | `global_reverb_amount` | `globalReverbSendAmountMenu` (`:1034`) | `reverb::Amount_Unpatched` | Int (param) |
/// | `global_reverb` | `globalReverbMenu` (`:1040`) | `HorizontalMenu` | Submenu (Horizontal) |
/// | `global_mod_fx_depth` | `globalModFXDepthMenu` (`:1072`) | `mod_fx::Depth_Unpatched` | Int (param) |
/// | `global_mod_fx_rate` | `globalModFXRateMenu` (`:1074`) | `mod_fx::Rate_Unpatched` | Int (param) |
/// | `global_mod_fx` | `globalModFXMenu` (`:1077`) | `submenu::ModFxHorizontalMenu` | Submenu (Horizontal) |
/// | `global_distortion` | `globalDistortionMenu` (`:1088`) | `HorizontalMenu` | Submenu (Horizontal) |
/// | `global_fx` | `globalFXMenu` (`:1096`) | `Submenu` | Submenu (Vertical) |
/// | `global_sidechain_volume` | `globalSidechainVolumeMenu` (`:1109`) | `sidechain::GlobalVolume` | Int (param, `updates_reverb_params`) |
/// | `global_sidechain` | `globalSidechainMenu` (`:1112`) | `HorizontalMenu` | Submenu (Horizontal) |
// clang-format on
///
/// @note **`globalEQMenu` (`:1005`) and `eqMenu` (`:752`) are the same four children**
///       (`bassMenu`, `trebleMenu`, `bassFreqMenu`, `trebleFreqMenu`, all shared-numbering
///       `UnpatchedParam`s) under the same name, so `global_fx` names batch 2's `eq` rather than
///       declaring an identical second page. `stutterMenu` is literally the same legacy object in
///       both trees, and so is batch 2's `stutter` node.
///
/// @note **The MIDI/CV root carries two titles.** `SoundEditor::setup` rewrites the single
///       `soundEditorRootMenuMIDIOrCV` object's `title` member on the way in --
///       `STRING_FOR_MIDI_INST_MENU_TITLE` for a MIDI output (`sound_editor.cpp:1726`),
///       `STRING_FOR_CV_INSTRUMENT` for a CV one (`:1731`). One `Node::title` is one static id, so
///       the port declares **two nodes over one child list**: `root_midi_or_cv` (the MIDI title)
///       and `root_cv` (the CV one). `midi_or_cv_root(ctx)` picks between them from
///       `MenuContext::output`, which is the same fact `setup()` branched on, and
///       `MenuScene::roots()` holds the MIDI spelling because that is the constructor argument in
///       `menus.cpp`. Both are in `kBatch3Roots`, so both get node ids and both are validated.
///
/// @note **What is a `Kind::Custom` placeholder here, and why.** Every one is an item whose value
///       or whose action lives behind the `scenes-includes-outside` wall -- `gui/` or `hid/` --
///       which no `get`/`set` hook in this layer may reach:
///
///         * `drum_name_edit` -- `EditName` opens `RenameUI` (`gui/menu_item/edit_name.cpp`),
///           like `sound_actions.h`'s two.
///         * `midi_device_linked` -- `Linked::writeCurrentValue` opens
///           `loadMidiDeviceDefinitionUI` (`midi/device_definition/linked.h:26-28`).
///         * `set_clip_length` -- `audioClipView.setClipLengthEqualToSampleLength()`
///           (`audio_clip/set_clip_length_equal_to_sample_length.h:23`).
///         * `audio_source_selector` / `specific_output_selector` -- a context menu and a
///           `drawPixelsForOled`-driven output picker (`audio_clip/audio_source_selector.h:22-27`,
///           `specific_output_source_selector.h:21-45`).
///         * `audio_clip_waveform` -- the marker editor (`audio_clip/sample_marker_editor.h:22`).
///         * `perform_editor` -- `performanceView` (`performance_session_view/editing_mode.h:38-63`).
///         * `active_scales` -- `ActiveScaleMenu` (`gui/menu_item/active_scales.h`), a whole
///           editor of its own.
///         * `configure_song_macros` / `song_midi_learn` -- two context menus
///           (`song/configure_macros.h:22-26`, `song/midi_learn.h:22-26`).
///         * `start_stem_export` -- `soundEditor.exitCompletely()` plus `getRootUI()`
///           (`stem_export/start.h:22-42`).
///
///       They all point at `nodes::kPending`, so `validate_tree` reports them (one
///       `PlaceholderCustom` each) unless the caller opts in, and each counts as **one node**
///       against `kMaxCustomNodes` (fix round 2, I4: the cap counts nodes, not distinct
///       `CustomItem` instances -- counting instances made it 1 for the whole tree and inert).

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/sound_arp.h"
#include "scenes/menu/nodes/sound_bend.h"
#include "scenes/menu/nodes/sound_compressor.h"
#include "scenes/menu/nodes/sound_env.h"
#include "scenes/menu/nodes/sound_filters.h"
#include "scenes/menu/nodes/sound_fx.h"
#include "scenes/menu/nodes/sound_lfo.h"
#include "scenes/menu/nodes/sound_master.h"
#include "scenes/menu/nodes/sound_osc.h"
#include "scenes/menu/nodes/sound_randomizer.h"
#include "scenes/menu/nodes/sound_root.h"
#include "scenes/menu/nodes/sound_sidechain.h"
#include "scenes/menu/nodes/sound_voice.h"
#include "scenes/menu/relevance.h"

#include <span>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

namespace mp = deluge::modulation::params;

/// @name Hooks. Bodies (and their legacy citations) in `roots.cpp`.
/// @{
int32_t get_midi_hide_unlabeled(const MenuContext& ctx);
void set_midi_hide_unlabeled(const MenuContext& ctx, int32_t value);
int32_t get_midi_bank(const MenuContext& ctx);
void set_midi_bank(const MenuContext& ctx, int32_t value);
int32_t get_midi_sub_bank(const MenuContext& ctx);
void set_midi_sub_bank(const MenuContext& ctx, int32_t value);
int32_t get_midi_pgm(const MenuContext& ctx);
void set_midi_pgm(const MenuContext& ctx, int32_t value);
int32_t get_cv2_source(const MenuContext& ctx);
void set_cv2_source(const MenuContext& ctx, int32_t value);
int32_t get_mpe_y_to_mod_wheel(const MenuContext& ctx);
void set_mpe_y_to_mod_wheel(const MenuContext& ctx, int32_t value);
int32_t get_aftertouch_to_mono(const MenuContext& ctx);
void set_aftertouch_to_mono(const MenuContext& ctx, int32_t value);
int32_t get_mpe_to_mono(const MenuContext& ctx);
void set_mpe_to_mono(const MenuContext& ctx, int32_t value);

int32_t get_arp_gate_midi(const MenuContext& ctx);
void set_arp_gate_midi(const MenuContext& ctx, int32_t value);
int32_t get_arp_rate_midi(const MenuContext& ctx);
void set_arp_rate_midi(const MenuContext& ctx, int32_t value);

int32_t get_audio_clip_transpose(const MenuContext& ctx);
void set_audio_clip_transpose(const MenuContext& ctx, int32_t value);
int32_t get_audio_clip_reverse(const MenuContext& ctx);
void set_audio_clip_reverse(const MenuContext& ctx, int32_t value);
int32_t get_audio_clip_interpolation(const MenuContext& ctx);
void set_audio_clip_interpolation(const MenuContext& ctx, int32_t value);
int32_t get_audio_clip_attack(const MenuContext& ctx);
void set_audio_clip_attack(const MenuContext& ctx, int32_t value);

int32_t get_swing_interval(const MenuContext& ctx);
void set_swing_interval(const MenuContext& ctx, int32_t value);
int32_t get_song_threshold_recording(const MenuContext& ctx);
void set_song_threshold_recording(const MenuContext& ctx, int32_t value);

int32_t get_stem_normalization(const MenuContext& ctx);
void set_stem_normalization(const MenuContext& ctx, int32_t value);
int32_t get_stem_normalization_drums(const MenuContext& ctx);
void set_stem_normalization_drums(const MenuContext& ctx, int32_t value);
int32_t get_stem_silence(const MenuContext& ctx);
void set_stem_silence(const MenuContext& ctx, int32_t value);
int32_t get_stem_song_fx(const MenuContext& ctx);
void set_stem_song_fx(const MenuContext& ctx, int32_t value);
int32_t get_stem_kit_fx(const MenuContext& ctx);
void set_stem_kit_fx(const MenuContext& ctx, int32_t value);
int32_t get_stem_offline(const MenuContext& ctx);
void set_stem_offline(const MenuContext& ctx, int32_t value);
int32_t get_stem_mixdown(const MenuContext& ctx);
void set_stem_mixdown(const MenuContext& ctx, int32_t value);
/// @}

using deluge::l10n::String;

// ---------------------------------------------------------------------------------------------
// The shared master pages

inline constexpr Node global_level =
    param(sid(String::STRING_FOR_VOLUME_LEVEL), {mp::Kind::UNPATCHED_GLOBAL, mp::UNPATCHED_VOLUME}, Hint::Bar);
inline constexpr Node global_pitch =
    param(sid(String::STRING_FOR_PITCH), {mp::Kind::UNPATCHED_GLOBAL, mp::UNPATCHED_PITCH_ADJUST});
inline constexpr Node global_pan = param(sid(String::STRING_FOR_PAN), {mp::Kind::UNPATCHED_GLOBAL, mp::UNPATCHED_PAN},
                                         Hint::Pan, nullptr, 0, Scaling::Pan);

/// `songMasterMenu` (`:948`) and `audioClipMasterMenu` (`:1154`): two legacy objects, the same name
/// and the same two children, so one node serves both trees.
inline constexpr const Node* kSongMasterChildren[] = {&global_level, &global_pan};
inline constexpr Node song_master = submenu(sid(String::STRING_FOR_MASTER), kSongMasterChildren, Layout::Horizontal);

inline constexpr const Node* kKitClipMasterChildren[] = {&global_level, &global_pan, &global_pitch};
inline constexpr Node kit_clip_master =
    submenu(sid(String::STRING_FOR_MASTER), kKitClipMasterChildren, Layout::Horizontal);

// ---------------------------------------------------------------------------------------------
// The global filters page
//
// `filter::UnpatchedFilterParam`'s relevance is `FilterInfo::isOn()` for its own slot
// (`filter/param.h:87-89`) -- the same predicate batch 1's patched filter params use.

inline constexpr Node global_lpf_freq =
    param(sid(String::STRING_FOR_FREQUENCY), {mp::Kind::UNPATCHED_GLOBAL, mp::UNPATCHED_LPF_FREQ}, Hint::Bar,
          relevance::has_lpf, sid(String::STRING_FOR_LPF_FREQUENCY));
inline constexpr Node global_lpf_res =
    param(sid(String::STRING_FOR_RESONANCE), {mp::Kind::UNPATCHED_GLOBAL, mp::UNPATCHED_LPF_RES}, Hint::Bar,
          relevance::has_lpf, sid(String::STRING_FOR_LPF_RESONANCE));
inline constexpr Node global_lpf_morph =
    param(sid(String::STRING_FOR_MORPH), {mp::Kind::UNPATCHED_GLOBAL, mp::UNPATCHED_LPF_MORPH}, Hint::Bar,
          relevance::has_lpf, sid(String::STRING_FOR_LPF_MORPH));
inline constexpr const Node* kGlobalLpfChildren[] = {&lpf_mode, &global_lpf_freq, &global_lpf_res, &global_lpf_morph};
inline constexpr Node global_lpf = submenu(sid(String::STRING_FOR_LPF), kGlobalLpfChildren, Layout::Horizontal);

inline constexpr Node global_hpf_freq =
    param(sid(String::STRING_FOR_FREQUENCY), {mp::Kind::UNPATCHED_GLOBAL, mp::UNPATCHED_HPF_FREQ}, Hint::Bar,
          relevance::has_hpf, sid(String::STRING_FOR_HPF_FREQUENCY));
inline constexpr Node global_hpf_res =
    param(sid(String::STRING_FOR_RESONANCE), {mp::Kind::UNPATCHED_GLOBAL, mp::UNPATCHED_HPF_RES}, Hint::Bar,
          relevance::has_hpf, sid(String::STRING_FOR_HPF_RESONANCE));
inline constexpr Node global_hpf_morph =
    param(sid(String::STRING_FOR_MORPH), {mp::Kind::UNPATCHED_GLOBAL, mp::UNPATCHED_HPF_MORPH}, Hint::Bar,
          relevance::has_hpf, sid(String::STRING_FOR_HPF_MORPH));
inline constexpr const Node* kGlobalHpfChildren[] = {&hpf_mode, &global_hpf_freq, &global_hpf_res, &global_hpf_morph};
inline constexpr Node global_hpf = submenu(sid(String::STRING_FOR_HPF), kGlobalHpfChildren, Layout::Horizontal);

inline constexpr const Node* kGlobalFiltersChildren[] = {&global_lpf, &global_hpf, &filter_routing};
inline constexpr Node global_filters = submenu(sid(String::STRING_FOR_FILTERS), kGlobalFiltersChildren);

// ---------------------------------------------------------------------------------------------
// The global FX page

inline constexpr Node global_delay_amount =
    param(sid(String::STRING_FOR_AMOUNT), {mp::Kind::UNPATCHED_GLOBAL, mp::UNPATCHED_DELAY_AMOUNT}, Hint::Bar, nullptr,
          sid(String::STRING_FOR_DELAY_AMOUNT));
inline constexpr Node global_delay_rate =
    param(sid(String::STRING_FOR_RATE), {mp::Kind::UNPATCHED_GLOBAL, mp::UNPATCHED_DELAY_RATE}, Hint::Knob, nullptr,
          sid(String::STRING_FOR_DELAY_RATE));
inline constexpr const Node* kGlobalDelayChildren[] = {&global_delay_amount, &delay_ping_pong, &delay_sync,
                                                       &global_delay_rate, &delay_type};
inline constexpr Node global_delay = submenu(sid(String::STRING_FOR_DELAY), kGlobalDelayChildren, Layout::Horizontal);

// `reverb::Amount_Unpatched` renders a BAR (`reverb/amount.h:23`, the unpatched twin of `Amount`).
inline constexpr Node global_reverb_amount =
    param(sid(String::STRING_FOR_AMOUNT), {mp::Kind::UNPATCHED_GLOBAL, mp::UNPATCHED_REVERB_SEND_AMOUNT}, Hint::Bar,
          nullptr, sid(String::STRING_FOR_REVERB_AMOUNT));
inline constexpr const Node* kGlobalReverbChildren[] = {&global_reverb_amount, &reverb_room_size, &reverb_damping,
                                                        &reverb_width,         &reverb_model,     &reverb_pan,
                                                        &reverb_hpf,           &reverb_lpf,       &reverb_sidechain};
inline constexpr Node global_reverb =
    submenu(sid(String::STRING_FOR_REVERB), kGlobalReverbChildren, Layout::Horizontal);

inline constexpr Node global_mod_fx_depth =
    param(sid(String::STRING_FOR_DEPTH), {mp::Kind::UNPATCHED_GLOBAL, mp::UNPATCHED_MOD_FX_DEPTH}, Hint::Bar,
          relevance::mod_fx_has_depth, sid(String::STRING_FOR_MOD_FX_DEPTH));
inline constexpr Node global_mod_fx_rate =
    param(sid(String::STRING_FOR_RATE), {mp::Kind::UNPATCHED_GLOBAL, mp::UNPATCHED_MOD_FX_RATE}, Hint::Knob,
          relevance::mod_fx_on, sid(String::STRING_FOR_MOD_FX_RATE));
inline constexpr const Node* kGlobalModFxChildren[] = {&mod_fx_type, &global_mod_fx_depth, &global_mod_fx_rate,
                                                       &mod_fx_feedback, &mod_fx_offset};
inline constexpr Node global_mod_fx = submenu(sid(String::STRING_FOR_MOD_FX), kGlobalModFxChildren, Layout::Horizontal);

inline constexpr const Node* kGlobalDistortionChildren[] = {&decimation, &bitcrush};
inline constexpr Node global_distortion =
    submenu(sid(String::STRING_FOR_DISTORTION), kGlobalDistortionChildren, Layout::Horizontal);

inline constexpr const Node* kGlobalFxChildren[] = {&eq,      &global_delay,  &global_reverb,
                                                    &stutter, &global_mod_fx, &global_distortion};
inline constexpr Node global_fx = submenu(sid(String::STRING_FOR_FX), kGlobalFxChildren);

// ---------------------------------------------------------------------------------------------
// The global sidechain page
//
// `sidechain::GlobalVolume` is the second `unpatched_param::UpdatingReverbParams` in the whole tree
// (`sidechain/volume_global.h:6`; the first is batch 2's `sidechain_shape`), so it carries
// `with_reverb_update` for exactly the reason `Binding::updates_reverb_params` documents: a `set`
// hook would replace the param write rather than follow it. Its rendering style is
// SIDECHAIN_DUCKING (`volume_global.h:10`), i.e. `Hint::Ducking`.

inline constexpr Node global_sidechain_volume =
    with_reverb_update(param(sid(String::STRING_FOR_VOLUME_DUCKING),
                             {mp::Kind::UNPATCHED_GLOBAL, mp::UNPATCHED_SIDECHAIN_VOLUME}, Hint::Ducking));
inline constexpr const Node* kGlobalSidechainChildren[] = {&global_sidechain_volume, &sidechain_sync, &sidechain_shape,
                                                           &sidechain_attack, &sidechain_release};
inline constexpr Node global_sidechain =
    submenu(sid(String::STRING_FOR_SIDECHAIN), kGlobalSidechainChildren, Layout::Horizontal);

// ---------------------------------------------------------------------------------------------
// The kit-drum root (`soundEditorRootMenuDrum`, `:1805`)
//
// Identical to `sound_root` but for its first child: a kit row's actions page renames the *drum*
// (`drumNameEditMenu`, `:1126`) where a synth's renames the track and the clip.

inline constexpr Node drum_name_edit = custom(sid(String::STRING_FOR_RENAME_DRUM), &kPending); // E2

inline constexpr const Node* kDrumActionsChildren[] = {&drum_name_edit, &sample0_recorder, &sample1_recorder};
inline constexpr Node drum_actions = submenu(sid(String::STRING_FOR_ACTIONS), kDrumActionsChildren);

inline constexpr const Node* kDrumRootChildren[] = {&drum_actions, &sound_master,
                                                    &arp,          &randomizer,
                                                    &audio_comp,   &sound_filters,
                                                    &sound_fx,     &sidechain,
                                                    &source0,      &source1,
                                                    &modulator0,   &modulator1,
                                                    &osc_mixer,    &env1,
                                                    &env2,         &env3,
                                                    &env4,         &lfo1,
                                                    &lfo2,         &lfo3,
                                                    &lfo4,         &voice,
                                                    &bend,         &drum_bend_range,
                                                    &patch_cables, &sequence_direction,
                                                    &output_midi};
inline constexpr Node root_drum = submenu(sid(String::STRING_FOR_SOUND), kDrumRootChildren);

// ---------------------------------------------------------------------------------------------
// The MIDI / CV root (`soundEditorRootMenuMIDIOrCV`, `:1950`)

inline constexpr const Node* kMidiOrCvActionsChildren[] = {&edit_name};
inline constexpr Node midi_or_cv_actions = submenu(sid(String::STRING_FOR_ACTIONS), kMidiOrCvActionsChildren);

inline constexpr Node midi_device_linked =
    with_relevant(custom(sid(String::STRING_FOR_MIDI_DEVICE_DEFINITION_LINKED), &kPending), // E2
                  relevance::is_midi_out);
inline constexpr Node midi_hide_unlabeled =
    toggle_item(sid(String::STRING_FOR_MIDI_DEVICE_DEFINITION_HIDE), get_midi_hide_unlabeled, set_midi_hide_unlabeled,
                relevance::is_midi_out);
inline constexpr const Node* kMidiDeviceDefinitionChildren[] = {&midi_device_linked, &midi_hide_unlabeled};
inline constexpr Node midi_device_definition =
    submenu(sid(String::STRING_FOR_MIDI_DEVICE_DEFINITION), kMidiDeviceDefinitionChildren, Layout::Vertical,
            relevance::is_midi_out);

// `midi::Preset::selectEncoderAction` wraps at 129 and draws 128 as "NONE" (`midi/preset.h:34-49`),
// so the editable range is 0..128 and `getMaxValue()` says 128 (`:22`).
inline constexpr IntRange kMidiPresetRange{0, 128, 1};
inline constexpr Node midi_bank = with_title(int_item(sid(String::STRING_FOR_BANK), get_midi_bank, set_midi_bank,
                                                      kMidiPresetRange, Hint::None, relevance::is_midi_out),
                                             sid(String::STRING_FOR_MIDI_BANK));
inline constexpr Node midi_sub_bank =
    with_title(int_item(sid(String::STRING_FOR_SUB_BANK_SHORT), get_midi_sub_bank, set_midi_sub_bank, kMidiPresetRange,
                        Hint::None, relevance::is_midi_out),
               sid(String::STRING_FOR_MIDI_SUB_BANK));
inline constexpr Node midi_pgm = with_title(int_item(sid(String::STRING_FOR_PGM), get_midi_pgm, set_midi_pgm,
                                                     kMidiPresetRange, Hint::None, relevance::is_midi_out),
                                            sid(String::STRING_FOR_MIDI_PGM_NUMB_MENU_TITLE));
inline constexpr const Node* kMidiProgramChildren[] = {&midi_bank, &midi_sub_bank, &midi_pgm};
/// `midiProgramMenu` (`:1933`) is a `HorizontalMenu` whose name *is* its title
/// (`STRING_FOR_MIDI_PROGRAM_MENU_TITLE`, the only string its constructor is given), FIXED layout
/// with two slots -- and `Layout::FIXED` has no `Node::Layout` spelling (`sound_fx.h`'s note).
inline constexpr Node midi_program =
    submenu(sid(String::STRING_FOR_MIDI_PROGRAM_MENU_TITLE), kMidiProgramChildren, Layout::Horizontal);

// The MIDI/CV arpeggiator: `arpMenuMIDIOrCV` (`:670`) differs from `arpMenu` only in its basic
// page, whose gate and rate live on `ArpeggiatorSettings` rather than in a param collection
// (`arpeggiator/midi_cv/gate.h:22-30`, `rate.h:22-30`).
inline constexpr Node arp_gate_midi =
    with_title(int_item(sid(String::STRING_FOR_GATE), get_arp_gate_midi, set_arp_gate_midi,
                        IntRange{kMinMenuValue, kMaxMenuValue, 1}, Hint::LengthSlider, relevance::arp_non_sound),
               sid(String::STRING_FOR_ARP_GATE_MENU_TITLE));
inline constexpr Node arp_rate_midi =
    with_title(int_item(sid(String::STRING_FOR_RATE), get_arp_rate_midi, set_arp_rate_midi,
                        IntRange{kMinMenuValue, kMaxMenuValue, 1}, Hint::Knob, relevance::arp_non_sound),
               sid(String::STRING_FOR_ARP_RATE_MENU_TITLE));
inline constexpr const Node* kArpBasicMidiChildren[] = {&arp_preset, &arp_gate_midi, &arp_sync, &arp_rate_midi};
inline constexpr Node arp_basic_midi = submenu(sid(String::STRING_FOR_BASIC), kArpBasicMidiChildren, Layout::Horizontal,
                                               nullptr, sid(String::STRING_FOR_ARP_BASIC_MENU_TITLE));
inline constexpr const Node* kArpMidiChildren[] = {&arp_mode, &arp_basic_midi, &arp_pattern, &arp_mpe,
                                                   &arp_include_in_kit_arp};
inline constexpr Node arp_midi = submenu(sid(String::STRING_FOR_ARPEGGIATOR), kArpMidiChildren);

/// `cv::DualCVSelection::getOptions` (`cv/cv2Mapping.h:31-41`).
inline constexpr uint16_t kCv2SourceNames[] = {
    sid(String::STRING_FOR_OFF),
    sid(String::STRING_FOR_PATCH_SOURCE_Y),
    sid(String::STRING_FOR_PATCH_SOURCE_AFTERTOUCH),
    sid(String::STRING_FOR_VELOCITY),
};
inline constexpr Node cv2_source = enumeration(sid(String::STRING_FOR_CV2_SOURCE), kCv2SourceNames, get_cv2_source,
                                               set_cv2_source, relevance::cv2_dual);

inline constexpr Node mpe_y_to_mod_wheel = toggle_item(
    sid(String::STRING_FOR_Y_AXIS_CONVERSION), get_mpe_y_to_mod_wheel, set_mpe_y_to_mod_wheel, relevance::is_midi_out);
inline constexpr Node midi_aftertouch_collapse =
    toggle_item(sid(String::STRING_FOR_PATCH_SOURCE_AFTERTOUCH), get_aftertouch_to_mono, set_aftertouch_to_mono);
inline constexpr Node midi_mpe_collapse = toggle_item(sid(String::STRING_FOR_MPE), get_mpe_to_mono, set_mpe_to_mono);
inline constexpr const Node* kMidiMpeChildren[] = {&midi_aftertouch_collapse, &midi_mpe_collapse};
inline constexpr Node midi_mpe =
    submenu(sid(String::STRING_FOR_MPE_MONO), kMidiMpeChildren, Layout::Vertical, relevance::is_midi_out);

inline constexpr const Node* kMidiOrCvRootChildren[] = {
    &midi_or_cv_actions, &midi_device_definition, &midi_program, &arp_midi,          &randomizer, &bend,
    &cv2_source,         &mpe_y_to_mod_wheel,     &midi_mpe,     &sequence_direction};
inline constexpr Node root_midi_or_cv = submenu(sid(String::STRING_FOR_MIDI_INST_MENU_TITLE), kMidiOrCvRootChildren);
/// The CV spelling of the very same root -- `sound_editor.cpp:1731`'s retitle, as a second node.
inline constexpr Node root_cv = submenu(sid(String::STRING_FOR_CV_INSTRUMENT), kMidiOrCvRootChildren);

/// @brief Which of the two MIDI/CV root nodes @p ctx wants -- the port of `setup()`'s retitle.
const Node& midi_or_cv_root(const MenuContext& ctx);

// ---------------------------------------------------------------------------------------------
// The MIDI-drum and gate-drum roots (`:1967`, `:1975`)

inline constexpr const Node* kNonAudioDrumRootChildren[] = {&drum_actions, &arp_midi, &randomizer};
inline constexpr Node root_midi_drum = submenu(sid(String::STRING_FOR_MIDI), kNonAudioDrumRootChildren);
inline constexpr Node root_gate_drum = submenu(sid(String::STRING_FOR_GATE), kNonAudioDrumRootChildren);

// ---------------------------------------------------------------------------------------------
// The audio-clip root (`soundEditorRootMenuAudioClip`, `:1985`)

inline constexpr Node set_clip_length =
    custom(sid(String::STRING_FOR_SET_CLIP_LENGTH_EQUAL_TO_SAMPLE_LENGTH), &kPending); // E2
inline constexpr const Node* kAudioClipActionsChildren[] = {&edit_name, &set_clip_length};
inline constexpr Node audio_clip_actions = submenu(sid(String::STRING_FOR_ACTIONS), kAudioClipActionsChildren);

inline constexpr Node audio_source_selector = custom(sid(String::STRING_FOR_AUDIO_SOURCE), &kPending); // E2
inline constexpr Node specific_output_selector =
    custom(sid(String::STRING_FOR_TRACK), &kPending, relevance::audio_source_is_specific);       // E2
inline constexpr Node audio_clip_waveform = custom(sid(String::STRING_FOR_WAVEFORM), &kPending); // E2

inline constexpr const Node* kAudioClipDistortionChildren[] = {&saturation, &bitcrush, &decimation};
inline constexpr Node audio_clip_distortion =
    submenu(sid(String::STRING_FOR_DISTORTION), kAudioClipDistortionChildren, Layout::Horizontal);

inline constexpr const Node* kAudioClipFxChildren[] = {&eq,      &global_delay,  &global_reverb,
                                                       &stutter, &global_mod_fx, &audio_clip_distortion};
inline constexpr Node audio_clip_fx = submenu(sid(String::STRING_FOR_FX), kAudioClipFxChildren);

// `audio_clip::Transpose` (`audio_clip/transpose.h:20-46`): a `Decimal` over the sample holder's
// transpose+cents pair, with a learn-only `ParamRef` on `UNPATCHED_PITCH_ADJUST` (`:41-45`) --
// exactly the shape `with_learn_param` exists for (`node.h`'s `Binding` precedence note).
inline constexpr Node audio_clip_transpose =
    with_learn_param(decimal_item(sid(String::STRING_FOR_TRANSPOSE), get_audio_clip_transpose, set_audio_clip_transpose,
                                  IntRange{-9600, 9600, 1}, 2),
                     {mp::Kind::UNPATCHED_GLOBAL, mp::UNPATCHED_PITCH_ADJUST});
inline constexpr Node audio_clip_reverse =
    toggle_item(sid(String::STRING_FOR_REVERSE), get_audio_clip_reverse, set_audio_clip_reverse);

// `AudioInterpolation::getOptions` (`audio_interpolation.h:25-28`) is LINEAR / SINC -- the very
// list batch 1 already declares for the two sample interpolation items (`sound_osc.h`'s
// `kInterpolationNames`), so this names it rather than declaring a duplicate.
inline constexpr Node audio_clip_interpolation =
    with_title(enumeration(sid(String::STRING_FOR_INTERPOLATION), kInterpolationNames, get_audio_clip_interpolation,
                           set_audio_clip_interpolation),
               sid(String::STRING_FOR_AUDIO_INTERPOLATION));

inline constexpr const Node* kAudioClipSampleChildren[] = {&file0_selector,      &audio_clip_transpose,
                                                           &audio_clip_reverse,  &sample0_pitch_speed,
                                                           &audio_clip_waveform, &audio_clip_interpolation};
inline constexpr Node audio_clip_sample =
    submenu(sid(String::STRING_FOR_SAMPLE), kAudioClipSampleChildren, Layout::Horizontal);

inline constexpr Node audio_clip_attack = int_item(sid(String::STRING_FOR_ATTACK), get_audio_clip_attack,
                                                   set_audio_clip_attack, IntRange{kMinMenuValue, kMaxMenuValue, 1});

inline constexpr const Node* kAudioClipRootChildren[] = {
    &audio_clip_actions, &audio_source_selector, &specific_output_selector, &song_master,       &audio_comp,
    &global_filters,     &audio_clip_fx,         &global_sidechain,         &audio_clip_sample, &audio_clip_attack,
    &voice_priority};
inline constexpr Node root_audio_clip = submenu(sid(String::STRING_FOR_AUDIO_CLIP), kAudioClipRootChildren);

// ---------------------------------------------------------------------------------------------
// The performance-view root (`soundEditorRootMenuPerformanceView`, `:2006`)

inline constexpr Node perform_editor = custom(sid(String::STRING_FOR_PERFORM_EDITOR), &kPending); // E2

inline constexpr const Node* kPerformanceRootChildren[] = {&perform_editor, &global_filters, &global_fx};
inline constexpr Node root_performance = submenu(sid(String::STRING_FOR_PERFORM_FX), kPerformanceRootChildren);

// ---------------------------------------------------------------------------------------------
// Stem export (`:2016-2067`), shared by the song and kit-global-FX roots

inline constexpr Node start_stem_export = custom(sid(String::STRING_FOR_START_EXPORT), &kPending); // E2
inline constexpr Node stem_normalization = toggle_item(sid(String::STRING_FOR_CONFIGURE_EXPORT_STEMS_NORMALIZATION),
                                                       get_stem_normalization, set_stem_normalization);
inline constexpr Node stem_normalization_drums =
    toggle_item(sid(String::STRING_FOR_CONFIGURE_EXPORT_STEMS_NORMALIZATION), get_stem_normalization_drums,
                set_stem_normalization_drums);
inline constexpr Node stem_silence =
    toggle_item(sid(String::STRING_FOR_CONFIGURE_EXPORT_STEMS_SILENCE), get_stem_silence, set_stem_silence);
inline constexpr Node stem_song_fx =
    toggle_item(sid(String::STRING_FOR_CONFIGURE_EXPORT_STEMS_SONGFX), get_stem_song_fx, set_stem_song_fx);
inline constexpr Node stem_kit_fx =
    toggle_item(sid(String::STRING_FOR_CONFIGURE_EXPORT_STEMS_KITFX), get_stem_kit_fx, set_stem_kit_fx);
inline constexpr Node stem_offline =
    toggle_item(sid(String::STRING_FOR_CONFIGURE_EXPORT_STEMS_OFFLINE_RENDERING), get_stem_offline, set_stem_offline);
inline constexpr Node stem_mixdown =
    toggle_item(sid(String::STRING_FOR_CONFIGURE_EXPORT_STEMS_MIXDOWN), get_stem_mixdown, set_stem_mixdown);

inline constexpr const Node* kConfigureStemExportChildren[] = {&stem_normalization, &stem_silence, &stem_song_fx,
                                                               &stem_offline, &stem_mixdown};
inline constexpr Node configure_stem_export =
    submenu(sid(String::STRING_FOR_CONFIGURE_EXPORT_STEMS), kConfigureStemExportChildren);
inline constexpr const Node* kStemExportChildren[] = {&start_stem_export, &configure_stem_export};
inline constexpr Node stem_export = submenu(sid(String::STRING_FOR_EXPORT_AUDIO), kStemExportChildren);

inline constexpr const Node* kKitFxConfigureStemExportChildren[] = {&stem_kit_fx, &stem_normalization_drums,
                                                                    &stem_silence, &stem_song_fx, &stem_offline};
inline constexpr Node kit_fx_configure_stem_export =
    submenu(sid(String::STRING_FOR_CONFIGURE_EXPORT_STEMS), kKitFxConfigureStemExportChildren);
inline constexpr const Node* kKitFxStemExportChildren[] = {&start_stem_export, &kit_fx_configure_stem_export};
inline constexpr Node kit_fx_stem_export = submenu(sid(String::STRING_FOR_EXPORT_AUDIO), kKitFxStemExportChildren);

// ---------------------------------------------------------------------------------------------
// The song root (`soundEditorRootMenuSongView`, `:2083`)

/// `swing::Interval::size()` is `NUM_SWING_INTERVALS` (`swing/interval.h:26`) rather than
/// `NUM_SYNC_VALUES`, the same kind of override `sidechain::Sync` makes.
inline constexpr IntRange kSwingIntervalRange{0, NUM_SWING_INTERVALS - 1, 1};
inline constexpr Node swing_interval = sync_level_item(sid(String::STRING_FOR_SWING_INTERVAL), 0, get_swing_interval,
                                                       set_swing_interval, kSwingIntervalRange);

inline constexpr Node active_scales = custom(sid(String::STRING_FOR_ACTIVE_SCALES), &kPending);                 // E2
inline constexpr Node configure_song_macros = custom(sid(String::STRING_FOR_CONFIGURE_SONG_MACROS), &kPending); // E2
inline constexpr Node song_midi_learn = custom(sid(String::STRING_FOR_MIDI_LEARN), &kPending);                  // E2

/// `record::ThresholdMode::getOptions` (`record/threshold_mode.h:44-54`), shared with the settings
/// root's `defaultThresholdRecordingModeMenu`.
inline constexpr uint16_t kThresholdModeNames[] = {
    sid(String::STRING_FOR_DISABLED),
    sid(String::STRING_FOR_LOW),
    sid(String::STRING_FOR_MEDIUM),
    sid(String::STRING_FOR_HIGH),
};
inline constexpr Node song_threshold_recording_mode = enumeration(
    sid(String::STRING_FOR_MODE), kThresholdModeNames, get_song_threshold_recording, set_song_threshold_recording);
inline constexpr const Node* kSongThresholdRecordingChildren[] = {&song_threshold_recording_mode};
inline constexpr Node song_threshold_recording =
    submenu(sid(String::STRING_FOR_THRESHOLD_RECORDING), kSongThresholdRecordingChildren);

inline constexpr const Node* kSongRootChildren[] = {&song_master,           &global_filters,  &global_fx,
                                                    &swing_interval,        &active_scales,   &song_threshold_recording,
                                                    &configure_song_macros, &song_midi_learn, &stem_export};
inline constexpr Node root_song = submenu(sid(String::STRING_FOR_SONG), kSongRootChildren);

// ---------------------------------------------------------------------------------------------
// The kit global-FX root (`soundEditorRootMenuKitGlobalFX`, `:2136`)

inline constexpr const Node* kKitFxActionsChildren[] = {&edit_name, &kit_fx_stem_export};
inline constexpr Node kit_fx_actions = submenu(sid(String::STRING_FOR_ACTIONS), kKitFxActionsChildren);

// `arpeggiator::KitRate` is the kit's own `UNPATCHED_ARP_RATE`, shown only on the kit's global-FX
// page (`arpeggiator/rate.h:19-26`); `arpGateMenu` is shared with the sound arpeggiator.
inline constexpr Node arp_kit_rate =
    param(sid(String::STRING_FOR_RATE), {mp::Kind::UNPATCHED_GLOBAL, mp::UNPATCHED_ARP_RATE}, Hint::Knob,
          relevance::is_kit_global_fx, sid(String::STRING_FOR_ARP_RATE_MENU_TITLE));
inline constexpr const Node* kArpBasicKitChildren[] = {&arp_preset, &arp_gate, &arp_sync, &arp_kit_rate};
inline constexpr Node arp_basic_kit = submenu(sid(String::STRING_FOR_BASIC), kArpBasicKitChildren, Layout::Horizontal,
                                              nullptr, sid(String::STRING_FOR_ARP_BASIC_MENU_TITLE));
inline constexpr const Node* kKitArpChildren[] = {&arp_mode, &arp_basic_kit, &arp_pattern, &randomizer};
inline constexpr Node kit_arp = submenu(sid(String::STRING_FOR_KIT_ARPEGGIATOR), kKitArpChildren);

inline constexpr const Node* kKitGlobalFxRootChildren[] = {
    &kit_fx_actions, &kit_clip_master, &kit_arp, &audio_comp, &global_filters, &global_fx, &global_sidechain};
inline constexpr Node root_kit_global_fx = submenu(sid(String::STRING_FOR_KIT_GLOBAL_FX), kKitGlobalFxRootChildren);

// ---------------------------------------------------------------------------------------------

} // namespace deluge::scenes::menu::nodes
