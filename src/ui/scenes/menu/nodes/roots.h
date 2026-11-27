#pragma once

/// @brief The nine non-settings roots as data, plus the shared "global"
///        (host chain, `UNPATCHED_GLOBAL`) pages they hang off.
///
/// The generated subtrees and the sound-editor root are declared elsewhere. This file declares
/// everything `SoundEditor::setup` can open that is *not* that root and not the settings root
/// (`nodes/settings.h`): the kit-drum root, the MIDI/CV root, the MIDI- and gate-drum roots, the audio
/// clip root, the performance-view root, the song root and the kit global-FX root -- and the
/// pages they share, which are the `UnpatchedGlobal` twins of the sound-editor root's patched/sound pages.
///
/// **Why the global pages are new nodes and not the sound-editor root's.** A global effectable's and a sound's
/// unpatched params both start after the shared ones, so the global filter frequency and the sound's portamento are the
/// *same integer* in two different numbering spaces -- only `ParamRef::kind` separates them. A page whose params are a
/// host chain's therefore needs its own nodes, carrying `Kind::UNPATCHED_GLOBAL`. Where a legacy page uses a **shared**
/// param (the ids both `instruments::classic::param` and `host::chains::param` name alike -- the stutter rate, the EQ
/// four, the compressor shape, the arp params, the bitcrush and decimation) the id is identical on both sides and this
/// file names the sound-editor root's node rather than declaring a second one, exactly as `sound_actions.h` names
/// `sample0_recorder`.
///
/// Each legacy global below is a variable of `origin/main:src/deluge/gui/ui/menus.cpp`.
///
// clang-format off
/// | node | legacy global | legacy class | kind |
/// |---|---|---|---|
/// | `global_level` | `globalLevelMenu` | `UnpatchedParam`, `UNPATCHED_VOLUME`, BAR | Int (param) |
/// | `global_pitch` | `globalPitchMenu` | `UnpatchedParam`, `UNPATCHED_PITCH_ADJUST` | Int (param) |
/// | `global_pan` | `globalPanMenu` | `unpatched_param::Pan`, `UNPATCHED_PAN` | Int (pan scaling) |
/// | `song_master` | `songMasterMenu` / `audioClipMasterMenu` | `HorizontalMenu` | Submenu (Horizontal) |
/// | `kit_clip_master` | `kitClipMasterMenu` | `HorizontalMenu` | Submenu (Horizontal) |
/// | `global_lpf_freq/res/morph` | `globalLPF*Menu` | `filter::UnpatchedFilterParam` | Int (param) |
/// | `global_hpf_freq/res/morph` | `globalHPF*Menu` | `filter::UnpatchedFilterParam` | Int (param) |
/// | `global_lpf` / `global_hpf` | `globalLPFMenu` / `globalHPFMenu` | `HorizontalMenu` | Submenu (Horizontal) |
/// | `global_filters` | `globalFiltersMenu` | `Submenu` | Submenu (Vertical) |
/// | `global_delay_amount` | `globalDelayFeedbackMenu` | `delay::Amount_Unpatched` | Int (param) |
/// | `global_delay_rate` | `globalDelayRateMenu` | `UnpatchedParam` | Int (param) |
/// | `global_delay` | `globalDelayMenu` | `HorizontalMenu` | Submenu (Horizontal) |
/// | `global_reverb_amount` | `globalReverbSendAmountMenu` | `reverb::Amount_Unpatched` | Int (param) |
/// | `global_reverb` | `globalReverbMenu` | `HorizontalMenu` | Submenu (Horizontal) |
/// | `global_mod_fx_depth` | `globalModFXDepthMenu` | `mod_fx::Depth_Unpatched` | Int (param) |
/// | `global_mod_fx_rate` | `globalModFXRateMenu` | `mod_fx::Rate_Unpatched` | Int (param) |
/// | `global_mod_fx` | `globalModFXMenu` | `submenu::ModFxHorizontalMenu` | Submenu (Horizontal) |
/// | `global_distortion` | `globalDistortionMenu` | `HorizontalMenu` | Submenu (Horizontal) |
/// | `global_fx` | `globalFXMenu` | `Submenu` | Submenu (Vertical) |
/// | `global_sidechain_volume` | `globalSidechainVolumeMenu` | `sidechain::GlobalVolume` | Int (param; its Edit raises the reverb's recompute) |
/// | `global_sidechain` | `globalSidechainMenu` | `HorizontalMenu` | Submenu (Horizontal) |
// clang-format on
///
/// @note **`globalEQMenu` and `eqMenu` are the same four children**
///       (`bassMenu`, `trebleMenu`, `bassFreqMenu`, `trebleFreqMenu`, all shared-numbering
///       `UnpatchedParam`s) under the same name, so `global_fx` names the sound-editor root's `eq` rather than
///       declaring an identical second page. `stutterMenu` is literally the same legacy object in
///       both trees, and so is the sound-editor root's `stutter` node.
///
/// @note **The MIDI/CV root carries two titles.** `SoundEditor::setup` rewrites the single
///       `soundEditorRootMenuMIDIOrCV` object's `title` member on the way in -- `STRING_FOR_MIDI_INST_MENU_TITLE` for a
///       MIDI output, `STRING_FOR_CV_INSTRUMENT` for a CV one. One `Node::title` is one static id, so the port declares
///       **two nodes over one child list**: `root_midi_or_cv` (the MIDI title) and `root_cv` (the CV one).
///       `midi_or_cv_root(ctx)` picks between them from the instrument kind in `MenuContext::addresses`, which is the
///       same fact `setup()` branched on, and `MenuScene::roots()` holds the MIDI spelling because that is the
///       constructor argument in `origin/main:src/deluge/gui/ui/menus.cpp`. Both are in `kAllRoots`, so both get
///       node ids and both are validated.
///
/// @note **No item here is a `Kind::Custom` placeholder.** Each item whose value or whose action lives behind
///       the `scenes-includes-outside` wall -- `gui/` or `hid/`, which no `get`/`set` hook in this layer may
///       reach -- is a real node item:
///
///         * `drum_name_edit` is a `nodes::EditNameItem` (`nodes/edit_name.h`) that opens `rename::RenameScene` on
///           `rename::TargetId::Drum` -- legacy's `EditName::Target::DRUM` -> `renameDrumUI` (`EditName::beginSession`
///           opens `RenameUI`, `origin/main:src/deluge/gui/menu_item/edit_name.cpp`) -- exactly as `sound_actions.h`'s
///           two do for the clip and the track.
///         * `midi_device_linked` is a `nodes::MidiDeviceLinkedItem` (`nodes/output_items.h`) that ports the whole
///           `Toggle`: the ON arm opens `browser::BrowserScene` on `BrowserId::MidiDeviceDefinition`
///           (the `if` arm of `Linked::writeCurrentValue` in
///           `origin/main:src/deluge/gui/menu_item/midi/device_definition/linked.h`) and the OFF arm clears the linked
///           file name (its `else` arm).
///         * `set_clip_length`, `start_stem_export`, `song_midi_learn` and `configure_song_macros` are
///           `nodes::ActionItem`s that file a `MenuAction` through `MenuWalkState::pending_action`, the verb
///           counterpart of `pending_scene` -- see `ui/scenes/menu/action.h`. The action is *filed*, not performed:
///           performing it is the wiring's job.
///         * `audio_source_selector` is a `nodes::ContextMenuItem` on `context_menu::DescriptorId::AudioSource`.
///         * `specific_output_selector` is a `nodes::SpecificOutputItem` (`nodes/output_items.h`): the song's
///           output list walked with the select encoder, skipping every output the audio track cannot record
///           from (`origin/main:src/deluge/gui/menu_item/audio_clip/specific_output_source_selector.h`).
///         * `audio_clip_waveform` is the marker editor (`SampleMarkerEditor` in
///         `origin/main:src/deluge/gui/menu_item/audio_clip/sample_marker_editor.h`).
///         * `perform_editor` is a `nodes::PerformEditorItem` that renders the three-option list and files
///           `MenuAction::OpenPerformanceView`; the mode it shows is the host's
///           `bind::UiFacts::performance_editing_mode()`.
///         * `active_scales` is a `nodes::ActiveScalesItem` (`nodes/scales.h`): one row per preset scale with a
///           checkbox, over `ui::widgets::oled::list`, which already ports the renderer legacy's own
///           `drawSubmenuItemsForOled` was adapted from.
///
///       No node anywhere in the tree points at `nodes::kPending`, so `validate_tree` reports no
///       `PlaceholderCustom` at all (`validate.h`'s `kPendingCustomNodes` is 0).
///       Every `Kind::Custom` node counts as **one node** against `kMaxCustomNodes` (the cap counts nodes, not
///       distinct `CustomItem` instances).

#include "arp/params.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/actions.h" // the five action items
#include "ui/scenes/menu/nodes/column_hooks.h"
#include "ui/scenes/menu/nodes/context_menu_items.h" // kAudioSourceItem
#include "ui/scenes/menu/nodes/detail.h"
#include "ui/scenes/menu/nodes/edit_name.h"    // kEditDrumName
#include "ui/scenes/menu/nodes/output_items.h" // kMidiDeviceLinked, kSpecificOutputSelector
#include "ui/scenes/menu/nodes/param_shows.h"
#include "ui/scenes/menu/nodes/sample.h" // kAudioClipWaveform
#include "ui/scenes/menu/nodes/scales.h" // kSongActiveScalesItem
#include "ui/scenes/menu/nodes/sound_arp.h"
#include "ui/scenes/menu/nodes/sound_bend.h"
#include "ui/scenes/menu/nodes/sound_compressor.h"
#include "ui/scenes/menu/nodes/sound_env.h"
#include "ui/scenes/menu/nodes/sound_filters.h"
#include "ui/scenes/menu/nodes/sound_fx.h"
#include "ui/scenes/menu/nodes/sound_lfo.h"
#include "ui/scenes/menu/nodes/sound_master.h"
#include "ui/scenes/menu/nodes/sound_osc.h"
#include "ui/scenes/menu/nodes/sound_randomizer.h"
#include "ui/scenes/menu/nodes/sound_root.h"
#include "ui/scenes/menu/nodes/sound_sidechain.h"
#include "ui/scenes/menu/nodes/sound_voice.h"
#include "ui/scenes/menu/relevance.h"

#include "plugin/types/display_mapping.h"
#include "project/device_settings.h"

#include <span>

namespace deluge::ui::scenes::menu {
struct MenuContext;
} // namespace deluge::ui::scenes::menu

namespace deluge::ui::scenes::menu::nodes {

int32_t get_swing_interval(const MenuContext& ctx);
void set_swing_interval(const MenuContext& ctx, int32_t value);
int32_t get_song_threshold_recording(const MenuContext& ctx);
void set_song_threshold_recording(const MenuContext& ctx, int32_t value);

using deluge::l10n::String;

// ---------------------------------------------------------------------------------------------
// The shared master pages

inline constexpr Node global_level = param(sid(String::STRING_FOR_VOLUME_LEVEL),
                                           {deluge::plugin::ParamKind::UNPATCHED_GLOBAL,
                                            deluge::host::chains::runtime_index(deluge::host::chains::param::kVolume)},
                                           Hint::Bar);
inline constexpr Node global_pitch = param(
    sid(String::STRING_FOR_PITCH), {deluge::plugin::ParamKind::UNPATCHED_GLOBAL,
                                    deluge::host::chains::runtime_index(deluge::host::chains::param::kPitchAdjust)});
inline constexpr Node global_pan = param(sid(String::STRING_FOR_PAN),
                                         {deluge::plugin::ParamKind::UNPATCHED_GLOBAL,
                                          deluge::host::chains::runtime_index(deluge::host::chains::param::kPan)},
                                         Hint::Pan, nullptr, 0, Scaling::Pan);

/// `songMasterMenu` and `audioClipMasterMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`): two legacy objects, the same
/// name and the same two children, so one node serves both trees.
inline constexpr const Node* kSongMasterChildren[] = {&global_level, &global_pan};
inline constexpr Node song_master = submenu(sid(String::STRING_FOR_MASTER), kSongMasterChildren, Layout::Horizontal);

inline constexpr const Node* kKitClipMasterChildren[] = {&global_level, &global_pan, &global_pitch};
inline constexpr Node kit_clip_master =
    submenu(sid(String::STRING_FOR_MASTER), kKitClipMasterChildren, Layout::Horizontal);

// ---------------------------------------------------------------------------------------------
// The global filters page
//
// `filter::UnpatchedFilterParam`'s relevance is `FilterInfo::isOn()` for its own slot
// (`UnpatchedFilterParam::isRelevant` in `origin/main:src/deluge/gui/menu_item/filter/param.h`) -- the same predicate
// the patched filter params use.

inline constexpr Node global_lpf_freq =
    with_column_draw(param(sid(String::STRING_FOR_FREQUENCY),
                           {deluge::plugin::ParamKind::UNPATCHED_GLOBAL,
                            deluge::host::chains::runtime_index(deluge::host::chains::param::kLpfFrequency)},
                           Hint::Bar, param_shows::kHasLpf, sid(String::STRING_FOR_LPF_FREQUENCY)),
                     global_lpf_graph_draw);
inline constexpr Node global_lpf_res =
    param(sid(String::STRING_FOR_RESONANCE),
          {deluge::plugin::ParamKind::UNPATCHED_GLOBAL,
           deluge::host::chains::runtime_index(deluge::host::chains::param::kLpfResonance)},
          Hint::Bar, param_shows::kHasLpf, sid(String::STRING_FOR_LPF_RESONANCE));
inline constexpr Node global_lpf_morph = with_name_text(
    with_value_text(
        with_column_draw(param(sid(String::STRING_FOR_MORPH),
                               {deluge::plugin::ParamKind::UNPATCHED_GLOBAL,
                                deluge::host::chains::runtime_index(deluge::host::chains::param::kLpfMorph)},
                               Hint::Bar, param_shows::kHasLpf, sid(String::STRING_FOR_LPF_MORPH)),
                         lpf_morph_draw),
        lpf_morph_text),
    lpf_morph_text);
inline constexpr const Node* kGlobalLpfChildren[] = {&lpf_mode, &global_lpf_freq, &global_lpf_res, &global_lpf_morph};
inline constexpr Node global_lpf = submenu(sid(String::STRING_FOR_LPF), kGlobalLpfChildren, Layout::Horizontal);

inline constexpr Node global_hpf_freq =
    with_column_draw(param(sid(String::STRING_FOR_FREQUENCY),
                           {deluge::plugin::ParamKind::UNPATCHED_GLOBAL,
                            deluge::host::chains::runtime_index(deluge::host::chains::param::kHpfFrequency)},
                           Hint::Bar, param_shows::kHasHpf, sid(String::STRING_FOR_HPF_FREQUENCY)),
                     global_hpf_graph_draw);
inline constexpr Node global_hpf_res =
    param(sid(String::STRING_FOR_RESONANCE),
          {deluge::plugin::ParamKind::UNPATCHED_GLOBAL,
           deluge::host::chains::runtime_index(deluge::host::chains::param::kHpfResonance)},
          Hint::Bar, param_shows::kHasHpf, sid(String::STRING_FOR_HPF_RESONANCE));
inline constexpr Node global_hpf_morph = with_name_text(
    with_reverse_in_horizontal(
        with_value_text(
            with_column_draw(param(sid(String::STRING_FOR_MORPH),
                                   {deluge::plugin::ParamKind::UNPATCHED_GLOBAL,
                                    deluge::host::chains::runtime_index(deluge::host::chains::param::kHpfMorph)},
                                   Hint::Bar, param_shows::kHasHpf, sid(String::STRING_FOR_HPF_MORPH)),
                             hpf_morph_draw),
            hpf_morph_text),
        hpf_morph_reversed),
    hpf_morph_text);
inline constexpr const Node* kGlobalHpfChildren[] = {&hpf_mode, &global_hpf_freq, &global_hpf_res, &global_hpf_morph};
inline constexpr Node global_hpf = submenu(sid(String::STRING_FOR_HPF), kGlobalHpfChildren, Layout::Horizontal);

inline constexpr const Node* kGlobalFiltersChildren[] = {&global_lpf, &global_hpf, &filter_routing};
inline constexpr Node global_filters = submenu(sid(String::STRING_FOR_FILTERS), kGlobalFiltersChildren);

// ---------------------------------------------------------------------------------------------
// The global FX page

inline constexpr Node global_delay_amount = with_column_draw(
    with_column_name(param(sid(String::STRING_FOR_AMOUNT),
                           {deluge::plugin::ParamKind::UNPATCHED_GLOBAL,
                            deluge::host::chains::runtime_index(deluge::host::chains::param::kDelayFeedback)},
                           Hint::Bar, nullptr, sid(String::STRING_FOR_DELAY_AMOUNT)),
                     sid(String::STRING_FOR_AMOUNT_SHORT)),
    delay_amount_draw);
inline constexpr Node global_delay_rate =
    param(sid(String::STRING_FOR_RATE),
          {deluge::plugin::ParamKind::UNPATCHED_GLOBAL,
           deluge::host::chains::runtime_index(deluge::host::chains::param::kDelayRate)},
          Hint::Knob, nullptr, sid(String::STRING_FOR_DELAY_RATE));
inline constexpr const Node* kGlobalDelayChildren[] = {&global_delay_amount, &delay_ping_pong, &delay_sync,
                                                       &global_delay_rate, &delay_type};
inline constexpr Node global_delay = submenu(sid(String::STRING_FOR_DELAY), kGlobalDelayChildren, Layout::Horizontal);

// `reverb::Amount_Unpatched` renders a BAR (`Amount_Unpatched::getRenderingStyle` in
// `origin/main:src/deluge/gui/menu_item/reverb/amount_unpatched.h`, the unpatched twin of `Amount`).
inline constexpr Node global_reverb_amount =
    with_column_name(param(sid(String::STRING_FOR_AMOUNT),
                           {deluge::plugin::ParamKind::UNPATCHED_GLOBAL,
                            deluge::host::chains::runtime_index(deluge::host::chains::param::kReverbAmount)},
                           Hint::Bar, nullptr, sid(String::STRING_FOR_REVERB_AMOUNT)),
                     sid(String::STRING_FOR_AMOUNT_SHORT));
inline constexpr const Node* kGlobalReverbChildren[] = {&global_reverb_amount, &reverb_room_size, &reverb_damping,
                                                        &reverb_width,         &reverb_model,     &reverb_pan,
                                                        &reverb_hpf,           &reverb_lpf,       &reverb_sidechain};
inline constexpr Node global_reverb =
    submenu(sid(String::STRING_FOR_REVERB), kGlobalReverbChildren, Layout::Horizontal);

inline constexpr Node global_mod_fx_depth = with_name_text(
    with_value_text(param(sid(String::STRING_FOR_DEPTH),
                          {deluge::plugin::ParamKind::UNPATCHED_GLOBAL,
                           deluge::host::chains::runtime_index(deluge::host::chains::param::kModFXDepth)},
                          Hint::Bar, param_shows::kModFxDepth, sid(String::STRING_FOR_MOD_FX_DEPTH)),
                    mod_fx_depth_text),
    mod_fx_depth_text);
inline constexpr Node global_mod_fx_rate =
    param(sid(String::STRING_FOR_RATE),
          {deluge::plugin::ParamKind::UNPATCHED_GLOBAL,
           deluge::host::chains::runtime_index(deluge::host::chains::param::kModFXRate)},
          Hint::Knob, param_shows::kModFxIsOn, sid(String::STRING_FOR_MOD_FX_RATE));
inline constexpr const Node* kGlobalModFxChildren[] = {&mod_fx_type, &global_mod_fx_depth, &global_mod_fx_rate,
                                                       &mod_fx_feedback, &mod_fx_offset};
inline constexpr Node global_mod_fx = with_page_title(
    submenu(sid(String::STRING_FOR_MOD_FX), kGlobalModFxChildren, Layout::Horizontal), mod_fx_page_title);

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
// (`origin/main:src/deluge/gui/menu_item/sidechain/volume_global.h`; the first is the sound-editor root's
// `sidechain_shape`): the reverb recomputes after a write to it, which SetParamValue raises for the param. Its
// rendering style is SIDECHAIN_DUCKING (`GlobalVolume::getRenderingStyle` in
// `origin/main:src/deluge/gui/menu_item/sidechain/volume_global.h`), i.e. `Hint::Ducking`.

inline constexpr Node global_sidechain_volume = with_column_name(
    param(sid(String::STRING_FOR_VOLUME_DUCKING),
          {deluge::plugin::ParamKind::UNPATCHED_GLOBAL,
           deluge::host::chains::runtime_index(deluge::host::chains::param::kSidechainCompressorVolume)},
          Hint::Ducking),
    sid(String::STRING_FOR_VOLUME_DUCKING_SHORT));
inline constexpr const Node* kGlobalSidechainChildren[] = {&global_sidechain_volume, &sidechain_sync, &sidechain_shape,
                                                           &sidechain_attack, &sidechain_release};
inline constexpr Node global_sidechain =
    submenu(sid(String::STRING_FOR_SIDECHAIN), kGlobalSidechainChildren, Layout::Horizontal);

// ---------------------------------------------------------------------------------------------
// The kit-drum root (`soundEditorRootMenuDrum` in `origin/main:src/deluge/gui/ui/menus.cpp`)
//
// Identical to `sound_root` but for its first child: a kit row's actions page renames the *drum*
// (`drumNameEditMenu`) where a synth's renames the track and the clip.

inline constexpr Node drum_name_edit = custom(sid(String::STRING_FOR_RENAME_DRUM), &kEditDrumName);

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
// The MIDI / CV root (`soundEditorRootMenuMIDIOrCV` in `origin/main:src/deluge/gui/ui/menus.cpp`)

inline constexpr const Node* kMidiOrCvActionsChildren[] = {&edit_name};
inline constexpr Node midi_or_cv_actions = submenu(sid(String::STRING_FOR_ACTIONS), kMidiOrCvActionsChildren);

/// `midiDeviceLinkedMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`), a `midi::device_definition::Linked` -- a
/// `Toggle` whose ON arm opens `loadMidiDeviceDefinitionUI` and whose OFF arm clears the linked file name
/// (`Linked::writeCurrentValue` in `origin/main:src/deluge/gui/menu_item/midi/device_definition/linked.h`). Both arms
/// are `nodes::kMidiDeviceLinked`. Its row is a checkbox and it has no page of its own, so its values are a toggle's:
/// the words a toggle's page lists.
inline constexpr Node midi_device_linked =
    with_value_names(with_relevant(custom(sid(String::STRING_FOR_MIDI_DEVICE_DEFINITION_LINKED), &kMidiDeviceLinked),
                                   relevance::is_midi_out),
                     kToggleWords);
inline constexpr Node midi_hide_unlabeled =
    setting_item(sid(String::STRING_FOR_MIDI_DEVICE_DEFINITION_HIDE), Kind::Toggle,
                 {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                  .path = "hideUnlabeledCC",
                  .group = deluge::project::SettingGroupId::INSTRUMENT_DEVICE});
inline constexpr const Node* kMidiDeviceDefinitionChildren[] = {&midi_device_linked, &midi_hide_unlabeled};
inline constexpr Node midi_device_definition =
    submenu(sid(String::STRING_FOR_MIDI_DEVICE_DEFINITION), kMidiDeviceDefinitionChildren, Layout::Vertical,
            relevance::is_midi_out);

// `midi::Preset`: 0..127, and 128 for none (`getMaxValue`); `midi_preset_turn` wraps round it and
// `midi_preset_text` names its top.
inline constexpr IntRange kMidiPresetRange{0, 128, 1};
inline constexpr Node midi_bank =
    with_leaf_text(with_turn(without_notification(with_column_art(
                                 with_title(setting_item(sid(String::STRING_FOR_BANK), Kind::Int,
                                                         {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                                          .path = "midiBank",
                                                          .group = deluge::project::SettingGroupId::CLIP_VALUES}),
                                            sid(String::STRING_FOR_MIDI_BANK)),
                                 midi_preset_art)),
                             midi_preset_turn),
                   midi_preset_text);
inline constexpr Node midi_sub_bank =
    with_leaf_text(with_turn(without_notification(with_column_art(
                                 with_title(setting_item(sid(String::STRING_FOR_SUB_BANK_SHORT), Kind::Int,
                                                         {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                                          .path = "midiSub",
                                                          .group = deluge::project::SettingGroupId::CLIP_VALUES}),
                                            sid(String::STRING_FOR_MIDI_SUB_BANK)),
                                 midi_preset_art)),
                             midi_preset_turn),
                   midi_preset_text);
inline constexpr Node midi_pgm =
    with_leaf_text(with_turn(without_notification(with_column_art(
                                 with_title(setting_item(sid(String::STRING_FOR_PGM), Kind::Int,
                                                         {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                                          .path = "midiPGM",
                                                          .group = deluge::project::SettingGroupId::CLIP_VALUES}),
                                            sid(String::STRING_FOR_MIDI_PGM_NUMB_MENU_TITLE)),
                                 midi_preset_art)),
                             midi_preset_turn),
                   midi_preset_text);
inline constexpr const Node* kMidiProgramChildren[] = {&midi_bank, &midi_sub_bank, &midi_pgm};
/// `midiProgramMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`) is a `midi::ProgramSubMenu`
/// (`origin/main:src/deluge/gui/menu_item/midi/program.h`), a horizontal menu, whose name *is* its title
/// (`STRING_FOR_MIDI_PROGRAM_MENU_TITLE`, the only string its constructor is given), FIXED layout with two slots and
/// `Layout::FIXED` (`with_fixed_columns`).
inline constexpr Node midi_program = with_fixed_columns(
    submenu(sid(String::STRING_FOR_MIDI_PROGRAM_MENU_TITLE), kMidiProgramChildren, Layout::Horizontal));

// The MIDI/CV arpeggiator: `arpMenuMIDIOrCV` (`origin/main:src/deluge/gui/ui/menus.cpp`) differs from `arpMenu` only in
// its basic page, whose gate and rate live on `ArpeggiatorSettings` rather than in a param collection (`midi_cv::Gate`
// in `origin/main:src/deluge/gui/menu_item/arpeggiator/midi_cv/gate.h`, `midi_cv::Rate` in
// `origin/main:src/deluge/gui/menu_item/arpeggiator/midi_cv/rate.h`).
inline constexpr Node arp_gate_midi = with_title(
    setting_item(sid(String::STRING_FOR_GATE), Kind::Int, arp_row("arpeggiator/gate", false), Hint::LengthSlider),
    sid(String::STRING_FOR_ARP_GATE_MENU_TITLE));
inline constexpr Node arp_rate_midi =
    with_title(setting_item(sid(String::STRING_FOR_RATE), Kind::Int, arp_row("arpeggiator/rate", false), Hint::Knob),
               sid(String::STRING_FOR_ARP_RATE_MENU_TITLE));
inline constexpr const Node* kArpBasicMidiChildren[] = {&arp_preset, &arp_gate_midi, &arp_sync, &arp_rate_midi};
inline constexpr Node arp_basic_midi = submenu(sid(String::STRING_FOR_BASIC), kArpBasicMidiChildren, Layout::Horizontal,
                                               nullptr, sid(String::STRING_FOR_ARP_BASIC_MENU_TITLE));
inline constexpr const Node* kArpMidiChildren[] = {&arp_mode, &arp_basic_midi, &arp_pattern, &arp_mpe,
                                                   &arp_include_in_kit_arp};
inline constexpr Node arp_midi = submenu(sid(String::STRING_FOR_ARPEGGIATOR), kArpMidiChildren);

inline constexpr Node cv2_source = setting_item(sid(String::STRING_FOR_CV2_SOURCE), Kind::Enum,
                                                {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                                 .path = "cv2Source",
                                                 .group = deluge::project::SettingGroupId::INSTRUMENT_DEVICE});

inline constexpr Node mpe_y_to_mod_wheel = setting_item(sid(String::STRING_FOR_Y_AXIS_CONVERSION), Kind::Toggle,
                                                        {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                                         .path = "outputMpeY",
                                                         .group = deluge::project::SettingGroupId::INSTRUMENT_DEVICE});
inline constexpr Node midi_aftertouch_collapse =
    setting_item(sid(String::STRING_FOR_PATCH_SOURCE_AFTERTOUCH), Kind::Toggle,
                 {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                  .path = "collapseAftertouch",
                  .group = deluge::project::SettingGroupId::INSTRUMENT_DEVICE});
inline constexpr Node midi_mpe_collapse = setting_item(sid(String::STRING_FOR_MPE), Kind::Toggle,
                                                       {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                                        .path = "collapseMpe",
                                                        .group = deluge::project::SettingGroupId::INSTRUMENT_DEVICE});
inline constexpr const Node* kMidiMpeChildren[] = {&midi_aftertouch_collapse, &midi_mpe_collapse};
inline constexpr Node midi_mpe =
    submenu(sid(String::STRING_FOR_MPE_MONO), kMidiMpeChildren, Layout::Vertical, relevance::is_midi_out);

inline constexpr const Node* kMidiOrCvRootChildren[] = {
    &midi_or_cv_actions, &midi_device_definition, &midi_program, &arp_midi,          &randomizer, &bend,
    &cv2_source,         &mpe_y_to_mod_wheel,     &midi_mpe,     &sequence_direction};
inline constexpr Node root_midi_or_cv = submenu(sid(String::STRING_FOR_MIDI_INST_MENU_TITLE), kMidiOrCvRootChildren);
/// The CV spelling of the very same root -- `SoundEditor::setup`'s retitle, as a second node.
inline constexpr Node root_cv = submenu(sid(String::STRING_FOR_CV_INSTRUMENT), kMidiOrCvRootChildren);

/// @brief Which of the two MIDI/CV root nodes @p ctx wants -- the port of `setup()`'s retitle.
const Node& midi_or_cv_root(const MenuContext& ctx);

// --------------------------------------------------------------------------------------------- The MIDI-drum and
// gate-drum roots (`soundEditorRootMenuMidiDrum` and `soundEditorRootMenuGateDrum` in
// `origin/main:src/deluge/gui/ui/menus.cpp`)

inline constexpr const Node* kNonAudioDrumRootChildren[] = {&drum_actions, &arp_midi, &randomizer};
inline constexpr Node root_midi_drum = submenu(sid(String::STRING_FOR_MIDI), kNonAudioDrumRootChildren);
inline constexpr Node root_gate_drum = submenu(sid(String::STRING_FOR_GATE), kNonAudioDrumRootChildren);

// ---------------------------------------------------------------------------------------------
// The audio-clip root (`soundEditorRootMenuAudioClip` in `origin/main:src/deluge/gui/ui/menus.cpp`)

inline constexpr Node set_clip_length =
    custom(sid(String::STRING_FOR_SET_CLIP_LENGTH_EQUAL_TO_SAMPLE_LENGTH), &kSetClipLengthItem);
inline constexpr const Node* kAudioClipActionsChildren[] = {&edit_name, &set_clip_length};
inline constexpr Node audio_clip_actions = submenu(sid(String::STRING_FOR_ACTIONS), kAudioClipActionsChildren);

inline constexpr Node audio_source_selector =
    with_value_names(custom(sid(String::STRING_FOR_AUDIO_SOURCE), &kAudioSourceItem), kAudioSourceOptions);
/// `specificOutputSelectorMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`), an
/// `audio_clip::SpecificSourceOutputSelector` -- the picker for which track this audio track
/// records from, **not** a browser (`nodes/output_items.h`).
inline constexpr Node specific_output_selector =
    custom(sid(String::STRING_FOR_TRACK), &kSpecificOutputSelector, relevance::audio_source_is_specific);
// `audioClipSampleMarkerEditorMenuEnd` (`origin/main:src/deluge/gui/ui/menus.cpp`), the only one of the two
// `audio_clip::SampleMarkerEditor` globals any submenu names. Relevant only once the clip has a
// sample, as the legacy item's `isRelevant` is.
inline constexpr Node audio_clip_waveform = with_value_text(
    with_column_draw(with_allow_session_from_horizontal(custom(sid(String::STRING_FOR_WAVEFORM), &kAudioClipWaveform,
                                                               relevance::audio_clip_has_sample)),
                     clip_end_draw),
    end_point_label);

/// The audio clip's marker editor on START: `audioClipSampleMarkerEditorMenuStart`, which no submenu lists and whose
/// name is empty; only the V1 shortcut cells `(0..1, 7)` reach it, so it has a place in `kAllRoots` and no menu row.
inline constexpr Node audio_clip_waveform_start =
    custom(sid(String::EMPTY_STRING), &kAudioClipWaveformStart, relevance::audio_clip_has_sample);

inline constexpr const Node* kAudioClipDistortionChildren[] = {&saturation, &bitcrush, &decimation};
inline constexpr Node audio_clip_distortion =
    submenu(sid(String::STRING_FOR_DISTORTION), kAudioClipDistortionChildren, Layout::Horizontal);

inline constexpr const Node* kAudioClipFxChildren[] = {&eq,      &global_delay,  &global_reverb,
                                                       &stutter, &global_mod_fx, &audio_clip_distortion};
inline constexpr Node audio_clip_fx = submenu(sid(String::STRING_FOR_FX), kAudioClipFxChildren);

// `audio_clip::Transpose` (`origin/main:src/deluge/gui/menu_item/audio_clip/transpose.h`): a `Decimal` over the sample
// holder's transpose+cents pair, with a learn-only `ParamRef` on `UNPATCHED_PITCH_ADJUST`
// (`Transpose::getLearningThing`) -- exactly the shape `with_learn_param` exists for (`node.h`'s `Binding` precedence
// note).
inline constexpr Node audio_clip_transpose =
    with_learn_param(with_decimal_places(setting_item(sid(String::STRING_FOR_TRANSPOSE), Kind::Decimal,
                                                      {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                                       .path = "transpose",
                                                       .group = deluge::project::SettingGroupId::AUDIO_CLIP}),
                                         2),
                     {deluge::plugin::ParamKind::UNPATCHED_GLOBAL,
                      deluge::host::chains::runtime_index(deluge::host::chains::param::kPitchAdjust)});
/// The grid draws the clip's waveform, which a reverse turns round.
inline constexpr Node audio_clip_reverse = with_reverse_in_horizontal(
    with_column_name(with_value_text(with_column_art(with_grid_repaint(setting_item(
                                                         sid(String::STRING_FOR_REVERSE), Kind::Toggle,
                                                         {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                                          .path = "reversed",
                                                          .group = deluge::project::SettingGroupId::AUDIO_CLIP})),
                                                     direction_art),
                                     on_off_text),
                     sid(String::STRING_FOR_PLAY)),
    reverse_always);

inline constexpr Node audio_clip_interpolation =
    with_affect_entire(with_title(setting_item(sid(String::STRING_FOR_INTERPOLATION), Kind::Enum,
                                               {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                                .path = "linearInterpolation",
                                                .group = deluge::project::SettingGroupId::AUDIO_CLIP}),
                                  sid(String::STRING_FOR_AUDIO_INTERPOLATION)));

inline constexpr const Node* kAudioClipSampleChildren[] = {&file0_selector,      &audio_clip_transpose,
                                                           &audio_clip_reverse,  &sample0_pitch_speed,
                                                           &audio_clip_waveform, &audio_clip_interpolation};
inline constexpr Node audio_clip_sample =
    submenu(sid(String::STRING_FOR_SAMPLE), kAudioClipSampleChildren, Layout::Horizontal);

inline constexpr Node audio_clip_attack = setting_item(sid(String::STRING_FOR_ATTACK), Kind::Int,
                                                       {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
                                                        .path = "attack",
                                                        .group = deluge::project::SettingGroupId::AUDIO_CLIP});

inline constexpr const Node* kAudioClipRootChildren[] = {
    &audio_clip_actions, &audio_source_selector, &specific_output_selector, &song_master,       &audio_comp,
    &global_filters,     &audio_clip_fx,         &global_sidechain,         &audio_clip_sample, &audio_clip_attack,
    &voice_priority};
inline constexpr Node root_audio_clip = submenu(sid(String::STRING_FOR_AUDIO_CLIP), kAudioClipRootChildren);

// ---------------------------------------------------------------------------------------------
// The performance-view root (`soundEditorRootMenuPerformanceView` in `origin/main:src/deluge/gui/ui/menus.cpp`)

inline constexpr Node perform_editor =
    with_value_names(custom(sid(String::STRING_FOR_PERFORM_EDITOR), &kPerformEditorItem), kPerformEditingModeNames);

inline constexpr const Node* kPerformanceRootChildren[] = {&perform_editor, &global_filters, &global_fx};
inline constexpr Node root_performance = submenu(sid(String::STRING_FOR_PERFORM_FX), kPerformanceRootChildren);

// --------------------------------------------------------------------------------------------- Stem export
// (`startStemExportMenu` and its siblings in `origin/main:src/deluge/gui/ui/menus.cpp`), shared by the song and
// kit-global-FX roots. The export's options are DEVICE rows, which the device port keeps on the export itself and
// saves nowhere.

inline constexpr Node start_stem_export = custom(sid(String::STRING_FOR_START_EXPORT), &kStartStemExportItem);
inline constexpr Node stem_normalization = setting_item(sid(String::STRING_FOR_CONFIGURE_EXPORT_STEMS_NORMALIZATION),
                                                        Kind::Toggle, device_row(project::device_path::kStemNormalize));
inline constexpr Node stem_normalization_drums =
    setting_item(sid(String::STRING_FOR_CONFIGURE_EXPORT_STEMS_NORMALIZATION), Kind::Toggle,
                 device_row(project::device_path::kStemNormalizeDrums));
inline constexpr Node stem_silence = setting_item(sid(String::STRING_FOR_CONFIGURE_EXPORT_STEMS_SILENCE), Kind::Toggle,
                                                  device_row(project::device_path::kStemToSilence));
inline constexpr Node stem_song_fx = setting_item(sid(String::STRING_FOR_CONFIGURE_EXPORT_STEMS_SONGFX), Kind::Toggle,
                                                  device_row(project::device_path::kStemSongFx));
inline constexpr Node stem_kit_fx = setting_item(sid(String::STRING_FOR_CONFIGURE_EXPORT_STEMS_KITFX), Kind::Toggle,
                                                 device_row(project::device_path::kStemKitFx));
inline constexpr Node stem_offline = setting_item(sid(String::STRING_FOR_CONFIGURE_EXPORT_STEMS_OFFLINE_RENDERING),
                                                  Kind::Toggle, device_row(project::device_path::kStemOffline));
inline constexpr Node stem_mixdown = setting_item(sid(String::STRING_FOR_CONFIGURE_EXPORT_STEMS_MIXDOWN), Kind::Toggle,
                                                  device_row(project::device_path::kStemMixdown));

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
// The song root (`soundEditorRootMenuSongView` in `origin/main:src/deluge/gui/ui/menus.cpp`)

/// `swing::Interval::size()` is `NUM_SWING_INTERVALS` (`origin/main:src/deluge/gui/menu_item/swing/interval.h`), here
/// `plugin::kNumSwingIntervals`, rather than every combined sync value: the same kind of override `sidechain::Sync`
/// makes.
inline constexpr IntRange kSwingIntervalRange{0, deluge::plugin::kNumSwingIntervals - 1, 1};
/// `swing::Interval::selectEncoderAction` (`wrapSwingIntervalSyncLevel`): a turn wraps within the even levels and
/// never lands on 0, OFF. `defaults::SwingInterval` inherits it.
std::optional<int32_t> swing_interval_turn(const MenuContext& ctx, int32_t value, int32_t offset);
inline constexpr Node swing_interval =
    with_turn(sync_level_item(sid(String::STRING_FOR_SWING_INTERVAL), 0, get_swing_interval, set_swing_interval,
                              kSwingIntervalRange),
              swing_interval_turn);

inline constexpr Node active_scales =
    with_value_names(custom(sid(String::STRING_FOR_ACTIVE_SCALES), &kSongActiveScalesItem), preset_scale_names);
/// The song root's two session-only items: `song::ConfigureMacros::isRelevant`
/// (`origin/main:src/deluge/gui/menu_item/song/configure_macros.h`) and `song::MidiLearn::isRelevant`
/// (`origin/main:src/deluge/gui/menu_item/song/midi_learn.h`) are both `getRootUI() == &sessionView`, so both are
/// hidden in the arranger and the song automation view even though `SoundEditor::setup`
/// opens this same root there. They are the only two nodes in the whole tree that spend
/// `MenuContext::is_session_view`.
inline constexpr Node configure_song_macros =
    custom(sid(String::STRING_FOR_CONFIGURE_SONG_MACROS), &kConfigureSongMacrosItem, relevance::is_session_view);
inline constexpr Node song_midi_learn =
    custom(sid(String::STRING_FOR_MIDI_LEARN), &kSongMidiLearnItem, relevance::is_session_view);

/// `record::ThresholdMode::getOptions` (`origin/main:src/deluge/gui/menu_item/record/threshold_mode.h`), shared with
/// the settings root's `defaultThresholdRecordingModeMenu`.
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
// The kit global-FX root (`soundEditorRootMenuKitGlobalFX` in `origin/main:src/deluge/gui/ui/menus.cpp`)

inline constexpr const Node* kKitFxActionsChildren[] = {&edit_name, &kit_fx_stem_export};
inline constexpr Node kit_fx_actions = submenu(sid(String::STRING_FOR_ACTIONS), kKitFxActionsChildren);

// `arpeggiator::KitRate` is the kit's own `UNPATCHED_ARP_RATE`, shown only on the kit's global-FX page
// (`KitRate::isRelevant` in `origin/main:src/deluge/gui/menu_item/arpeggiator/rate.h`); `arpGateMenu` is shared with
// the sound arpeggiator.
inline constexpr Node arp_kit_rate =
    param(sid(String::STRING_FOR_RATE), arp_param(deluge::arp::param::kRate), Hint::Knob, param_shows::kKitEffects,
          sid(String::STRING_FOR_ARP_RATE_MENU_TITLE));
inline constexpr const Node* kArpBasicKitChildren[] = {&arp_preset, &arp_gate, &arp_sync, &arp_kit_rate};
inline constexpr Node arp_basic_kit = submenu(sid(String::STRING_FOR_BASIC), kArpBasicKitChildren, Layout::Horizontal,
                                              nullptr, sid(String::STRING_FOR_ARP_BASIC_MENU_TITLE));
inline constexpr const Node* kKitArpChildren[] = {&arp_mode, &arp_basic_kit, &arp_pattern, &randomizer};
inline constexpr Node kit_arp = submenu(sid(String::STRING_FOR_KIT_ARPEGGIATOR), kKitArpChildren);

inline constexpr const Node* kKitGlobalFxRootChildren[] = {
    &kit_fx_actions, &kit_clip_master, &kit_arp, &audio_comp, &global_filters, &global_fx, &global_sidechain};
inline constexpr Node root_kit_global_fx = submenu(sid(String::STRING_FOR_KIT_GLOBAL_FX), kKitGlobalFxRootChildren);

// ---------------------------------------------------------------------------------------------

} // namespace deluge::ui::scenes::menu::nodes
