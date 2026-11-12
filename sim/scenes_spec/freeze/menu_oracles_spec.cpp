/// @file
/// @brief Records what the legacy menu items answer, as the reference tables `tests/golden/ui/menu_oracle/` holds.
///
/// Not a spec of anything: the scenes-spec gate skips `freeze/`, and every example fails unless `MENU_ORACLE_OUT`
/// names the directory to write into. One run writes the one family `MENU_ORACLE_FAMILIES` names, so each family
/// starts in a fresh process, as the spec that reads it does:
///
///     GOLDEN_SCENARIO=scenes_spec SCENES_SPEC=freeze/menu_oracles_spec SCENES_SPEC_CARD=<card> \
///       MENU_ORACLE_FAMILIES=<family> MENU_ORACLE_OUT=<repo>/tests/golden/ui/menu_oracle golden_vt_render
///
/// Each family asks the legacy items the questions its spec asks, in the spec's order and on the spec's own fixtures
/// (the shared `support/*_fixtures.h`), because a write leaves the model where it wrote.
///
/// @note This file may include `gui/menu_item/`, `gui/ui/menus.h` and `gui/ui/sound_editor.h`; the specs that read
///       the tables do not.

#include "support/menu_over_root.h"
#include "support/oracle_table.h"
#include "support/oracle_tree.h"
#include "support/output_items_fixtures.h"
#include "support/sample_items_fixtures.h"
#include "support/tables_1_fixtures.h"
#include "support/tables_2_fixtures.h"
#include "support/tables_3_fixtures.h"

#include "gui/ui/menu_view_of.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/dynamic.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/relevance.h"
#include "scenes/menu/shortcuts.h"

// The legacy headers below render through an unqualified `OLED::main`, which resolves inside `gui/ui/menus.cpp` only
// because that file has a file-scope `using namespace deluge::hid::display`.
namespace deluge::gui::menu_item {
using deluge::hid::display::OLED;
} // namespace deluge::gui::menu_item
#include "gui/context_menu/reset_settings.h"
#include "gui/l10n/setting_names.h"
#include "gui/l10n/short_label.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/active_scales.h"
#include "gui/menu_item/arpeggiator/arp_unpatched_param.h"
#include "gui/menu_item/arpeggiator/chord_type.h"
#include "gui/menu_item/arpeggiator/include_in_kit_arp.h"
#include "gui/menu_item/arpeggiator/midi_cv/gate.h"
#include "gui/menu_item/arpeggiator/midi_cv/rate.h"
#include "gui/menu_item/arpeggiator/midi_cv/rhythm.h"
#include "gui/menu_item/arpeggiator/midi_cv/sequence_length.h"
#include "gui/menu_item/arpeggiator/mode.h"
#include "gui/menu_item/arpeggiator/mpe_velocity.h"
#include "gui/menu_item/arpeggiator/note_mode.h"
#include "gui/menu_item/arpeggiator/note_mode_for_drums.h"
#include "gui/menu_item/arpeggiator/octave_mode.h"
#include "gui/menu_item/arpeggiator/octaves.h"
#include "gui/menu_item/arpeggiator/preset_mode.h"
#include "gui/menu_item/arpeggiator/rate.h"
#include "gui/menu_item/arpeggiator/rhythm.h"
#include "gui/menu_item/arpeggiator/sequence_length.h"
#include "gui/menu_item/arpeggiator/step_repeat.h"
#include "gui/menu_item/arpeggiator/sync.h"
#include "gui/menu_item/audio_clip/attack.h"
#include "gui/menu_item/audio_clip/audio_source_selector.h"
#include "gui/menu_item/audio_clip/reverse.h"
#include "gui/menu_item/audio_clip/sample_marker_editor.h"
#include "gui/menu_item/audio_clip/set_clip_length_equal_to_sample_length.h"
#include "gui/menu_item/audio_clip/specific_output_source_selector.h"
#include "gui/menu_item/audio_clip/transpose.h"
#include "gui/menu_item/audio_compressor/compressor_params.h"
#include "gui/menu_item/audio_compressor/compressor_values.h"
#include "gui/menu_item/audio_interpolation.h"
#include "gui/menu_item/automation/automation.h"
#include "gui/menu_item/battery/level.h"
#include "gui/menu_item/bend_range/main.h"
#include "gui/menu_item/bend_range/per_finger.h"
#include "gui/menu_item/colour.h"
#include "gui/menu_item/cpu/voice_usage.h"
#include "gui/menu_item/cv/cv2Mapping.h"
#include "gui/menu_item/defaults/accessibility_menu_highlighting.h"
#include "gui/menu_item/defaults/bend_range.h"
#include "gui/menu_item/defaults/favourites_layout.h"
#include "gui/menu_item/defaults/grid_default_active_mode.h"
#include "gui/menu_item/defaults/hold_time.h"
#include "gui/menu_item/defaults/keyboard_layout.h"
#include "gui/menu_item/defaults/magnitude.h"
#include "gui/menu_item/defaults/metronome_volume.h"
#include "gui/menu_item/defaults/pad_brightness.h"
#include "gui/menu_item/defaults/patch_cable_polarity.h"
#include "gui/menu_item/defaults/scale.h"
#include "gui/menu_item/defaults/screensaver.h"
#include "gui/menu_item/defaults/session_layout.h"
#include "gui/menu_item/defaults/slice_mode.h"
#include "gui/menu_item/defaults/startup_song_mode.h"
#include "gui/menu_item/defaults/swing_interval.h"
#include "gui/menu_item/defaults/ui/clip_type/default_new_clip_type.h"
#include "gui/menu_item/defaults/velocity.h"
#include "gui/menu_item/delay/amount.h"
#include "gui/menu_item/delay/amount_unpatched.h"
#include "gui/menu_item/delay/ping_pong.h"
#include "gui/menu_item/delay/sync.h"
#include "gui/menu_item/delay/type.h"
#include "gui/menu_item/dx/browse.h"
#include "gui/menu_item/dx/cartridge.h"
#include "gui/menu_item/dx/engine_select.h"
#include "gui/menu_item/dx/global_params.h"
#include "gui/menu_item/dx/operator_params.h"
#include "gui/menu_item/dx/param.h"
#include "gui/menu_item/edit_name.h"
#include "gui/menu_item/envelope/envelope_menu.h"
#include "gui/menu_item/envelope/segment.h"
#include "gui/menu_item/eq/eq_unpatched_param.h"
#include "gui/menu_item/file_selector.h"
#include "gui/menu_item/filter/filter_container.h"
#include "gui/menu_item/filter/mode.h"
#include "gui/menu_item/filter/param.h"
#include "gui/menu_item/filter_route.h"
#include "gui/menu_item/firmware/version.h"
#include "gui/menu_item/flash/status.h"
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/fx/clipping.h"
#include "gui/menu_item/horizontal_menu.h"
#include "gui/menu_item/horizontal_menu_container.h"
#include "gui/menu_item/horizontal_menu_group.h"
#include "gui/menu_item/integer_range.h"
#include "gui/menu_item/key_range.h"
#include "gui/menu_item/keyboard/layout.h"
#include "gui/menu_item/lfo/sync.h"
#include "gui/menu_item/lfo/type.h"
#include "gui/menu_item/master_transpose.h"
#include "gui/menu_item/menu_item.h"
#include "gui/menu_item/menu_item_with_cc_learning.h"
#include "gui/menu_item/midi/after_touch_to_mono.h"
#include "gui/menu_item/midi/bank.h"
#include "gui/menu_item/midi/command.h"
#include "gui/menu_item/midi/default_velocity_to_level.h"
#include "gui/menu_item/midi/device.h"
#include "gui/menu_item/midi/device_definition/hide_unlabeled.h"
#include "gui/menu_item/midi/device_definition/linked.h"
#include "gui/menu_item/midi/device_definition/submenu.h"
#include "gui/menu_item/midi/device_is_relative.h"
#include "gui/menu_item/midi/device_receive_clock.h"
#include "gui/menu_item/midi/device_send_clock.h"
#include "gui/menu_item/midi/device_send_thru.h"
#include "gui/menu_item/midi/devices.h"
#include "gui/menu_item/midi/follow/follow_channel.h"
#include "gui/menu_item/midi/follow/follow_feedback_automation.h"
#include "gui/menu_item/midi/follow/follow_feedback_channel_type.h"
#include "gui/menu_item/midi/follow/follow_kit_root_note.h"
#include "gui/menu_item/midi/mpe_to_mono.h"
#include "gui/menu_item/midi/pgm.h"
#include "gui/menu_item/midi/program.h"
#include "gui/menu_item/midi/receive_clock.h"
#include "gui/menu_item/midi/send_clock.h"
#include "gui/menu_item/midi/sound/channel.h"
#include "gui/menu_item/midi/sound/note_for_drum.h"
#include "gui/menu_item/midi/sub.h"
#include "gui/menu_item/midi/takeover.h"
#include "gui/menu_item/midi/transpose.h"
#include "gui/menu_item/midi/y_axis_to_cc1.h"
#include "gui/menu_item/mod_fx/depth_patched.h"
#include "gui/menu_item/mod_fx/depth_unpatched.h"
#include "gui/menu_item/mod_fx/feedback.h"
#include "gui/menu_item/mod_fx/offset.h"
#include "gui/menu_item/mod_fx/rate.h"
#include "gui/menu_item/mod_fx/rate_unpatched.h"
#include "gui/menu_item/mod_fx/type.h"
#include "gui/menu_item/modulator/destination.h"
#include "gui/menu_item/modulator/transpose.h"
#include "gui/menu_item/monitor/mode.h"
#include "gui/menu_item/mpe/direction_selector.h"
#include "gui/menu_item/mpe/zone_num_member_channels.h"
#include "gui/menu_item/mpe/zone_selector.h"
#include "gui/menu_item/note/fill.h"
#include "gui/menu_item/note/iterance_divisor.h"
#include "gui/menu_item/note/iterance_preset.h"
#include "gui/menu_item/note/iterance_step_toggle.h"
#include "gui/menu_item/note/probability.h"
#include "gui/menu_item/note/velocity.h"
#include "gui/menu_item/note_row/fill.h"
#include "gui/menu_item/note_row/iterance_divisor.h"
#include "gui/menu_item/note_row/iterance_preset.h"
#include "gui/menu_item/note_row/iterance_step_toggle.h"
#include "gui/menu_item/note_row/probability.h"
#include "gui/menu_item/osc/audio_recorder.h"
#include "gui/menu_item/osc/pulse_width.h"
#include "gui/menu_item/osc/retrigger_phase.h"
#include "gui/menu_item/osc/source/feedback.h"
#include "gui/menu_item/osc/source/volume.h"
#include "gui/menu_item/osc/source/wave_index.h"
#include "gui/menu_item/osc/sync.h"
#include "gui/menu_item/osc/type.h"
#include "gui/menu_item/patched_param/integer.h"
#include "gui/menu_item/patched_param/integer_non_fm.h"
#include "gui/menu_item/patched_param/pan.h"
#include "gui/menu_item/performance_session_view/editing_mode.h"
#include "gui/menu_item/randomizer/midi_cv/bass_probability.h"
#include "gui/menu_item/randomizer/midi_cv/chord_polyphony.h"
#include "gui/menu_item/randomizer/midi_cv/chord_probability.h"
#include "gui/menu_item/randomizer/midi_cv/glide_probability.h"
#include "gui/menu_item/randomizer/midi_cv/note_probability.h"
#include "gui/menu_item/randomizer/midi_cv/ratchet_amount.h"
#include "gui/menu_item/randomizer/midi_cv/ratchet_probability.h"
#include "gui/menu_item/randomizer/midi_cv/spread_gate.h"
#include "gui/menu_item/randomizer/midi_cv/spread_octave.h"
#include "gui/menu_item/randomizer/midi_cv/spread_velocity.h"
#include "gui/menu_item/randomizer/midi_cv/swap_probability.h"
#include "gui/menu_item/randomizer/randomizer_lock.h"
#include "gui/menu_item/randomizer/randomizer_unpatched_param.h"
#include "gui/menu_item/record/countin.h"
#include "gui/menu_item/record/quantize.h"
#include "gui/menu_item/record/threshold_mode.h"
#include "gui/menu_item/reset_settings/reset.h"
#include "gui/menu_item/reverb/amount.h"
#include "gui/menu_item/reverb/amount_unpatched.h"
#include "gui/menu_item/reverb/damping.h"
#include "gui/menu_item/reverb/hpf.h"
#include "gui/menu_item/reverb/lpf.h"
#include "gui/menu_item/reverb/model.h"
#include "gui/menu_item/reverb/pan.h"
#include "gui/menu_item/reverb/room_size.h"
#include "gui/menu_item/reverb/sidechain/shape.h"
#include "gui/menu_item/reverb/sidechain/volume.h"
#include "gui/menu_item/reverb/width.h"
#include "gui/menu_item/runtime_feature/settings.h"
#include "gui/menu_item/sample/browser_preview/mode.h"
#include "gui/menu_item/sample/end.h"
#include "gui/menu_item/sample/interpolation.h"
#include "gui/menu_item/sample/pitch_speed.h"
#include "gui/menu_item/sample/repeat.h"
#include "gui/menu_item/sample/reverse.h"
#include "gui/menu_item/sample/start.h"
#include "gui/menu_item/sample/time_stretch.h"
#include "gui/menu_item/sample/transpose.h"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/sequence/direction.h"
#include "gui/menu_item/shortcuts/version.h"
#include "gui/menu_item/sidechain/attack.h"
#include "gui/menu_item/sidechain/release.h"
#include "gui/menu_item/sidechain/send.h"
#include "gui/menu_item/sidechain/shape.h"
#include "gui/menu_item/sidechain/sync.h"
#include "gui/menu_item/sidechain/volume_global.h"
#include "gui/menu_item/song/configure_macros.h"
#include "gui/menu_item/song/midi_learn.h"
#include "gui/menu_item/source/patched_param/modulator_feedback.h"
#include "gui/menu_item/source/patched_param/modulator_level.h"
#include "gui/menu_item/stem_export/start.h"
#include "gui/menu_item/stutter/direction.h"
#include "gui/menu_item/stutter/quantized.h"
#include "gui/menu_item/stutter/rate.h"
#include "gui/menu_item/submenu.h"
#include "gui/menu_item/submenu/MPE.h"
#include "gui/menu_item/submenu/actual_source.h"
#include "gui/menu_item/submenu/arp_mpe_submenu.h"
#include "gui/menu_item/submenu/bend.h"
#include "gui/menu_item/submenu/mod_fx.h"
#include "gui/menu_item/submenu/modulator.h"
#include "gui/menu_item/swing/interval.h"
#include "gui/menu_item/synth_mode.h"
#include "gui/menu_item/trigger/in/ppqn.h"
#include "gui/menu_item/trigger/out/ppqn.h"
#include "gui/menu_item/unison/count.h"
#include "gui/menu_item/unison/detune.h"
#include "gui/menu_item/unison/stereoSpread.h"
#include "gui/menu_item/unpatched_param/pan.h"
#include "gui/menu_item/voice/polyphony.h"
#include "gui/menu_item/voice/portamento.h"
#include "gui/menu_item/voice/priority.h"
#include "gui/ui/menus.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "hid/buttons.h"
#include "hid/display/oled.h"
#include "hid/display/oled_canvas/canvas.h"
#include "model/instrument/non_audio_instrument.h"
#include "playback/playback_handler.h"

// `eq_menu.h` names `UnpatchedParam` without including it.
#include "gui/menu_item/unpatched_param.h"

#include "gui/menu_item/cv/selection.h"
#include "gui/menu_item/cv/submenu.h"
#include "gui/menu_item/cv/transpose.h"
#include "gui/menu_item/cv/volts.h"
#include "gui/menu_item/eq/eq_menu.h"
#include "gui/menu_item/gate/mode.h"
#include "gui/menu_item/gate/off_time.h"
#include "gui/menu_item/gate/selection.h"
#include "gui/menu_item/patch_cable_strength/fixed.h"
#include "gui/menu_item/patch_cable_strength/range.h"
#include "gui/menu_item/patch_cable_strength/regular.h"
#include "gui/menu_item/patch_cables.h"
#include "gui/menu_item/sidechain/volume.h"
#include "gui/menu_item/source_selection/range.h"
#include "gui/menu_item/source_selection/regular.h"

// After the patch-cable headers: they spell `MultiRange` unqualified, which `multi_range.h` redirects.
#include "gui/menu_item/multi_range.h"
#include "gui/menu_item/range.h"

#include "effects/reverb.h"
#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/midi_drum.h"
#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "modulation/params/param.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/sound/sound_drum.h"
#include "storage/flash_storage.h"

#include "support/clip_overview_cases.h"
#include "support/descriptor_fixtures.h"
#include "support/dx_cartridge_fixtures.h"
#include "support/dx_pages_fixtures.h"
#include "support/dx_rig.h"
#include "support/global_guest_menu.h"
#include "support/host_flash.h"
#include "support/menu_engine.h"
#include "support/menu_scales_fixtures.h"
#include "support/midi_device_fixtures.h"
#include "support/reset_witness.h"
#include "support/shortcuts_fixtures.h"

#include "spec/gfx/support/pbm.h"

#include "cppspec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "spec/support/spec_main.h"

/// @name Legacy globals no header declares for this file.
/// @{
extern deluge::gui::menu_item::arpeggiator::PresetMode arpPresetModeMenu;
extern deluge::gui::menu_item::voice::PolyphonyType polyphonyMenu;
extern deluge::gui::menu_item::unison::CountToStereoSpread numUnisonMenu;
extern deluge::gui::menu_item::PatchCables patchCablesMenu;
extern deluge::gui::menu_item::patch_cable_strength::Fixed vibratoMenu;
extern deluge::gui::menu_item::sidechain::VolumeShortcut sidechainVolumeShortcutMenu;
extern deluge::gui::menu_item::cv::Selection cvSelectionMenu;
extern deluge::gui::menu_item::gate::Selection gateSelectionMenu;
extern deluge::gui::menu_item::cv::Volts cvVoltsMenu;
extern deluge::gui::menu_item::cv::Transpose cvTransposeMenu;
extern deluge::gui::menu_item::Submenu soundFiltersMenu;
extern deluge::gui::menu_item::HorizontalMenu audioCompMenu;
extern deluge::gui::menu_item::HorizontalMenu unisonMenu;
extern deluge::gui::menu_item::HorizontalMenu recorderMenu;
extern deluge::gui::menu_item::submenu::ActualSource source0Menu;
extern deluge::gui::menu_item::submenu::ActualSource source1Menu;
extern deluge::gui::menu_item::submenu::Modulator modulator0Menu;
extern deluge::gui::menu_item::submenu::Modulator modulator1Menu;
extern deluge::gui::menu_item::HorizontalMenu oscMixerMenu;
extern deluge::gui::menu_item::envelope::EnvelopeMenu env1Menu;
extern deluge::gui::menu_item::envelope::EnvelopeMenu env2Menu;
extern deluge::gui::menu_item::envelope::EnvelopeMenu env3Menu;
extern deluge::gui::menu_item::envelope::EnvelopeMenu env4Menu;
extern deluge::gui::menu_item::HorizontalMenu lfo1Menu;
extern deluge::gui::menu_item::HorizontalMenu lfo2Menu;
extern deluge::gui::menu_item::HorizontalMenu lfo3Menu;
extern deluge::gui::menu_item::HorizontalMenu lfo4Menu;
/// @}

/// @name The legacy items, `gui/ui/menus.cpp`'s globals.
/// @{
extern deluge::gui::menu_item::voice::PolyphonyType polyphonyMenu;
extern deluge::gui::menu_item::voice::Priority priorityMenu;
extern deluge::gui::menu_item::sidechain::Send sidechainSendMenu;
extern deluge::gui::menu_item::SynthModeSelection synthModeMenu;
extern deluge::gui::menu_item::MasterTranspose masterTransposeMenu;
extern deluge::gui::menu_item::osc::Type osc0TypeMenu;
extern deluge::gui::menu_item::osc::Type osc1TypeMenu;
extern deluge::gui::menu_item::sample::Transpose source0TransposeMenu;
extern deluge::gui::menu_item::sample::Transpose source1TransposeMenu;
extern deluge::gui::menu_item::osc::RetriggerPhase osc0PhaseMenu;
extern deluge::gui::menu_item::osc::RetriggerPhase osc1PhaseMenu;
extern deluge::gui::menu_item::sample::Reverse sample0ReverseMenu;
extern deluge::gui::menu_item::sample::Reverse sample1ReverseMenu;
extern deluge::gui::menu_item::sample::Repeat sample0RepeatMenu;
extern deluge::gui::menu_item::sample::Repeat sample1RepeatMenu;
extern deluge::gui::menu_item::sample::TimeStretch sample0TimeStretchMenu;
extern deluge::gui::menu_item::sample::TimeStretch sample1TimeStretchMenu;
extern deluge::gui::menu_item::sample::PitchSpeed sample0PitchSpeedMenu;
extern deluge::gui::menu_item::sample::PitchSpeed sample1PitchSpeedMenu;
extern deluge::gui::menu_item::sample::Interpolation sample0InterpolationMenu;
extern deluge::gui::menu_item::sample::Interpolation sample1InterpolationMenu;
extern deluge::gui::menu_item::osc::Sync oscSyncMenu;
extern deluge::gui::menu_item::lfo::Type lfo1TypeMenu;
extern deluge::gui::menu_item::lfo::Type lfo2TypeMenu;
extern deluge::gui::menu_item::lfo::Type lfo3TypeMenu;
extern deluge::gui::menu_item::lfo::Type lfo4TypeMenu;
extern deluge::gui::menu_item::lfo::Sync lfo1SyncMenu;
extern deluge::gui::menu_item::lfo::Sync lfo2SyncMenu;
extern deluge::gui::menu_item::lfo::Sync lfo3SyncMenu;
extern deluge::gui::menu_item::lfo::Sync lfo4SyncMenu;
extern deluge::gui::menu_item::modulator::Transpose modulator0TransposeMenu;
extern deluge::gui::menu_item::modulator::Transpose modulator1TransposeMenu;
extern deluge::gui::menu_item::osc::RetriggerPhase modulator0PhaseMenu;
extern deluge::gui::menu_item::osc::RetriggerPhase modulator1PhaseMenu;
extern deluge::gui::menu_item::modulator::Destination modulatorDestMenu;
extern deluge::gui::menu_item::unison::CountToStereoSpread numUnisonMenu;
extern deluge::gui::menu_item::unison::Detune unisonDetuneMenu;
extern deluge::gui::menu_item::midi::sound::OutputMidiChannel outputMidiChannelMenu;
extern deluge::gui::menu_item::midi::sound::OutputMidiNoteForDrum outputMidiNoteForDrumMenu;
extern deluge::gui::menu_item::mod_fx::Type modFXTypeMenu;
extern deluge::gui::menu_item::filter::FilterModeSelection lpfModeMenu;
extern deluge::gui::menu_item::filter::FilterModeSelection hpfModeMenu;
extern deluge::gui::menu_item::FilterRouting filterRoutingMenu;
extern deluge::gui::menu_item::fx::Clipping clippingMenu;
extern deluge::gui::menu_item::delay::PingPong delayPingPongMenu;
extern deluge::gui::menu_item::delay::Type delayTypeMenu;
extern deluge::gui::menu_item::delay::Sync delaySyncMenu;
extern deluge::gui::menu_item::sidechain::Attack sidechainAttackMenu;
extern deluge::gui::menu_item::sidechain::Release sidechainReleaseMenu;
extern deluge::gui::menu_item::sidechain::Sync sidechainSyncMenu;
extern deluge::gui::menu_item::audio_compressor::Attack compAttack;
extern deluge::gui::menu_item::audio_compressor::Release compRelease;
extern deluge::gui::menu_item::audio_compressor::Ratio compRatio;
extern deluge::gui::menu_item::audio_compressor::SideHPF compHPF;
extern deluge::gui::menu_item::audio_compressor::Blend compBlend;
extern deluge::gui::menu_item::stutter::QuantizedStutter stutterQuantizedMenu;
extern deluge::gui::menu_item::arpeggiator::Mode arpModeMenu;
extern deluge::gui::menu_item::arpeggiator::OctaveMode arpOctaveModeMenu;
extern deluge::gui::menu_item::arpeggiator::NoteMode arpNoteModeMenu;
extern deluge::gui::menu_item::arpeggiator::NoteModeForDrums arpNoteModeMenuForDrums;
extern deluge::gui::menu_item::arpeggiator::ArpMpeVelocity arpMpeVelocityMenu;
extern deluge::gui::menu_item::arpeggiator::Octaves arpOctavesMenu;
extern deluge::gui::menu_item::arpeggiator::StepRepeat arpStepRepeatMenu;
extern deluge::gui::menu_item::arpeggiator::ChordType arpChordSimulatorMenuKit;
extern deluge::gui::menu_item::arpeggiator::Sync arpSyncMenu;
extern deluge::gui::menu_item::arpeggiator::IncludeInKitArp arpIncludeInKitArpMenu;
extern deluge::gui::menu_item::arpeggiator::PresetMode arpPresetModeMenu;
extern deluge::gui::menu_item::randomizer::RandomizerLock randomizerLockMenu;
extern deluge::gui::menu_item::arpeggiator::midi_cv::Gate arpGateMenuMIDIOrCV;
extern deluge::gui::menu_item::arpeggiator::midi_cv::Rate arpRateMenuMIDIOrCV;
extern deluge::gui::menu_item::arpeggiator::midi_cv::Rhythm arpRhythmMenuMIDIOrCV;
extern deluge::gui::menu_item::arpeggiator::midi_cv::SequenceLength arpSequenceLengthMenuMIDIOrCV;
extern deluge::gui::menu_item::randomizer::midi_cv::SpreadGate spreadGateMenuMIDIOrCV;
extern deluge::gui::menu_item::randomizer::midi_cv::SpreadOctave spreadOctaveMenuMIDIOrCV;
extern deluge::gui::menu_item::randomizer::midi_cv::SpreadVelocity spreadVelocityMenuMIDIOrCV;
extern deluge::gui::menu_item::randomizer::midi_cv::RatchetAmount ratchetAmountMenuMIDIOrCV;
extern deluge::gui::menu_item::randomizer::midi_cv::RatchetProbability ratchetProbabilityMenuMIDIOrCV;
extern deluge::gui::menu_item::randomizer::midi_cv::ChordPolyphony chordPolyphonyMenuMIDIOrCV;
extern deluge::gui::menu_item::randomizer::midi_cv::ChordProbability chordProbabilityMenuMIDIOrCV;
extern deluge::gui::menu_item::randomizer::midi_cv::NoteProbability randomizerNoteProbabilityMenuMIDIOrCV;
extern deluge::gui::menu_item::randomizer::midi_cv::SwapProbability swapProbabilityMenuMIDIOrCV;
extern deluge::gui::menu_item::randomizer::midi_cv::BassProbability bassProbabilityMenuMIDIOrCV;
extern deluge::gui::menu_item::randomizer::midi_cv::GlideProbability glideProbabilityMenuMIDIOrCV;
extern deluge::gui::menu_item::audio_clip::Transpose audioClipTransposeMenu;
extern deluge::gui::menu_item::AudioInterpolation audioClipInterpolationMenu;
extern deluge::gui::menu_item::reverb::Pan reverbPanMenu;
extern deluge::gui::menu_item::sidechain::Attack reverbSidechainAttackMenu;
extern deluge::gui::menu_item::reverb::RoomSize reverbRoomSizeMenu;
extern deluge::gui::menu_item::reverb::Damping reverbDampingMenu;
extern deluge::gui::menu_item::reverb::Width reverbWidthMenu;
extern deluge::gui::menu_item::reverb::HPF reverbHPFMenu;
extern deluge::gui::menu_item::reverb::LPF reverbLPFMenu;
extern deluge::gui::menu_item::reverb::Model reverbModelMenu;
extern deluge::gui::menu_item::reverb::sidechain::Shape reverbSidechainShapeMenu;
extern deluge::gui::menu_item::reverb::sidechain::Volume reverbSidechainVolumeMenu;
extern deluge::gui::menu_item::swing::Interval swingIntervalMenu;
extern deluge::gui::menu_item::audio_clip::Reverse audioClipReverseMenu;
extern deluge::gui::menu_item::audio_clip::Attack audioClipAttackMenu;
extern deluge::gui::menu_item::sequence::Direction sequenceDirectionMenu;
extern deluge::gui::menu_item::midi::MPEYToModWheel mpeyToModWheelMenu;
extern deluge::gui::menu_item::cv::DualCVSelection cv2SourceMenu;
extern deluge::gui::menu_item::midi::AftertouchToMono midiAftertouchCollapseMenu;
extern deluge::gui::menu_item::midi::MPEToMono midiMPECollapseMenu;
extern deluge::gui::menu_item::midi::device_definition::HideUnlabeled hideUnlabeledCCMenu;
extern deluge::gui::menu_item::midi::device_definition::Linked midiDeviceLinkedMenu;
extern deluge::gui::menu_item::audio_clip::AudioSourceSelector audioSourceSelectorMenu;
extern deluge::gui::menu_item::audio_clip::SpecificSourceOutputSelector specificOutputSelectorMenu;
extern deluge::gui::menu_item::midi::Bank midiBankMenu;
extern deluge::gui::menu_item::midi::Sub midiSubMenu;
extern deluge::gui::menu_item::midi::PGM midiPGMMenu;
extern deluge::gui::menu_item::bend_range::Main mainBendRangeMenu;
extern deluge::gui::menu_item::bend_range::PerFinger perFingerBendRangeMenu;
extern deluge::gui::menu_item::bend_range::PerFinger drumBendRangeMenu;
extern deluge::gui::menu_item::sidechain::Release reverbSidechainReleaseMenu;
extern deluge::gui::menu_item::sidechain::Sync reverbSidechainSyncMenu;
/// @}

/// @name The legacy items the range, scale, MIDI device and editing-mode families drive.
/// @{
extern deluge::gui::menu_item::IntegerRange defaultTempoMenu;
extern deluge::gui::menu_item::IntegerRange defaultSwingAmountMenu;
extern deluge::gui::menu_item::KeyRange defaultKeyMenu;
extern deluge::gui::menu_item::ActiveScaleMenu defaultActiveScaleMenu;
extern deluge::gui::menu_item::ActiveScaleMenu activeScaleMenu;
extern deluge::gui::menu_item::midi::Device midiDeviceMenu;
extern deluge::gui::menu_item::midi::DefaultVelocityToLevel defaultVelocityToLevelMenu;
extern deluge::gui::menu_item::midi::DeviceSendClock device_send_clock_menu;
extern deluge::gui::menu_item::midi::DeviceReceiveClock device_receive_clock_menu;
extern deluge::gui::menu_item::midi::DeviceIsRelative device_is_relative_menu;
extern deluge::gui::menu_item::midi::DeviceSendThru device_send_thru_menu;
extern deluge::gui::menu_item::performance_session_view::EditingMode performEditorMenu;
/// @}

/// @name The legacy root menus `SoundEditor::setup` opens, and the note-row editor's.
/// @{
extern deluge::gui::menu_item::Submenu soundEditorRootMenu;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuDrum;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuMIDIOrCV;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuMidiDrum;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuGateDrum;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuAudioClip;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuSongView;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuKitGlobalFX;
extern deluge::gui::menu_item::HorizontalMenu noteRowEditorRootMenu;
/// @}

/// @name What the shortcut family reads: the per-root horizontal-menu chains and the menu groups they name, and the
///       patch-source shortcut table, all file-scope globals no header declares.
/// @{
extern deluge::vector<deluge::gui::menu_item::HorizontalMenu*> horizontalMenusChainForSound;
extern deluge::vector<deluge::gui::menu_item::HorizontalMenu*> horizontalMenusChainForKit;
extern deluge::vector<deluge::gui::menu_item::HorizontalMenu*> horizontalMenusChainForSong;
extern deluge::vector<deluge::gui::menu_item::HorizontalMenu*> horizontalMenusChainForAudioClip;
extern deluge::vector<deluge::gui::menu_item::HorizontalMenu*> horizontalMenusChainForMidiOrCv;
extern "C++" PatchSource modSourceShortcuts[2][8];
extern deluge::gui::menu_item::HorizontalMenuGroup arpMenuGroup;
extern deluge::gui::menu_item::HorizontalMenuGroup arpMenuGroupKit;
extern deluge::gui::menu_item::HorizontalMenuGroup arpMenuGroupMIDIOrCV;
extern deluge::gui::menu_item::HorizontalMenuGroup voiceMenuGroup;
extern deluge::gui::menu_item::HorizontalMenuGroup envMenuGroup;
extern deluge::gui::menu_item::HorizontalMenuGroup lfoMenuGroup;
extern deluge::gui::menu_item::HorizontalMenuGroup reverbMenuGroup;
extern deluge::gui::menu_item::HorizontalMenuGroup filtersMenuGroup;
extern deluge::gui::menu_item::HorizontalMenuGroup globalFiltersMenuGroup;
extern deluge::gui::menu_item::HorizontalMenuGroup globalReverbMenuGroup;

/// @brief How many UIs legacy's stack holds (`gui/ui/ui.cpp`), which no header declares.
extern int32_t numUIsOpen;

/// @brief Legacy's held-Shift state, which `Buttons::isShiftButtonPressed` reads and `hid/buttons.h` does not declare.
namespace Buttons {
extern bool shiftCurrentlyPressed;
extern bool shiftCurrentlyStuck;
} // namespace Buttons
/// @}

namespace {

using deluge::scenes::spec::oracle_escape;

/// @brief Point `soundEditor`'s context members at what @p ctx edits, as a legacy menu open sets them: the legacy
///        items the freezer reads resolve the sound they edit through these.
void install_legacy_context(const deluge::scenes::menu::MenuContext& ctx, bool allows_note_tails) {
	const deluge::scenes::spec::MenuEngine e = deluge::scenes::spec::menu_engine(ctx);
	soundEditor.currentSound = e.sound;
	soundEditor.currentModControllable = e.mod;
	soundEditor.currentParamManager = e.params;
	soundEditor.currentSourceIndex = ctx.source_index;
	soundEditor.currentSource = e.source;
	soundEditor.currentMultiRange = e.range;
	soundEditor.currentMultiRangeIndex = ctx.range_index;
	soundEditor.currentArpSettings = e.arp;
	soundEditor.currentSidechain = e.mod != nullptr ? &e.mod->core().sidechain() : nullptr;
	soundEditor.allowsNoteTails = allows_note_tails;
	soundEditor.setupKitGlobalFXMenu = ctx.is_kit_global_fx;
}

/// @brief The rows one family writes, in the order it asks them.
class OracleWriter {
public:
	explicit OracleWriter(std::string name) : name_(std::move(name)) {
		// The fixtures report each context they make current; legacy reads the sound it edits through `soundEditor`.
		deluge::scenes::spec::legacy_context_hook = &install_legacy_context;
	}

	void row(std::string_view path, std::string_view column, std::vector<std::string> values = {}) {
		std::string key{path};
		key += '\t';
		key += column;
		if (!keys_.insert(key).second) {
			duplicates_ += std::string{path} + " " + std::string{column} + "\n";
			return;
		}
		std::string line = oracle_escape(path) + "\t" + oracle_escape(column);
		for (const std::string& v : values) {
			line += "\t" + oracle_escape(v);
		}
		lines_.push_back(std::move(line));
	}
	void row(std::string_view path, std::string_view column, std::string value) {
		row(path, column, std::vector<std::string>{std::move(value)});
	}
	void row(std::string_view path, std::string_view column, int64_t value) {
		row(path, column, std::to_string(value));
	}

	/// @brief The keys written twice, one per line; empty when every key was written once.
	[[nodiscard]] const std::string& duplicates() const { return duplicates_; }
	[[nodiscard]] size_t size() const { return lines_.size(); }

	/// @brief Write `<MENU_ORACLE_OUT>/<name>.tsv`; false when the directory is unset or the file cannot be written.
	bool flush() const {
		const char* dir = std::getenv("MENU_ORACLE_OUT");
		if (dir == nullptr || dir[0] == '\0') {
			return false;
		}
		std::ofstream out(std::string{dir} + "/" + name_ + ".tsv");
		out << "# Reference answers of upstream main's menu, one row per answer: path, column, values "
		       "(tab-separated).\n"
		       "# A row changes only to follow main's source (git show origin/main:<path>); tests/golden/README.md "
		       "says how.\n";
		for (const std::string& line : lines_) {
			out << line << "\n";
		}
		return out.good();
	}

private:
	std::string name_;
	std::set<std::string> keys_;
	std::vector<std::string> lines_;
	std::string duplicates_;
};

/// @brief Whether this run writes @p family: `MENU_ORACLE_FAMILIES` names exactly one.
bool writes(std::string_view family) {
	const char* named = std::getenv("MENU_ORACLE_FAMILIES");
	return named != nullptr && family == named;
}

/// @brief A panel as the tables hold it: `pixels/<name>.pbm`.
bool write_pixels(std::string_view name, const std::string& pbm) {
	const char* dir = std::getenv("MENU_ORACLE_OUT");
	if (dir == nullptr) {
		return false;
	}
	std::filesystem::create_directories(std::string{dir} + "/pixels");
	std::ofstream(std::string{dir} + "/pixels/" + std::string{name} + ".pbm") << pbm;
	return true;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Helpers every family shares: the legacy-side readers and writers, and the lockstep walk.

namespace {

namespace legacy = deluge::gui::menu_item;
namespace spec = deluge::scenes::spec;
namespace menu = deluge::scenes::menu;
namespace nodes = deluge::scenes::menu::nodes;
namespace bindings = deluge::scenes::menu::bindings;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using spec::TreeEntry;

/// @brief What went wrong in the freeze itself (a state the reference could not be taken in), one per line.
std::string& freeze_problems() {
	static std::string problems;
	return problems;
}

void check(bool ok, const std::string& what) {
	if (!ok) {
		freeze_problems() += what + "\n";
	}
}

/// @brief @p items, the children of @p menu, in the order `menu` shows them.
///
/// `eq::EqMenu` reorders its four items on its first render, to bass, bass frequency, treble frequency, treble -- the
/// graph's handles left to right -- and the port's `nodes::eq` lists them in that order. Every other menu's order is
/// its constructor's.
deluge::vector<::MenuItem*> in_menu_order(::MenuItem& menu, const deluge::vector<::MenuItem*>& items) {
	deluge::vector<::MenuItem*> out = items;
	if (dynamic_cast<legacy::eq::EqMenu*>(&menu) == nullptr) {
		return out;
	}
	namespace params = deluge::modulation::params;
	constexpr std::array kOrder{params::UNPATCHED_BASS, params::UNPATCHED_BASS_FREQ, params::UNPATCHED_TREBLE_FREQ,
	                            params::UNPATCHED_TREBLE};
	const auto rank = [&](::MenuItem* item) {
		const auto p = static_cast<legacy::UnpatchedParam*>(item)->getP();
		return std::distance(kOrder.begin(), std::ranges::find(kOrder, p));
	};
	std::ranges::stable_sort(out, {}, rank);
	return out;
}

/// @brief Read a `Submenu`'s protected child list: the pointer-to-member is formed inside a derived class, which
///        [class.protected] permits.
struct SubmenuPeek : legacy::Submenu {
	static deluge::vector<::MenuItem*> items_of(legacy::Submenu& menu) {
		return in_menu_order(menu, menu.*(&SubmenuPeek::items));
	}
};

/// @brief Call a legacy item's protected `writeCurrentValue()`.
template <typename T>
struct ValuePeek : legacy::Value<T> {
	static void write(legacy::Value<T>& value) { (value.*(&ValuePeek::writeCurrentValue))(); }
};

/// @brief The legacy item's current value, read the way legacy reads it, or nothing for an item that holds none.
std::optional<int32_t> legacy_value(::MenuItem* item) {
	if (auto* v = dynamic_cast<legacy::Value<int32_t>*>(item); v != nullptr) {
		v->readCurrentValue();
		return v->getValue();
	}
	if (auto* v = dynamic_cast<legacy::Value<bool>*>(item); v != nullptr) {
		v->readCurrentValue();
		return v->getValue() ? 1 : 0;
	}
	return std::nullopt;
}

/// @brief `OLED::main` -- what every legacy `renderOLED()` draws into -- as a PBM.
std::string legacy_panel_pbm() {
	const auto& canvas = deluge::hid::display::OLED::main;
	return page_major_to_pbm(canvas.bytes().data(), OLED_MAIN_WIDTH_PIXELS, OLED_MAIN_HEIGHT_PIXELS);
}

/// @brief Drive @p item's legacy write path with @p v. False for an item that holds no value.
bool legacy_write(::MenuItem* item, int32_t v) {
	if (auto* n = dynamic_cast<legacy::Value<int32_t>*>(item); n != nullptr) {
		n->setValue(v);
		ValuePeek<int32_t>::write(*n);
		return true;
	}
	if (auto* b = dynamic_cast<legacy::Value<bool>*>(item); b != nullptr) {
		b->setValue(v != 0);
		ValuePeek<bool>::write(*b);
		return true;
	}
	return false;
}

using Pair = std::pair<TreeEntry, ::MenuItem*>;

/// @brief Walk the port's tree and the legacy tree together, pairing them level by level. False, with @p where, as soon
///        as they disagree on a shape.
bool walk_pairs(const Node& node, ::MenuItem& item, const std::string& path, const Node* parent, int32_t depth,
                std::vector<Pair>& out, std::string& where) {
	out.emplace_back(TreeEntry{path, &node, parent, depth}, &item);
	if (node.kind != menu::Kind::Submenu) {
		// A `Kind::Custom` placeholder standing in for a legacy `Submenu` would drop that whole legacy subtree.
		if (dynamic_cast<legacy::Submenu*>(&item) != nullptr) {
			where = path + ": non-submenu node stands in for legacy submenu " + std::string{item.getName()};
			return false;
		}
		return true;
	}
	auto* submenu = dynamic_cast<legacy::Submenu*>(&item);
	if (submenu == nullptr) {
		where = path + ": node is a submenu but legacy item is not: " + std::string{item.getName()};
		return false;
	}
	const auto kids = SubmenuPeek::items_of(*submenu);
	if (kids.size() != node.children.size()) {
		where = path + ": child count differs under " + std::string{item.getName()} + " (" + std::to_string(kids.size())
		        + " legacy vs " + std::to_string(node.children.size()) + " port)";
		return false;
	}
	for (size_t i = 0; i < kids.size(); i++) {
		if (node.children[i] == nullptr || kids[i] == nullptr) {
			where = path + ": null child under " + std::string{item.getName()};
			return false;
		}
		if (!walk_pairs(*node.children[i], *kids[i], path + "/" + std::to_string(i), &node, depth + 1, out, where)) {
			return false;
		}
	}
	return true;
}

bool walk(const Node& root, ::MenuItem& legacy_root, std::string_view label, std::vector<Pair>& out,
          std::string& where) {
	return walk_pairs(root, legacy_root, std::string{label}, nullptr, 0, out, where);
}

/// @brief The legacy object a root's label stands for.
::MenuItem* legacy_root_for(std::string_view label) {
	if (label == "sound") {
		return &soundEditorRootMenu;
	}
	if (label == "drum") {
		return &soundEditorRootMenuDrum;
	}
	if (label == "midi/cv") {
		return &soundEditorRootMenuMIDIOrCV;
	}
	if (label == "midi drum") {
		return &soundEditorRootMenuMidiDrum;
	}
	if (label == "gate drum") {
		return &soundEditorRootMenuGateDrum;
	}
	if (label == "audio clip") {
		return &soundEditorRootMenuAudioClip;
	}
	if (label == "performance") {
		return &soundEditorRootMenuPerformanceView;
	}
	if (label == "song") {
		return &soundEditorRootMenuSongView;
	}
	if (label == "kit global fx") {
		return &soundEditorRootMenuKitGlobalFX;
	}
	if (label == "settings") {
		return &settingsRootMenu;
	}
	// The batch-1 roots, in `nodes::kBatch1Roots` order.
	::MenuItem* const batch_1[] = {&soundFiltersMenu, &audioCompMenu,  &unisonMenu,     &recorderMenu, &source0Menu,
	                               &source1Menu,      &modulator0Menu, &modulator1Menu, &oscMixerMenu, &env1Menu,
	                               &env2Menu,         &env3Menu,       &env4Menu,       &lfo1Menu,     &lfo2Menu,
	                               &lfo3Menu,         &lfo4Menu};
	for (size_t i = 0; i < std::size(batch_1); i++) {
		if (label == spec::tables1::root_label(i)) {
			return batch_1[i];
		}
	}
	return nullptr;
}

/// @brief The node the legacy item @p item was ported to, for the menus a chain in these trees can name.
///
/// Null in, null out -- legacy's `return nullptr` is "go up". An unmapped answer is null too, so callers name the
/// legacy answer through `chain_label`, which tells the two apart.
const Node* chained_node_for(::MenuItem* item) {
	if (item == nullptr) {
		return nullptr;
	}
	if (item == &legacy::arpeggiator::arpOctaveModeToNoteModeMenu) {
		return &nodes::arp_octave_mode_to_note_mode;
	}
	if (item == &legacy::arpeggiator::arpOctaveModeToNoteModeMenuForDrums) {
		return &nodes::arp_octave_mode_to_note_mode_drums;
	}
	if (item == &legacy::arpeggiator::arpNoteModeFromOctaveModeMenu) {
		return &nodes::arp_note_mode_from_octave_mode;
	}
	if (item == &legacy::arpeggiator::arpNoteModeFromOctaveModeMenuForDrums) {
		return &nodes::arp_note_mode_from_octave_mode_drums;
	}
	if (item == &legacy::unison::stereoSpreadMenu) {
		return &nodes::unison_spread;
	}
	if (item == &legacy::voice::polyphonicVoiceCountMenu) {
		return &nodes::voice_count;
	}
	if (item == &legacy::source_selection::regularMenu) {
		return &nodes::source_selection_regular;
	}
	if (item == &legacy::source_selection::rangeMenu) {
		return &nodes::source_selection_range;
	}
	if (item == &legacy::patch_cable_strength::regularMenu) {
		return &nodes::patch_cable_strength_regular;
	}
	if (item == &legacy::patch_cable_strength::rangeMenu) {
		return &nodes::patch_cable_strength_range;
	}
	if (item == &cvSubmenu) {
		return &nodes::cv_submenu;
	}
	if (item == &gateModeMenu) {
		return &nodes::gate_mode;
	}
	if (item == &gateOffTimeMenu) {
		return &nodes::gate_off_time;
	}
	return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------
// The tree family: the answers every node of a root gives, shared by `tables_2` and `tables_3`.

/// @brief What a family decides about its fixtures, since the two specs install and exclude differently.
struct TreeKit {
	void (*install)(const MenuContext&, Song*);
	bool (*dynamic_name)(const Node&);
	bool (*write_needs_ui)(const Node&);
};

/// @brief One fixture a root's answers are recorded on: its name in the column (empty: the root's own), its context and
///        song, and the root UI legacy's two session-only items read.
struct TreeFixture {
	std::string_view name;
	MenuContext ctx;
	Song* song;
	UI* root_ui = nullptr;
	/// Whether this fixture's reads, options, learn targets and param kinds are taken on every leaf, as the sound
	/// root's spec always compared them, rather than only where the node is shown.
	bool ask_hidden = false;
	/// Whether relevance is asked of the children of a page this fixture never opens too.
	bool ask_hidden_relevance = false;
};

/// @brief A root walked against its legacy tree.
struct TreeWalk {
	std::string label;
	const Node* root;
	std::vector<Pair> pairs;
};

std::string at(std::string_view column, std::string_view fixture) {
	return fixture.empty() ? std::string{column} : std::string{column} + "@" + std::string{fixture};
}

/// @brief The family's install, then the `navigationDepth` legacy alone reads.
void freezer_install(const TreeKit& kit, const MenuContext& ctx, Song* song) {
	kit.install(ctx, song);
	soundEditor.navigationDepth = 0;
}

/// @brief The symbolic name of a chained page, whichever family names it.
std::string chain_label(const Node* node) {
	std::string name = spec::tables2::chain_name(node);
	return name.starts_with("UNMAPPED") ? spec::tables3::chain_name(node) : name;
}

/// @brief The name of what a legacy `selectButtonPress()` answered: the page it chains to, `<ascend>` for none, or the
///        legacy item's own name when the port has no node for it.
std::string legacy_chain_label(::MenuItem* answer) {
	if (answer == nullptr) {
		return "<ascend>";
	}
	const Node* node = chained_node_for(answer);
	return node == nullptr ? "UNMAPPED:" + std::string{answer->getName()} : chain_label(node);
}

/// @brief What an item answers whatever the fixture: name, title, child count, param, affect-entire, edit marking,
///        slots, range dependence. Written for every pair `walk` made, in walk order.
void emit_static(OracleWriter& out, const TreeEntry& e, ::MenuItem& item, bool dynamic_name) {
	if (!dynamic_name) {
		out.row(e.path, "name", std::string{item.getName()});
		out.row(e.path, "title", std::string{item.getTitle()});
	}
	if (e.node->kind == menu::Kind::Submenu) {
		out.row(e.path, "kids", static_cast<int64_t>(e.node->children.size()));
		return;
	}
	out.row(e.path, "ae", item.usesAffectEntire() ? 1 : 0);
	out.row(e.path, "edits", item.selectEncoderActionEditsInstrument() ? 1 : 0);
	out.row(e.path, "slots", item.getOccupiedSlots());
	out.row(e.path, "range_dep", item.isRangeDependent() ? 1 : 0);
	if (spec::param_comparable(*e.node)) {
		out.row(e.path, "param_index", static_cast<int64_t>(item.getParamIndex()));
	}
}

/// @brief What an item answers on one fixture: shown or not, its value, its options, its learn target, and its param
///        kind. Only where the node is shown on a page the fixture opens, except that the sound root's synth fixture
///        (`TreeFixture::ask_hidden`) is asked of every leaf, and relevance is asked under a page the fixture never
///        opens where `TreeFixture::ask_hidden_relevance` says so.
void emit_fixture(OracleWriter& out, const TreeKit& kit, const TreeEntry& e, ::MenuItem& item, const TreeFixture& f) {
	freezer_install(kit, f.ctx, f.song);
	const bool page_opens = e.parent == nullptr || menu::bindings::relevant(*e.parent, f.ctx);
	const bool wide = f.ask_hidden;
	if (!page_opens && !wide) {
		return; // a page this fixture never opens
	}
	if (page_opens || f.ask_hidden_relevance) {
		soundEditor.navigationDepth = static_cast<uint8_t>(e.navigation_depth());
		if (f.root_ui != nullptr) {
			setRootUILowLevel(f.root_ui);
		}
		if (!spec::tables3::recorder_on_soundless_row(e, f.ctx)) {
			out.row(e.path, at("rel", f.name),
			        item.isRelevant(spec::menu_engine(f.ctx).mod, f.ctx.source_index) ? 1 : 0);
		}
		if (f.root_ui != nullptr) {
			nullifyUIs();
		}
		soundEditor.navigationDepth = 0;
	}
	const bool shown = page_opens && menu::bindings::relevant(*e.node, f.ctx);
	if (e.node->kind == menu::Kind::Custom || (!shown && !wide)) {
		return;
	}
	// A target this fixture does not show answers nothing readable.
	const bool target_hidden = e.node->bind.target.valid() && !shown;
	if (!spec::tables3::value_is_port_owned(*e.node) && !spec::tables3::value_is_two_ended(*e.node) && !target_hidden) {
		if (const std::optional<int32_t> v = legacy_value(&item)) {
			out.row(e.path, at("read", f.name), *v);
		}
	}
	// A page main keeps only for kit rows (the drums' note mode) shows on a kit row alone.
	const bool lane_only = e.node->bind.target.shown_on == deluge::scenes::bind::Target::ShownOn::ON_LANE;
	if (auto* selection = dynamic_cast<legacy::Selection*>(&item);
	    selection != nullptr && e.node->kind == menu::Kind::Enum && !(wide && lane_only)) {
		std::vector<std::string> names;
		for (const std::string_view o : selection->getOptions(legacy::Selection::OptType::FULL)) {
			names.emplace_back(o);
		}
		out.row(e.path, at("options", f.name), std::move(names));
	}
	if (auto* learner = dynamic_cast<::MenuItemWithCCLearning*>(&item); learner != nullptr) {
		const deluge::modulation::KnobTarget thing = learner->getLearningThing();
		const auto unpatched = f.ctx.addresses.slot ? deluge::modulation::params::Kind::UNPATCHED_SOUND
		                                            : deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
		out.row(e.path, at("learn", f.name),
		        thing ? std::to_string(deluge::bridge::controlTargetOf(thing, unpatched).value) : std::string{"none"});
	}
	if (spec::menu_engine(f.ctx).clip != nullptr && spec::param_comparable(*e.node)) {
		out.row(e.path, at("param_kind", f.name), static_cast<int64_t>(item.getParamKind()));
	}
}

/// @brief The write probes on one fixture: for each of the node's three probe values, written by the item from a
///        sentinel, what the port reads and what the item reads back.
void emit_writes(OracleWriter& out, const TreeKit& kit, const TreeEntry& e, ::MenuItem& item, const TreeFixture& f) {
	if (e.node->bind.set == nullptr || kit.write_needs_ui(*e.node) || spec::tables3::value_is_port_owned(*e.node)
	    || spec::tables3::value_is_two_ended(*e.node)) {
		return;
	}
	freezer_install(kit, f.ctx, f.song);
	if (!menu::bindings::relevant(*e.node, f.ctx)) {
		return;
	}
	const menu::IntRange r = menu::bindings::range(*e.node, f.ctx);
	for (const int32_t v : spec::distinct_probe_values(r)) {
		const std::optional<int32_t> sentinel = spec::sentinel_for(r, v);
		if (!sentinel || !legacy_write(&item, *sentinel)) {
			continue;
		}
		if (!legacy_write(&item, v)) {
			continue;
		}
		out.row(
		    e.path, at("write", f.name) + "/" + std::to_string(v),
		    {std::to_string(menu::bindings::read(*e.node, f.ctx)), std::to_string(legacy_value(&item).value_or(0))});
	}
}

/// @brief Walk @p root against its legacy tree, and write the static answers.
bool begin_tree(OracleWriter& out, const TreeKit& kit, std::string_view label, const Node& root, TreeWalk& walked) {
	walked.label = std::string{label};
	walked.root = &root;
	std::string problem;
	if (!walk(root, *legacy_root_for(label), label, walked.pairs, problem)) {
		check(false, "walk: " + problem);
		return false;
	}
	for (const auto& [e, item] : walked.pairs) {
		emit_static(out, e, *item, kit.dynamic_name(*e.node));
	}
	return true;
}

/// @brief What every fixture answers, for the pairs of one walked root.
void emit_reads(OracleWriter& out, const TreeKit& kit, const TreeWalk& walked, std::span<const TreeFixture> fixtures) {
	for (const TreeFixture& f : fixtures) {
		for (const auto& [e, item] : walked.pairs) {
			emit_fixture(out, kit, e, *item, f);
		}
	}
}

/// @brief The write probes for the pairs of one walked root.
void emit_tree_writes(OracleWriter& out, const TreeKit& kit, const TreeWalk& walked,
                      std::span<const TreeFixture> fixtures) {
	for (const TreeFixture& f : fixtures) {
		for (const auto& [e, item] : walked.pairs) {
			emit_writes(out, kit, e, *item, f);
		}
	}
}

/// @brief The first path under @p walked whose node is @p node, or empty.
std::string path_of(const TreeWalk& walked, const Node& node) {
	for (const auto& [e, item] : walked.pairs) {
		if (e.node == &node) {
			return e.path;
		}
	}
	return {};
}

/// @brief The legacy item the walk paired @p node with, as @p T, or null.
template <typename T = ::MenuItem>
T* item_of(const TreeWalk& walked, const Node& node) {
	for (const auto& [e, item] : walked.pairs) {
		if (e.node == &node) {
			return dynamic_cast<T*>(item);
		}
	}
	return nullptr;
}

/// @brief A legacy `getOptions(FULL)` as a row's values.
std::vector<std::string> options_of(legacy::Selection& selection) {
	std::vector<std::string> names;
	for (const std::string_view o : selection.getOptions(legacy::Selection::OptType::FULL)) {
		names.emplace_back(o);
	}
	return names;
}

// ---------------------------------------------------------------------------------------------------------------
// Family `tables_3`: the settings root and the nine others.

namespace tables_3 {

namespace t3 = spec::tables3;

/// @brief Records `display->displayPopup(...)`, so a legacy item can be driven with no display booted: two of
///        `midi::Command`'s arms call straight through the global.
struct SpyDisplay final : deluge::hid::Display {
	std::vector<std::string> popups;

	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const* text, int8_t, bool, uint8_t, int32_t, PopupType) override {
		popups.emplace_back(text != nullptr ? text : "");
	}
	void popupText(char const* text, PopupType) override { popups.emplace_back(text != nullptr ? text : ""); }
	void popupTextTemporary(char const* text, PopupType) override { popups.emplace_back(text != nullptr ? text : ""); }
	void cancelPopup() override {}
	void freezeWithError(char const*) override {}
	void displayError(Error) override {}
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const*, bool, bool) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return !popups.empty(); }
	bool hasPopupOfType(PopupType) override { return !popups.empty(); }
	void consoleText(char const*) override {}
	void timerRoutine() override {}
};

/// @brief Installs a `SpyDisplay` and points `soundEditor`'s navigation record at @p item, so the legacy
///        `midi::Command` can be called without dereferencing a null `display`; restores both on the way out.
struct LegacyLearnScope {
	SpyDisplay spy;
	deluge::hid::Display* saved_display;
	::MenuItem* saved_item;
	uint8_t saved_depth;

	explicit LegacyLearnScope(::MenuItem* item) : saved_display(display) {
		saved_depth = soundEditor.navigationDepth;
		saved_item = soundEditor.menuItemNavigationRecord[0];
		soundEditor.navigationDepth = 0;
		soundEditor.menuItemNavigationRecord[0] = item;
		display = &spy;
	}
	~LegacyLearnScope() {
		display = saved_display;
		soundEditor.menuItemNavigationRecord[0] = saved_item;
		soundEditor.navigationDepth = saved_depth;
	}
	LegacyLearnScope(const LegacyLearnScope&) = delete;
	LegacyLearnScope& operator=(const LegacyLearnScope&) = delete;
};

const TreeKit kKit{&t3::install, &t3::has_dynamic_name, &t3::write_needs_booted_ui};

const t3::TablesRoot& settings_root() {
	return t3::roots().back();
}

/// @brief Reverb width and room size are renamed by legacy under the MUTABLE model; the port's node names are fixed
/// ids.
void emit_reverb_titles(OracleWriter& out, const std::vector<TreeWalk>& walks) {
	const deluge::effects::ReverbModel saved = AudioEngine::reverb.model();
	for (const TreeWalk& walked : walks) {
		for (const auto& [e, item] : walked.pairs) {
			if (e.node != &nodes::reverb_width && e.node != &nodes::reverb_room_size) {
				continue;
			}
			AudioEngine::setReverbModel(deluge::effects::ReverbModel::FREEVERB);
			out.row(e.path, "name@freeverb", std::string{item->getName()});
			out.row(e.path, "title@freeverb", std::string{item->getTitle()});
			AudioEngine::setReverbModel(deluge::effects::ReverbModel::MUTABLE);
			out.row(e.path, "name@mutable", std::string{item->getName()});
			out.row(e.path, "title@mutable", std::string{item->getTitle()});
		}
	}
	AudioEngine::setReverbModel(saved);
}

/// @brief The song root's two session-only items in the arranger and the automation view: one root, three root UIs.
void emit_session_only(OracleWriter& out, const TreeWalk& song_walk) {
	struct Case {
		const char* name;
		UI* root_ui;
		bool session_view_root;
		bool expected;
	};
	static spec::SongFixture song = spec::make_synth_song();
	const Case cases[] = {
	    {"arranger", &arrangerView, false, false},
	    {"automation", &automationView, false, false},
	};
	for (const Node* guarded : {&nodes::configure_song_macros, &nodes::song_midi_learn}) {
		::MenuItem* item = item_of(song_walk, *guarded);
		check(item != nullptr, "session-only item paired");
		if (item == nullptr) {
			continue;
		}
		for (const Case& c : cases) {
			const MenuContext ctx = menu::context_for(*song.song, deluge::gui::menuViewOf(nullptr), 0, false, true,
			                                          c.session_view_root, false);
			t3::install(ctx, song.song);
			setRootUILowLevel(c.root_ui);
			const bool want = item->isRelevant(spec::menu_engine(ctx).mod, ctx.source_index);
			nullifyUIs();
			check(want == c.expected, std::string{"session-only item in the "} + c.name);
			out.row(path_of(song_walk, *guarded), std::string{"rel@"} + c.name, want ? 1 : 0);
		}
	}
}

/// @brief cv::Selection and gate::Selection chain, output by output; the three chain-only pages' own shape and values.
void emit_chaining(OracleWriter& out, const TreeWalk& settings) {
	const t3::TablesRoot& root = settings_root();
	t3::install(root.ctx, root.song);
	const std::string cv_path = path_of(settings, nodes::cv_selection);
	const std::string gate_path = path_of(settings, nodes::gate_selection);
	check(!cv_path.empty() && !gate_path.empty(), "the two selectors are in the settings root");

	for (int32_t which = 0; which <= NUM_GATE_CHANNELS; which++) {
		soundEditor.currentSourceIndex = which;
		cvSelectionMenu.setValue(which);
		gateSelectionMenu.setValue(which);
		out.row(cv_path, "chain/" + std::to_string(which), legacy_chain_label(cvSelectionMenu.selectButtonPress()));
		out.row(gate_path, "chain/" + std::to_string(which), legacy_chain_label(gateSelectionMenu.selectButtonPress()));
	}

	// The three pages are children of nothing, so the lockstep walk never sees them. Their own names and titles are
	// built from runtime state (the output number), so only the shape and the values are recorded.
	const auto cv_kids = SubmenuPeek::items_of(cvSubmenu);
	out.row("chain/cv_submenu", "kids", static_cast<int64_t>(cv_kids.size()));
	for (size_t i = 0; i < cv_kids.size(); i++) {
		out.row("chain/cv_submenu/" + std::to_string(i), "name", std::string{cv_kids[i]->getName()});
	}

	for (int32_t which = 0; which < NUM_PHYSICAL_CV_CHANNELS; which++) {
		soundEditor.currentSourceIndex = which;
		cvVoltsMenu.readCurrentValue();
		out.row("chain/cv_volts", "read/" + std::to_string(which), cvVoltsMenu.getValue());
		cvTransposeMenu.readCurrentValue();
		out.row("chain/cv_transpose", "read/" + std::to_string(which), cvTransposeMenu.getValue());
		// A write goes to that output and no other.
		MenuContext ctx = root.ctx;
		ctx.source_index = static_cast<int8_t>(which);
		bindings::write(nodes::cv_volts, ctx, 100 + which);
	}
	for (int32_t which = 0; which < NUM_GATE_CHANNELS; which++) {
		soundEditor.currentSourceIndex = which;
		gateModeMenu.readCurrentValue();
		out.row("chain/gate_mode", "read/" + std::to_string(which), gateModeMenu.getValue());
		gateModeMenu.updateOptions(which);
		out.row("chain/gate_mode", "options/" + std::to_string(which), options_of(gateModeMenu));
	}
	soundEditor.currentSourceIndex = root.ctx.source_index;
	gateOffTimeMenu.readCurrentValue();
	out.row("chain/gate_off_time", "read", gateOffTimeMenu.getValue());
	out.row("chain/gate_off_time", "title", std::string{gateOffTimeMenu.getTitle()});
}

/// @brief The twelve MIDI-command learn items, against the legacy `midi::Command` in the same position.
void emit_midi_commands(OracleWriter& out, const TreeWalk& settings) {
	const t3::TablesRoot& root = settings_root();
	t3::install(root.ctx, root.song);
	const t3::CommandSlots saved = t3::CommandSlots::capture();
	const auto legacy_of = [&](size_t i) {
		return item_of<legacy::midi::Command>(settings, *nodes::kMidiCommandsChildren[i]);
	};
	check(std::size(nodes::kMidiCommandsChildren) == 12, "twelve MIDI commands");
	for (size_t i = 0; i < std::size(nodes::kMidiCommandsChildren); i++) {
		legacy::midi::Command* command = legacy_of(i);
		check(command != nullptr, "command " + std::to_string(i) + " paired");
		if (command == nullptr) {
			return;
		}
		out.row(path_of(settings, *nodes::kMidiCommandsChildren[i]), "command",
		        static_cast<int64_t>(command->commandNumber));
	}
	t3::FakeCable& cable = t3::fake_cable();
	for (size_t i = 0; i < std::size(nodes::kMidiCommandsChildren); i++) {
		legacy::midi::Command* command = legacy_of(i);
		const auto cc = static_cast<int32_t>(40 + i);
		t3::CommandSlots::clear_all();
		{
			LegacyLearnScope scope{command};
			command->learnCC(cable, 3, cc, 127);
			check(scope.spy.popups.empty(), "a CC learned on the current item pops nothing");
		}
		const std::string slot = t3::CommandSlots::capture().describe();
		check(slot != "(all clear)", "the CC learned");
		out.row("learn_cc/" + std::to_string(i), "slot", slot);
	}

	// From the list: the `Submenu` forwards a note-on and a program change to the row under its cursor.
	{
		auto* list = item_of<legacy::Submenu>(settings, nodes::midi_commands);
		check(list != nullptr, "the commands list is paired");
		const size_t play = 0;
		t3::CommandSlots::clear_all();
		{
			(void)list->focusChild(legacy_of(play));
			LegacyLearnScope scope{list};
			check(list->learnNoteOn(cable, 5, 61), "the list learns a note-on");
		}
		out.row("learn_list/0/note", "slot", t3::CommandSlots::capture().describe());
		t3::CommandSlots::clear_all();
		{
			(void)list->focusChild(legacy_of(play));
			LegacyLearnScope scope{list};
			list->learnProgramChange(cable, 2, 9);
		}
		out.row("learn_list/0/pgm", "slot", t3::CommandSlots::capture().describe());
		// The list has no `learnCC`; the row itself learns the CC (the port forwards it, an accepted difference).
		t3::CommandSlots::clear_all();
		{
			LegacyLearnScope scope{legacy_of(play)};
			legacy_of(play)->learnCC(cable, 3, 42, 127);
		}
		out.row("learn_list/0/cc", "slot", t3::CommandSlots::capture().describe());
	}

	// On its own screen: a note-on, and a program change (FILL refuses it).
	for (size_t i = 0; i < std::size(nodes::kMidiCommandsChildren); i++) {
		legacy::midi::Command* command = legacy_of(i);
		t3::CommandSlots::clear_all();
		{
			LegacyLearnScope scope{command};
			check(command->learnNoteOn(cable, 5, static_cast<int32_t>(60 + i)), "a note-on learns");
		}
		out.row("learn_own/" + std::to_string(i) + "/note", "slot", t3::CommandSlots::capture().describe());

		t3::CommandSlots::clear_all();
		const bool is_fill = command->commandNumber == GlobalMIDICommand::FILL;
		std::string pgm;
		{
			LegacyLearnScope scope{command};
			command->learnProgramChange(cable, 2, static_cast<int32_t>(7 + i));
			check(scope.spy.popups.size() == (is_fill ? 1u : 0u), "only FILL refuses a program change");
			if (is_fill && !scope.spy.popups.empty()) {
				check(scope.spy.popups[0] == deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_LEARN_PC),
				      "FILL refuses with CANT_LEARN_PC");
			}
		}
		const std::string slot = t3::CommandSlots::capture().describe();
		check((slot == "(all clear)") == is_fill, "FILL alone binds nothing");
		out.row("learn_own/" + std::to_string(i) + "/pgm", "slot", is_fill ? std::string{"refused"} : slot);
	}

	// An MPE zone, which a note-on learn from an MPE-configured port really delivers.
	for (const int32_t zone :
	     {static_cast<int32_t>(MIDI_CHANNEL_MPE_LOWER_ZONE), static_cast<int32_t>(MIDI_CHANNEL_MPE_UPPER_ZONE)}) {
		t3::CommandSlots::clear_all();
		{
			LegacyLearnScope scope{legacy_of(0)};
			check(legacy_of(0)->learnNoteOn(cable, zone, 64), "a zone learns");
		}
		out.row("learn_mpe/" + std::to_string(zone), "slot", t3::CommandSlots::capture().describe());
	}
	saved.restore();
}

/// @brief The follow channels and the kit root note, against the legacy items.
void emit_follow(OracleWriter& out, const TreeWalk& settings) {
	const t3::TablesRoot& root = settings_root();
	t3::install(root.ctx, root.song);
	const t3::FollowSaved saved;
	t3::FakeCable& cable = t3::fake_cable();
	size_t which = 0;
	for (const Node* node : nodes::kMidiFollowChannelChildren) {
		auto* item = item_of<legacy::midi::FollowChannel>(settings, *node);
		check(item != nullptr, "follow channel " + std::to_string(which) + " paired");
		if (item != nullptr) {
			for (const bool note : {true, false}) {
				midiEngine.midiFollowChannelType[which].clear();
				{
					LegacyLearnScope scope{item};
					if (note) {
						(void)item->learnNoteOn(cable, 4, 60);
					}
					else {
						item->learnCC(cable, 4, 60, 100);
					}
				}
				out.row("follow/" + std::to_string(which), note ? "learned/note" : "learned/cc",
				        t3::follow_state(which));
			}
		}
		which++;
	}
	check(which == 19, "nineteen follow channels");

	auto* kit_root = item_of<legacy::midi::FollowKitRootNote>(settings, nodes::midi_follow_kit_root_note);
	check(kit_root != nullptr, "the kit root note is paired");
	if (kit_root != nullptr) {
		midiEngine.midiFollowKitRootNote = 36;
		{
			LegacyLearnScope scope{kit_root};
			(void)kit_root->learnNoteOn(cable, 4, 61);
		}
		out.row("follow/kit_root", "learned", static_cast<int64_t>(midiEngine.midiFollowKitRootNote));
	}
}

/// @brief The MIDI device list and the two output selectors.
void emit_dynamic(OracleWriter& out) {
	const t3::TablesRoot& root = settings_root();
	t3::install(root.ctx, root.song);

	// `Devices::drawPixelsForOled` walks `lowestDeviceNum` upward through `getCable` and keeps the connected ones.
	const auto visible = [] {
		std::vector<std::string> names;
		for (int32_t idx = -3; idx < static_cast<int32_t>(MIDIDeviceManager::hostedMIDIDevices.size()); idx++) {
			MIDICable* cable = deluge::gui::menu_item::midi::devicesMenu.getCable(idx);
			if (cable != nullptr && cable->connectionFlags != 0u) {
				names.emplace_back(cable->getDisplayName());
			}
		}
		return names;
	};
	{
		t3::CableFixture cables;
		cables.set(1, 1, 1, {"ALPHA", "BETA"});
		out.row("devices/all", "list", visible());
		cables.set(1, 0, 0, {});
		out.row("devices/din_only", "list", visible());
		auto ghost = std::make_unique<MIDICableUSBHosted>();
		ghost->name = "GHOST";
		ghost->connectionFlags = 0;
		MIDIDeviceManager::hostedMIDIDevices.push_back(ghost.get());
		out.row("devices/ghost", "list", visible());
		MIDIDeviceManager::hostedMIDIDevices.clear();
	}

	out.row("cv_selection", "options", options_of(cvSelectionMenu));
	for (int32_t which = 0; which < NUM_PHYSICAL_CV_CHANNELS; which++) {
		cvSelectionMenu.setValue(which);
		out.row("cv_selection", "chain/" + std::to_string(which),
		        legacy_chain_label(cvSelectionMenu.selectButtonPress()));
	}
	out.row("gate_selection", "options", options_of(gateSelectionMenu));
	for (int32_t which = 0; which <= NUM_GATE_CHANNELS; which++) {
		gateSelectionMenu.setValue(which);
		out.row("gate_selection", "chain/" + std::to_string(which),
		        legacy_chain_label(gateSelectionMenu.selectButtonPress()));
	}
	for (int32_t which = 0; which < NUM_GATE_CHANNELS; which++) {
		gateModeMenu.updateOptions(which);
		out.row("gate_mode/" + std::to_string(which), "options", options_of(gateModeMenu));
	}
	soundEditor.currentSourceIndex = root.ctx.source_index;
}

/// @brief The pad colours' storage bytes.
void emit_colours(OracleWriter& out, const TreeWalk& settings) {
	const Node* const colours[] = {&nodes::pad_colour_active, &nodes::pad_colour_stopped, &nodes::pad_colour_muted,
	                               &nodes::pad_colour_soloed, &nodes::pad_colour_fill,    &nodes::pad_colour_once};
	uint8_t saved[FlashStorage::NUM_PAD_COLOUR_SLOTS];
	std::copy(std::begin(FlashStorage::padColours), std::end(FlashStorage::padColours), saved);
	for (size_t k = 0; k < std::size(colours); k++) {
		auto* item = item_of<legacy::Colour>(settings, *colours[k]);
		check(item != nullptr, "pad colour " + std::to_string(k) + " paired");
		if (item == nullptr) {
			continue;
		}
		// The item writes one byte of `FlashStorage::padColours`: find which, from a base the write moves off.
		std::fill(std::begin(FlashStorage::padColours), std::end(FlashStorage::padColours),
		          static_cast<uint8_t>(deluge::gui::RED));
		item->setValue(static_cast<int32_t>(deluge::gui::PINK));
		item->writeCurrentValue();
		int32_t moved = -1;
		int32_t moved_count = 0;
		for (size_t i = 0; i < FlashStorage::NUM_PAD_COLOUR_SLOTS; i++) {
			if (FlashStorage::padColours[i] != deluge::gui::RED) {
				moved = static_cast<int32_t>(i);
				moved_count++;
			}
		}
		check(moved_count == 1, "a pad colour item writes exactly one byte");
		out.row("colour/" + std::to_string(k), "storage", moved);
	}
	std::copy(std::begin(saved), std::end(saved), FlashStorage::padColours);
}

void freeze(OracleWriter& out) {
	const std::span<const t3::TablesRoot> roots = t3::roots();
	std::vector<TreeWalk> walks(roots.size());
	for (size_t i = 0; i < roots.size(); i++) {
		t3::install(roots[i].ctx, roots[i].song);
		if (!begin_tree(out, kKit, roots[i].name, *roots[i].node, walks[i])) {
			return;
		}
	}
	emit_reverb_titles(out, walks);
	for (size_t i = 0; i < roots.size(); i++) {
		const TreeFixture fixture{"", roots[i].ctx, roots[i].song, roots[i].root_ui};
		emit_reads(out, kKit, walks[i], std::span{&fixture, 1});
	}
	const auto walk_of = [&](std::string_view label) -> const TreeWalk& {
		for (const TreeWalk& w : walks) {
			if (w.label == label) {
				return w;
			}
		}
		std::abort();
	};
	emit_session_only(out, walk_of("song"));
	for (size_t i = 0; i < roots.size(); i++) {
		const TreeFixture fixture{"", roots[i].ctx, roots[i].song, roots[i].root_ui};
		emit_tree_writes(out, kKit, walks[i], std::span{&fixture, 1});
	}
	const TreeWalk& settings = walk_of("settings");
	emit_chaining(out, settings);
	emit_midi_commands(out, settings);
	emit_follow(out, settings);
	emit_dynamic(out);
	emit_colours(out, settings);
}

} // namespace tables_3

// ---------------------------------------------------------------------------------------------------------------
// Family `tables_2`: the sound root.

namespace tables_2 {

namespace t2 = spec::tables2;

const TreeKit kKit{&t2::install, &t2::has_dynamic_name, &t2::write_needs_booted_ui};

/// @brief The two fixtures the sound root's answers are recorded on, the kit row with its first drum selected.
std::vector<TreeFixture> fixtures() {
	t2::Rigged& synth = t2::fixture();
	t2::KitRig& kit = t2::kit_fixture();
	const MenuContext kit_ctx = t2::select_drum(kit, kit.drum_a);
	return {TreeFixture{"synth", synth.ctx, synth.song.song, nullptr, /*ask_hidden=*/true},
	        TreeFixture{"kit row", kit_ctx, kit.song.song}};
}

/// @brief The page a chain case is recorded under: its tree path, or `chain/<name>` for a chain-only page.
std::string chain_path(const TreeWalk& walked, const Node& node) {
	std::string path = path_of(walked, node);
	return path.empty() ? "chain/" + t2::chain_name(&node) : path;
}

/// @brief What each press of SELECT chains to, value by value, on a synth clip and on a kit row.
void emit_chaining(OracleWriter& out, const TreeWalk& walked) {
	t2::Rigged& f = t2::fixture();
	struct Case {
		const Node* node;
		::MenuItem* item;
		int32_t value;
	};
	const Case cases[] = {
	    {&nodes::arp_preset, &arpPresetModeMenu, int32_t(ArpPreset::CUSTOM)},
	    {&nodes::arp_preset, &arpPresetModeMenu, int32_t(ArpPreset::UP)},
	    {&nodes::arp_preset, &arpPresetModeMenu, int32_t(ArpPreset::OFF)},
	    {&nodes::polyphony, &polyphonyMenu, int32_t(PolyphonyMode::POLY)},
	    {&nodes::polyphony, &polyphonyMenu, int32_t(PolyphonyMode::MONO)},
	    {&nodes::polyphony, &polyphonyMenu, int32_t(PolyphonyMode::AUTO)},
	    {&nodes::unison_count, &numUnisonMenu, 1},
	    {&nodes::unison_count, &numUnisonMenu, 4},
	    {&nodes::arp_octave_mode_to_note_mode, &legacy::arpeggiator::arpOctaveModeToNoteModeMenu, 0},
	    {&nodes::arp_octave_mode_to_note_mode_drums, &legacy::arpeggiator::arpOctaveModeToNoteModeMenuForDrums, 0},
	};
	for (const Case& c : cases) {
		t2::install(f.ctx, f.song.song);
		menu::bindings::write(*c.node, f.ctx, c.value);
		// `Selection::selectButtonPress`'s fall-through arm asks whether the item is the open menu; in the port the
		// chain hook only ever runs on the focused leaf, so that is the state to put legacy in.
		soundEditor.navigationDepth = 0;
		soundEditor.menuItemNavigationRecord[0] = c.item;
		c.item->readCurrentValue();
		out.row(chain_path(walked, *c.node), "chain@synth/" + std::to_string(c.value),
		        legacy_chain_label(c.item->selectButtonPress()));
	}
	soundEditor.menuItemNavigationRecord[0] = nullptr;

	// The other half of `PresetMode::selectButtonPress`: a kit row picks the drum twin of the octave-mode page.
	t2::KitRig& k = t2::kit_fixture();
	const MenuContext ctx = t2::select_drum(k, k.drum_a);
	menu::bindings::write(nodes::arp_preset, ctx, int32_t(ArpPreset::CUSTOM));
	soundEditor.navigationDepth = 0;
	soundEditor.menuItemNavigationRecord[0] = &arpPresetModeMenu;
	arpPresetModeMenu.readCurrentValue();
	out.row(chain_path(walked, nodes::arp_preset), "chain@kit row/" + std::to_string(int32_t(ArpPreset::CUSTOM)),
	        legacy_chain_label(arpPresetModeMenu.selectButtonPress()));
	soundEditor.menuItemNavigationRecord[0] = nullptr;
	t2::install(f.ctx, f.song.song);
}

/// @brief The four chain-only arp pages answer AFFECT ENTIRE as their legacy items do; they are in no tree.
void emit_chain_only_ae(OracleWriter& out) {
	const std::pair<const Node*, ::MenuItem*> pages[] = {
	    {&nodes::arp_octave_mode_to_note_mode, &legacy::arpeggiator::arpOctaveModeToNoteModeMenu},
	    {&nodes::arp_octave_mode_to_note_mode_drums, &legacy::arpeggiator::arpOctaveModeToNoteModeMenuForDrums},
	    {&nodes::arp_note_mode_from_octave_mode, &legacy::arpeggiator::arpNoteModeFromOctaveModeMenu},
	    {&nodes::arp_note_mode_from_octave_mode_drums, &legacy::arpeggiator::arpNoteModeFromOctaveModeMenuForDrums},
	};
	for (const auto& [node, item] : pages) {
		out.row("chain/" + t2::chain_name(node), "ae", item->usesAffectEntire() ? 1 : 0);
	}
}

/// @brief A cable made the way the strength page makes one, committed through the port; the cable sets the later rows
///        index into are built in this order.
void commit_cable(const MenuContext& ctx, PatchSource source, int32_t param, int32_t value, Polarity polarity,
                  const Node* page) {
	menu::MenuState st{};
	st.depth = 0;
	st.path[0] = page;
	st.walk.patching_param = param;
	st.walk.patch_source = source;
	st.walk.patch_polarity = polarity;
	st.edit_int = value;
	nodes::kPatchCableStrengthRegularItem.commit(ctx, st);
}

/// @brief The patch-cable items: their names, where they chain, what the strength page reads and a write leaves, and
///        which strength page each stored cable opens.
void emit_patch_cables(OracleWriter& out, const TreeWalk& walked) {
	t2::Rigged& f = t2::fixture();
	namespace params = deluge::modulation::params;
	out.row("cables/patch_cables", "name", std::string{patchCablesMenu.getName()});
	out.row("cables/vibrato", "name", std::string{vibratoMenu.getName()});
	out.row("cables/sidechain_volume", "name", std::string{sidechainVolumeShortcutMenu.getName()});

	t2::install(f.ctx, f.song.song);
	soundEditor.patchingParamSelected = params::LOCAL_LPF_FREQ;
	legacy::source_selection::regularMenu.s = PatchSource::ENVELOPE_1;
	legacy::source_selection::rangeMenu.s = PatchSource::LFO_GLOBAL_1;
	check(!Buttons::isShiftButtonPressed(), "shift is not held");
	struct Case {
		const char* label;
		::MenuItem* item;
	};
	::MenuItem* lpf = item_of(walked, nodes::lpf_freq);
	check(lpf != nullptr, "lpf_freq is paired");
	const Case cases[] = {
	    {"source_selection_regular", &legacy::source_selection::regularMenu},
	    {"patch_cable_strength_regular", &legacy::patch_cable_strength::regularMenu},
	    {"source_selection_range", &legacy::source_selection::rangeMenu},
	    {"patch_cable_strength_range", &legacy::patch_cable_strength::rangeMenu},
	    {"vibrato", &vibratoMenu},
	    {"sidechain_volume", &sidechainVolumeShortcutMenu},
	    {"lpf_freq", lpf},
	};
	for (const Case& c : cases) {
		if (c.item != nullptr) {
			out.row(std::string{"cables/"} + c.label, "chain", legacy_chain_label(c.item->selectButtonPress()));
		}
	}

	// The strength page's reader, on a cable the port created and wrote.
	{
		const deluge::synth::CableTarget learn =
		    deluge::synth::CableTarget::depth_of(params::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_1);
		check(t2::seed_cable(f.ctx, learn, 1234), "the cable is created");
		legacy::patch_cable_strength::regularMenu.readCurrentValue();
		out.row("strength/seed", "read", legacy::patch_cable_strength::regularMenu.getValue());
		out.row("strength/seed", "learn",
		        std::to_string(
		            deluge::modulation::knobWord(legacy::patch_cable_strength::regularMenu.getLearningThing()).word));
		check(t2::seed_cable(f.ctx, learn, -2500), "the cable is moved");
		legacy::patch_cable_strength::regularMenu.readCurrentValue();
		out.row("strength/sentinel", "read", legacy::patch_cable_strength::regularMenu.getValue());
		menu::MenuState st{};
		st.walk.patching_param = params::LOCAL_LPF_FREQ;
		st.walk.patch_source = PatchSource::ENVELOPE_1;
		nodes::kPatchCableStrengthRegularItem.on_focus(f.ctx, st);
		st.edit_int = 4321;
		nodes::kPatchCableStrengthRegularItem.commit(f.ctx, st);
		legacy::patch_cable_strength::regularMenu.readCurrentValue();
		out.row("strength/commit", "write/4321", legacy::patch_cable_strength::regularMenu.getValue());
	}

	// The cables the other strength examples create, in their order: the row indices below follow it.
	{
		const Polarity fresh = PatchCable::getDefaultPolarity(PatchSource::ENVELOPE_3);
		commit_cable(f.ctx, PatchSource::ENVELOPE_3, params::LOCAL_HPF_RESONANCE, 2000,
		             fresh == Polarity::BIPOLAR ? Polarity::UNIPOLAR : Polarity::BIPOLAR, nullptr);
		commit_cable(f.ctx, PatchSource::LFO_GLOBAL_1, params::LOCAL_HPF_MORPH, 1200,
		             PatchCable::getDefaultPolarity(PatchSource::LFO_GLOBAL_1), &nodes::sidechain);
		check(t2::seed_cable(
		          f.ctx,
		          deluge::synth::CableTarget::depth_of(params::LOCAL_OSC_A_PITCH_ADJUST, PatchSource::LFO_GLOBAL_1),
		          1500),
		      "the pitch cable is created");
	}

	// Which strength page the selected cable needs, and the sources it installs.
	PatchCableSet* set = spec::menu_engine(f.ctx).params->getPatchCableSetAllowJibberish();
	check(t2::seed_cable(f.ctx, deluge::synth::CableTarget::depth_of(params::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_1),
	                     1000),
	      "the plain cable is created");
	check(t2::seed_cable(f.ctx,
	                     deluge::synth::CableTarget::depth_of_depth(params::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_1,
	                                                                PatchSource::LFO_GLOBAL_1),
	                     900),
	      "the ranged cable is created");
	for (int32_t i = 0; i < set->numPatchCables; i++) {
		patchCablesMenu.currentValue = i;
		const std::string page = legacy_chain_label(patchCablesMenu.selectButtonPress());
		out.row("cables/row/" + std::to_string(i), "page", page);
		std::vector<std::string> sources{std::to_string(int(soundEditor.patchingParamSelected)),
		                                 std::to_string(int(legacy::source_selection::regularMenu.s))};
		if (!set->patchCables[i].destination.is_param()) {
			sources.push_back(std::to_string(int(legacy::source_selection::rangeMenu.s)));
		}
		out.row("cables/row/" + std::to_string(i), "sources", sources);
	}
	patchCablesMenu.currentValue = set->numPatchCables;
	out.row("cables/row/end", "page", legacy_chain_label(patchCablesMenu.selectButtonPress()));

	// Shift+SELECT deletes automation exactly on the legacy `Automation` items.
	for (const auto& [e, item] : walked.pairs) {
		if (dynamic_cast<legacy::Automation*>(item) != nullptr) {
			out.row(e.path, "automation", 1);
		}
	}
}

/// @brief With AFFECT ENTIRE held on a kit row, legacy's hand-written sweeps write every drum: what each drum reads
///        after the write, from a sentinel seeded on both through legacy's own single-target write.
void emit_sweeps(OracleWriter& out, const TreeWalk& walked) {
	t2::KitRig& k = t2::kit_fixture();
	const auto observe = [&](const Node& node) {
		std::array<int32_t, 2> read{};
		read[0] = menu::bindings::read(node, t2::select_drum(k, k.drum_a));
		read[1] = menu::bindings::read(node, t2::select_drum(k, k.drum_b));
		return read;
	};
	for (const auto& [e, item] : walked.pairs) {
		if (e.node->bind.set == nullptr || t2::write_needs_booted_ui(*e.node)) {
			continue;
		}
		const menu::IntRange r = menu::bindings::range(*e.node, t2::select_drum(k, k.drum_a));
		const int32_t v = r.max;
		const std::optional<int32_t> sentinel = spec::sentinel_for(r, v);
		if (!sentinel.has_value() || !legacy_value(item).has_value()) {
			continue;
		}
		(void)t2::select_drum(k, k.drum_a);
		if (!legacy_write(item, *sentinel)) {
			continue;
		}
		(void)t2::select_drum(k, k.drum_b);
		if (!legacy_write(item, *sentinel)) {
			continue;
		}
		(void)t2::select_drum(k, k.drum_a, /*affect_entire_held=*/true); // legacy reads the sweep off `currentUIMode`
		if (!legacy_write(item, v)) {
			continue;
		}
		const std::array<int32_t, 2> after = observe(*e.node);
		out.row("sweep/" + e.path, "drum_a", after[0]);
		out.row("sweep/" + e.path, "drum_b", after[1]);
	}
	t2::install(t2::fixture().ctx, t2::fixture().song.song);
}

void freeze(OracleWriter& out) {
	t2::Rigged& f = t2::fixture();
	t2::install(f.ctx, f.song.song);
	TreeWalk walked;
	if (!begin_tree(out, kKit, "sound", nodes::sound_root, walked)) {
		return;
	}
	std::vector<TreeFixture> fx = fixtures();
	emit_reads(out, kKit, walked, fx);
	emit_chain_only_ae(out);
	emit_chaining(out, walked);
	emit_patch_cables(out, walked);
	fx = fixtures();
	emit_tree_writes(out, kKit, walked, fx);
	emit_sweeps(out, walked);
}

} // namespace tables_2

// ---------------------------------------------------------------------------------------------------------------
// Family `tables_1`: the seventeen batch-1 roots, and the note-range picker.

namespace tables_1 {

namespace t1 = spec::tables1;

bool no_booted_ui(const Node&) {
	return false;
}

const TreeKit kKit{&t1::install, &t1::has_dynamic_name, &no_booted_ui};

/// @brief Reach `Range::getText`, which is `protected`.
///
/// The same `[class.protected]` move `SubmenuPeek` makes, one class further up: `MultiRange` is `final` so nothing can
/// derive from it, but `getText` is declared on `Range` and `MultiRange` merely overrides it. A pointer-to-member
/// formed inside a class derived from `Range` therefore has type `void (Range::*)(...)`, applies to the
/// `multiRangeMenu` global, and dispatches virtually to `MultiRange::getText`.
struct RangePeek : legacy::Range {
	static std::string text_of_range(legacy::Range& r, int32_t value) {
		static_cast<legacy::MultiRange&>(r).setValue(value);
		char buf[32] = {};
		(r.*(&RangePeek::getText))(buf, nullptr, nullptr, /*mayShowJustOne=*/false); // `MultiRange::getText`
		return std::string{buf};
	}
};

/// @brief `MultiRange::currentScroll`, which is protected on a `final` class: an explicit instantiation may name it
///        (access is not checked there), and the friend it defines hands the member pointer out.
auto multi_range_scroll();
template <int32_t legacy::MultiRange::* Member>
struct MultiRangeScrollPeek {
	friend auto multi_range_scroll() { return Member; }
};
template struct MultiRangeScrollPeek<&legacy::MultiRange::currentScroll>;

/// @brief Make @p root legacy's root UI for an example, and put the old one back: `MultiRange` re-renders the root
///        after a range change (`SoundEditor::possibleChangeToCurrentRangeDisplay`), which reads it.
struct RootUiFor {
	explicit RootUiFor(RootUI* root) : previous_(getRootUI()) { setRootUILowLevel(root); }
	~RootUiFor() {
		if (previous_ != nullptr) {
			setRootUILowLevel(previous_);
		}
		else {
			nullifyUIs();
		}
	}
	RootUiFor(const RootUiFor&) = delete;
	RootUiFor& operator=(const RootUiFor&) = delete;

private:
	RootUI* previous_;
};

/// @brief The top notes of source 0's ranges, as the fixture's sound holds them.
std::vector<int32_t> tops_of(t1::RangeFixture& f) {
	const auto& ranges = static_cast<SoundInstrument*>(f.song.instrument)->sources[0].ranges;
	std::vector<int32_t> tops;
	for (int32_t i = 0; i < std::ssize(ranges); i++) {
		tops.push_back(ranges.getElement(i)->topNote);
	}
	return tops;
}

/// @brief The picker's label of every range, on a fixture with the given top notes.
void emit_labels(OracleWriter& out, std::string_view prefix, std::span<const int16_t> tops) {
	t1::RangeFixture f{static_cast<int32_t>(tops.size())};
	f.set_tops(tops);
	for (size_t i = 0; i < tops.size(); i++) {
		out.row(std::string{prefix} + "/" + std::to_string(i), "label",
		        RangePeek::text_of_range(legacy::multiRangeMenu, static_cast<int32_t>(i)));
	}
}

/// @brief Every editable edge of the four ranges turned far enough to reach every clamp: the ranges left after 130
///        turns of one note each.
void emit_edges(OracleWriter& out) {
	using deluge::ui::oled::RangeEdge;
	t1::RangeFixture f{4};
	const int16_t tops[] = {40, 41, 60, 32767};
	for (int32_t index = 0; index < 4; index++) {
		for (const RangeEdge edge : {RangeEdge::Left, RangeEdge::Right}) {
			if (!nodes::may_edit_range_edge(f.ctx, index, edge)) {
				continue;
			}
			for (const int32_t offset : {1, -1}) {
				f.set_tops(tops);
				legacy::multiRangeMenu.setValue(index);
				soundEditor.editingRangeEdge =
				    edge == RangeEdge::Left ? legacy::RangeEdit::LEFT : legacy::RangeEdit::RIGHT;
				for (int32_t k = 0; k < 130; k++) {
					legacy::multiRangeMenu.selectEncoderAction(offset);
				}
				soundEditor.editingRangeEdge = legacy::RangeEdit::OFF;
				const std::vector<int32_t> after = tops_of(f);
				out.row("picker/edge/" + std::to_string(index) + "/" + (edge == RangeEdge::Left ? "L" : "R") + "/"
				            + std::to_string(offset),
				        "range", t1::spans_of(after));
			}
		}
	}
}

/// @brief The edge a horizontal turn leaves edited, from each edge and each direction.
void emit_horizontal_edges(OracleWriter& out) {
	using deluge::ui::oled::RangeEdge;
	t1::RangeFixture f{3};
	const std::pair<RangeEdge, legacy::RangeEdit> starts[] = {{RangeEdge::Off, legacy::RangeEdit::OFF},
	                                                          {RangeEdge::Left, legacy::RangeEdit::LEFT},
	                                                          {RangeEdge::Right, legacy::RangeEdit::RIGHT}};
	const auto name_of = [](legacy::RangeEdit e) {
		return e == legacy::RangeEdit::LEFT ? "left" : e == legacy::RangeEdit::RIGHT ? "right" : "off";
	};
	for (int32_t index = 0; index < 3; index++) {
		for (const auto& [start, legacy_start] : starts) {
			for (const int32_t offset : {1, -1}) {
				legacy::multiRangeMenu.setValue(index);
				soundEditor.editingRangeEdge = legacy_start;
				legacy::multiRangeMenu.horizontalEncoderAction(offset);
				out.row("picker/hedge/" + std::to_string(index) + "/" + name_of(legacy_start) + "/"
				            + std::to_string(offset),
				        "edge", std::string{name_of(soundEditor.editingRangeEdge)});
			}
		}
	}
	soundEditor.editingRangeEdge = legacy::RangeEdit::OFF;
}

/// @brief Shift and a select turn on four ranges whose second holds a single note: the ranges left, or a refusal.
void emit_inserts(OracleWriter& out) {
	const RootUiFor root{&instrumentClipView};
	const int16_t tops[] = {40, 41, 60, 32767};
	int32_t inserts = 0;
	int32_t refusals = 0;
	for (int32_t index = 0; index < 4; index++) {
		for (const int32_t offset : {1, -1}) {
			t1::RangeFixture f{4};
			f.set_tops(tops);
			legacy::multiRangeMenu.setValue(index);
			soundEditor.setCurrentMultiRange(index);
			{
				spec::ShiftHeld shift;
				legacy::multiRangeMenu.selectEncoderAction(offset);
			}
			display->cancelPopup();
			const std::string path = "picker/insert/" + std::to_string(index) + "/" + std::to_string(offset);
			const std::vector<int32_t> after = tops_of(f);
			if (after.size() == 4) {
				refusals++;
				out.row(path, "ranges", std::string{"refused"});
				continue;
			}
			inserts++;
			out.row(path, "ranges", t1::spans_of(after));
			out.row(path, "cursor", static_cast<int64_t>(legacy::multiRangeMenu.getValue()));
		}
	}
	check(inserts == 6 && refusals == 2, "six ranges inserted and two refused");
}

/// @brief A range deleted from four, at each index, and from one.
void emit_deletes(OracleWriter& out) {
	const RootUiFor root{&instrumentClipView};
	const int16_t tops[] = {40, 50, 60, 32767};
	for (int32_t index = 0; index < 4; index++) {
		t1::RangeFixture f{4};
		f.set_tops(tops);
		legacy::multiRangeMenu.setValue(index);
		soundEditor.setCurrentMultiRange(index);
		legacy::multiRangeMenu.deletePress();
		display->cancelPopup();
		const std::vector<int32_t> after = tops_of(f);
		check(after.size() == 3, "a delete from four leaves three");
		const std::string path = "picker/delete/4/" + std::to_string(index);
		out.row(path, "ranges", t1::spans_of(after));
		out.row(path, "cursor", static_cast<int64_t>(legacy::multiRangeMenu.getValue()));
	}
	t1::RangeFixture last{1};
	legacy::multiRangeMenu.setValue(0);
	soundEditor.setCurrentMultiRange(0);
	legacy::multiRangeMenu.deletePress();
	display->cancelPopup();
	check(tops_of(last).size() == 1, "the last range is not deleted");
	out.row("picker/delete/1/0", "ranges", std::string{"refused"});
}

/// @brief The window's scroll on opening, after flicks, and after an insert before the current range.
void emit_scrolls(OracleWriter& out) {
	const RootUiFor root{&instrumentClipView};
	const int16_t tops[] = {20, 30, 40, 50, 60, 32767};
	t1::RangeFixture f{6};
	f.set_tops(tops);
	static_cast<SoundInstrument*>(f.song.instrument)->sources[0].defaultRangeI = 4;
	soundEditor.currentMultiRange = nullptr;
	const auto scroll = [&](std::string_view name) {
		out.row("picker/scroll/" + std::string{name}, "scroll",
		        static_cast<int64_t>(legacy::multiRangeMenu.*multi_range_scroll()));
	};
	legacy::multiRangeMenu.beginSession(nullptr);
	scroll("open");
	legacy::multiRangeMenu.selectEncoderAction(-1);
	scroll("down1");
	legacy::multiRangeMenu.selectEncoderAction(-1);
	legacy::multiRangeMenu.selectEncoderAction(-1);
	scroll("down3");
	legacy::multiRangeMenu.selectEncoderAction(1);
	scroll("up1");
	{
		spec::ShiftHeld shift;
		legacy::multiRangeMenu.selectEncoderAction(-1);
	}
	display->cancelPopup();
	scroll("insert_before");
}

void emit_picker(OracleWriter& out) {
	const int16_t spread[] = {47, 71, 32767};
	emit_labels(out, "picker/r3", spread);
	const int16_t below_c_minus_2[] = {-14, -1, 32767};
	emit_labels(out, "picker/low", below_c_minus_2);
	emit_edges(out);
	emit_horizontal_edges(out);
	emit_inserts(out);
	emit_deletes(out);
	emit_scrolls(out);
}

void freeze(OracleWriter& out) {
	t1::Fixture& f = t1::fixture();
	const std::span<const Node* const> roots = nodes::kBatch1Roots;
	std::vector<TreeWalk> walks(roots.size());
	for (size_t i = 0; i < roots.size(); i++) {
		if (!begin_tree(out, kKit, t1::root_label(i), *roots[i], walks[i])) {
			return;
		}
	}
	const TreeFixture fixture{"", f.ctx, f.song.song, nullptr, /*ask_hidden=*/true, /*ask_hidden_relevance=*/true};
	for (const TreeWalk& walked : walks) {
		emit_reads(out, kKit, walked, std::span{&fixture, 1});
	}
	for (const TreeWalk& walked : walks) {
		emit_tree_writes(out, kKit, walked, std::span{&fixture, 1});
	}
	emit_picker(out);
}

} // namespace tables_1

// ---------------------------------------------------------------------------------------------------------------
// Family `descriptors`: the setting rows' ranges, reads and writes, where each shows, its names and its node pairing.

namespace descriptors {

/// @brief A case and the legacy item that shows it.
struct CaseItem {
	std::string_view path; ///< the case's row path, checked against the case it is paired with
	::MenuItem* item;
};

/// @brief `Number::getMinValue` and `getMaxValue` are protected; named through a derived class ([class.protected]).
struct NumberPeek : legacy::Number {
	static Range range_of(const legacy::Number& number) {
		return {(number.*(&NumberPeek::getMinValue))(), (number.*(&NumberPeek::getMaxValue))()};
	}
};

/// @brief The item's menu range: a number's least and greatest value, an enumeration's options, a toggle's two.
Range legacy_range(::MenuItem* item) {
	if (auto* number = dynamic_cast<legacy::Number*>(item); number != nullptr) {
		return NumberPeek::range_of(*number);
	}
	if (auto* list = dynamic_cast<legacy::Enumeration*>(item); list != nullptr) {
		return {0, static_cast<int32_t>(list->size()) - 1};
	}
	if (dynamic_cast<legacy::Value<bool>*>(item) != nullptr) {
		return {0, 1};
	}
	return {std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min()};
}

/// @brief The item's value after its `readCurrentValue`, or the least `int32_t` for an item that holds none.
int32_t legacy_read(::MenuItem* item) {
	return legacy_value(item).value_or(std::numeric_limits<int32_t>::min());
}

/// @brief The legacy item each case of @p cases shows, in the order the case list holds them.
void pair_items(std::map<const Case*, ::MenuItem*>& out, const std::vector<Case>& cases,
                std::initializer_list<CaseItem> items) {
	check(cases.size() == items.size(),
	      "descriptors: " + std::to_string(cases.size()) + " cases, " + std::to_string(items.size()) + " items");
	size_t i = 0;
	for (const CaseItem& item : items) {
		if (i >= cases.size()) {
			break;
		}
		check(cases[i].path == item.path,
		      "descriptors: case " + std::string{cases[i].path} + " paired with " + std::string{item.path});
		out[&cases[i++]] = item.item;
	}
}

/// @brief The case→item table: every case of every list, with the legacy item that shows it.
const std::map<const Case*, ::MenuItem*>& kCaseItems() {
	static const std::map<const Case*, ::MenuItem*> table = [] {
		std::map<const Case*, ::MenuItem*> out;
		pair_items(out, voice_cases(),
		           {
		               {"polyphonic", &polyphonyMenu},
		               {"polyphonic", &polyphonyMenu},
		               {"voicePriority", &priorityMenu},
		               {"maxVoices", &legacy::voice::polyphonicVoiceCountMenu},
		               {"sideChainSend", &sidechainSendMenu},
		               {"mode", &synthModeMenu},
		               {"transpose", &masterTransposeMenu},
		           });
		pair_items(out, oscillator_cases(),
		           {
		               {"osc1/type", &osc0TypeMenu},
		               {"osc2/type", &osc1TypeMenu},
		               {"osc1/type", &osc0TypeMenu},
		               {"osc1/type", &osc0TypeMenu},
		               {"osc1/transpose", &source0TransposeMenu},
		               {"osc2/transpose", &source1TransposeMenu},
		               {"osc1/retrigPhase", &osc0PhaseMenu},
		               {"osc2/retrigPhase", &osc1PhaseMenu},
		               {"osc1/loopMode", &sample0RepeatMenu},
		               {"osc2/loopMode", &sample1RepeatMenu},
		               {"osc1/reversed", &sample0ReverseMenu},
		               {"osc2/reversed", &sample1ReverseMenu},
		               {"osc1/timeStretchEnable", &sample0PitchSpeedMenu},
		               {"osc2/timeStretchEnable", &sample1PitchSpeedMenu},
		               {"osc1/timeStretchAmount", &sample0TimeStretchMenu},
		               {"osc2/timeStretchAmount", &sample1TimeStretchMenu},
		               {"osc1/linearInterpolation", &sample0InterpolationMenu},
		               {"osc2/linearInterpolation", &sample1InterpolationMenu},
		               {"osc2/oscillatorSync", &oscSyncMenu},
		           });
		pair_items(out, lfo_and_modulator_cases(),
		           {
		               {"lfo1/type", &lfo1TypeMenu},
		               {"lfo2/type", &lfo2TypeMenu},
		               {"lfo3/type", &lfo3TypeMenu},
		               {"lfo4/type", &lfo4TypeMenu},
		               {"lfo1/syncLevel", &lfo1SyncMenu},
		               {"lfo2/syncLevel", &lfo2SyncMenu},
		               {"lfo3/syncLevel", &lfo3SyncMenu},
		               {"lfo4/syncLevel", &lfo4SyncMenu},
		               {"modulator1/transpose", &modulator0TransposeMenu},
		               {"modulator2/transpose", &modulator1TransposeMenu},
		               {"modulator1/retrigPhase", &modulator0PhaseMenu},
		               {"modulator2/retrigPhase", &modulator1PhaseMenu},
		               {"modulator2/toModulator1", &modulatorDestMenu},
		           });
		pair_items(out, unison_and_midi_cases(),
		           {
		               {"unison/num", &numUnisonMenu},
		               {"unison/detune", &unisonDetuneMenu},
		               {"unison/spread", &legacy::unison::stereoSpreadMenu},
		               {"midiOutput/channel", &outputMidiChannelMenu},
		               {"midiOutput/noteForDrum", &outputMidiNoteForDrumMenu},
		           });
		pair_items(out, effect_cases(),
		           {
		               {"modFXType", &modFXTypeMenu},
		               {"lpfMode", &lpfModeMenu},
		               {"hpfMode", &hpfModeMenu},
		               {"filterRoute", &filterRoutingMenu},
		               {"clippingAmount", &clippingMenu},
		               {"delay/pingPong", &delayPingPongMenu},
		               {"delay/type", &delayTypeMenu},
		               {"delay/syncLevel", &delaySyncMenu},
		               {"sidechain/attack", &sidechainAttackMenu},
		               {"sidechain/release", &sidechainReleaseMenu},
		               {"sidechain/syncLevel", &sidechainSyncMenu},
		               {"audioCompressor/attack", &compAttack},
		               {"audioCompressor/release", &compRelease},
		               {"audioCompressor/ratio", &compRatio},
		               {"audioCompressor/compHPF", &compHPF},
		               {"audioCompressor/compBlend", &compBlend},
		               {"stutter/quantized", &stutterQuantizedMenu},
		           });
		pair_items(out, arp_cases(),
		           {
		               {"arpeggiator/arpMode", &arpModeMenu},
		               {"arpeggiator/octaveMode", &arpOctaveModeMenu},
		               {"arpeggiator/noteMode", &arpNoteModeMenu},
		               {"arpeggiator/noteMode", &arpNoteModeMenuForDrums},
		               {"arpeggiator/mpeVelocity", &arpMpeVelocityMenu},
		               {"arpeggiator/numOctaves", &arpOctavesMenu},
		               {"arpeggiator/stepRepeat", &arpStepRepeatMenu},
		               {"arpeggiator/chordType", &arpChordSimulatorMenuKit},
		               {"arpeggiator/syncLevel", &arpSyncMenu},
		               {"arpeggiator/randomizerLock", &randomizerLockMenu},
		               {"arpeggiator/kitArp", &arpIncludeInKitArpMenu},
		               {"arpeggiator/preset", &arpPresetModeMenu},
		           });
		pair_items(out, arp_host_cases(),
		           {
		               {"arpeggiator/gate", &arpGateMenuMIDIOrCV},
		               {"arpeggiator/rate", &arpRateMenuMIDIOrCV},
		               {"arpeggiator/rhythm", &arpRhythmMenuMIDIOrCV},
		               {"arpeggiator/sequenceLength", &arpSequenceLengthMenuMIDIOrCV},
		               {"arpeggiator/chordPolyphony", &chordPolyphonyMenuMIDIOrCV},
		               {"arpeggiator/ratchetAmount", &ratchetAmountMenuMIDIOrCV},
		               {"arpeggiator/noteProbability", &randomizerNoteProbabilityMenuMIDIOrCV},
		               {"arpeggiator/bassProbability", &bassProbabilityMenuMIDIOrCV},
		               {"arpeggiator/swapProbability", &swapProbabilityMenuMIDIOrCV},
		               {"arpeggiator/glideProbability", &glideProbabilityMenuMIDIOrCV},
		               {"arpeggiator/chordProbability", &chordProbabilityMenuMIDIOrCV},
		               {"arpeggiator/ratchetProbability", &ratchetProbabilityMenuMIDIOrCV},
		               {"arpeggiator/spreadVelocity", &spreadVelocityMenuMIDIOrCV},
		               {"arpeggiator/spreadGate", &spreadGateMenuMIDIOrCV},
		               {"arpeggiator/spreadOctave", &spreadOctaveMenuMIDIOrCV},
		           });
		pair_items(out, model_cases(),
		           {
		               {"cv2Source", &cv2SourceMenu},
		               {"transpose", &audioClipTransposeMenu},
		               {"linearInterpolation", &audioClipInterpolationMenu},
		               {"reverb/roomSize", &reverbRoomSizeMenu},
		               {"reverb/dampening", &reverbDampingMenu},
		               {"reverb/width", &reverbWidthMenu},
		               {"reverb/hpf", &reverbHPFMenu},
		               {"reverb/lpf", &reverbLPFMenu},
		               {"reverb/pan", &reverbPanMenu},
		               {"reverb/model", &reverbModelMenu},
		               {"reverb/compressor/shape", &reverbSidechainShapeMenu},
		               {"swingInterval", &swingIntervalMenu},
		               {"priority", &priorityMenu},
		               {"reversed", &audioClipReverseMenu},
		               {"pitchSpeedIndependent", &sample0PitchSpeedMenu},
		               {"attack", &audioClipAttackMenu},
		               {"midiBank", &midiBankMenu},
		               {"midiSub", &midiSubMenu},
		               {"midiPGM", &midiPGMMenu},
		               {"bendRange", &mainBendRangeMenu},
		               {"bendRangeMPE", &perFingerBendRangeMenu},
		               {"reverb/compressor/attack", &reverbSidechainAttackMenu},
		               {"reverb/compressor/release", &reverbSidechainReleaseMenu},
		           });
		pair_items(out, scaled_cases(),
		           {
		               {"reverb/compressor/volume", &reverbSidechainVolumeMenu},
		           });
		pair_items(out, group_cases(),
		           {
		               {"sequenceDirection", &sequenceDirectionMenu},
		               {"sequenceDirection", &sequenceDirectionMenu},
		               {"bendRange", &drumBendRangeMenu},
		               {"outputMpeY", &mpeyToModWheelMenu},
		               {"collapseAftertouch", &midiAftertouchCollapseMenu},
		               {"collapseMpe", &midiMPECollapseMenu},
		               {"hideUnlabeledCC", &hideUnlabeledCCMenu},
		               {"deviceDefinitionFileName", &midiDeviceLinkedMenu},
		               {"mode", &audioSourceSelectorMenu},
		               {"inputChannel", &audioSourceSelectorMenu},
		               {"outputRecordingFrom", &specificOutputSelectorMenu},
		           });
		pair_items(out, applicability_cases(), {{"reverb/compressor/syncLevel", &reverbSidechainSyncMenu}});
		return out;
	}();
	return table;
}

::MenuItem* item_of(const Case& c) {
	const auto found = kCaseItems().find(&c);
	check(found != kCaseItems().end(), "descriptors: no item for " + case_key(c));
	return found == kCaseItems().end() ? nullptr : found->second;
}

/// @brief The cases whose items are compared for where they show: every case with a name, and the applicability-only
/// ones.
std::vector<const Case*> shown_cases() {
	std::vector<const Case*> cases = all_name_cases();
	for (const Case& c : applicability_cases()) {
		cases.push_back(&c);
	}
	return cases;
}

/// @brief The legacy items that show @p family's row @p path.
std::vector<const ::MenuItem*> items_for(Family family, std::string_view path) {
	std::vector<const ::MenuItem*> out;
	for (const Case* c : shown_cases()) {
		if (c->family == family && c->path == path) {
			out.push_back(item_of(*c));
		}
	}
	return out;
}

// ---- Ranges, reads and writes.

/// @brief A value as a row holds it.
std::string num(int64_t value) {
	return std::to_string(value);
}

/// @brief A numeric case: the item's range, its reads at the stored probes and its writes at its menu probes.
/// @param variant The column suffix of a state the rows are also taken in (`@plugged`).
void freeze_numeric(OracleWriter& out, const Case& c, ::MenuItem* item, std::string_view variant) {
	const MenuContext& ctx = enter(c.where, c.source);
	const SettingInfo& row = row_of(c);
	const std::string key = case_key(c);
	const auto column = [&](const std::string& base) { return base + std::string{variant}; };
	const Range range = legacy_range(item);
	out.row(key, column("range"), std::vector<std::string>{num(range.first), num(range.second)});

	const SettingInfo* partner = partner_of(c, row);
	std::map<int64_t, int32_t> read;
	const auto record_read = [&](int64_t at) {
		const int32_t shown = legacy_read(item);
		const auto [held, fresh] = read.emplace(at, shown);
		check(held->second == shown,
		      "descriptors: " + key + " reads " + num(held->second) + " and " + num(shown) + " at " + num(at));
		if (fresh) {
			out.row(key, column("read/" + num(at)), shown);
		}
	};
	if (partner == nullptr) {
		const auto [least, greatest] = deluge::plugin::stored_bounds(row);
		std::vector<int32_t> probes{least, greatest, row.default_value};
		probes.insert(probes.end(), c.stored.begin(), c.stored.end());
		for (const int32_t stored : probes) {
			set_stored(c.family, ctx, c.where, row, stored);
			const std::optional<int32_t> now = get_stored(c.family, ctx, c.where, row);
			record_read(c.lossy ? now.value_or(-99999) : stored);
		}
	}
	else {
		for (const auto& [stored, partner_stored] : c.pairs) {
			set_stored(c.family, ctx, c.where, row, stored);
			set_stored(c.family, ctx, c.where, *partner, partner_stored);
			record_read(int64_t{stored} * 1000 + partner_stored);
		}
	}

	if (!c.writes) {
		return;
	}
	std::set<int32_t> written;
	for (const int32_t menu : menu_probes(range)) {
		if (std::ranges::find(c.skip_writes, menu) != c.skip_writes.end() || !written.insert(menu).second) {
			continue;
		}
		legacy_write(item, menu);
		std::vector<std::string> stored{num(get_stored(c.family, ctx, c.where, row).value_or(-99999))};
		if (partner != nullptr) {
			stored.push_back(num(get_stored(c.family, ctx, c.where, *partner).value_or(-99999)));
		}
		out.row(key, column("write/" + num(menu)), std::move(stored));
		if (const auto want = from_menu(row, menu); c.lossy && want) {
			set_stored(c.family, ctx, c.where, row, want->first);
		}
	}
}

/// @brief A CHOICE case: the item's range, what it reads at each applicable choice, and what it stores at each
/// position.
void freeze_choices(OracleWriter& out, const Case& c, ::MenuItem* item, std::string_view variant) {
	const MenuContext& ctx = enter(c.where, c.source);
	const SettingInfo& row = row_of(c);
	const std::string key = case_key(c);
	const auto column = [&](const std::string& base) { return base + std::string{variant}; };
	const std::vector<int32_t> choices = applicable(row, context_of(c.where, c.family), ctx, c.where);
	const Range range = legacy_range(item);
	out.row(key, column("range"), std::vector<std::string>{num(range.first), num(range.second)});
	for (const int32_t choice : choices) {
		set_stored(c.family, ctx, c.where, row, choice);
		out.row(key, column("read/" + num(choice)), legacy_read(item));
	}
	if (!c.writes) {
		return;
	}
	for (size_t position = 0; position < choices.size() && static_cast<int32_t>(position) <= range.second; ++position) {
		legacy_write(item, static_cast<int32_t>(position));
		out.row(key, column("write/" + num(static_cast<int64_t>(position))),
		        num(get_stored(c.family, ctx, c.where, row).value_or(-1)));
	}
}

/// @brief A case whose item shows the stored value in steps: its reads at the stored probes and its writes.
void freeze_scaled(OracleWriter& out, const Case& c, ::MenuItem* item) {
	const MenuContext& ctx = enter(c.where, c.source);
	const SettingInfo& row = row_of(c);
	const std::string key = case_key(c);
	const int32_t step = c.menu_step;
	const Range range = legacy_range(item);
	out.row(key, "range", std::vector<std::string>{num(range.first), num(range.second)});
	std::vector<int32_t> probes{-step, -1, 0, step - 1, step, 5 * step + 1000, 50 * step, 51 * step - 1};
	probes.insert(probes.end(), c.stored.begin(), c.stored.end());
	std::set<int32_t> read;
	for (const int32_t stored : probes) {
		set_stored(c.family, ctx, c.where, row, stored);
		if (read.insert(stored).second) {
			out.row(key, "read/" + num(stored), legacy_read(item));
		}
	}
	std::set<int32_t> written;
	for (const int32_t menu : menu_probes(range)) {
		if (!written.insert(menu).second) {
			continue;
		}
		legacy_write(item, menu);
		out.row(key, "write/" + num(menu), num(get_stored(c.family, ctx, c.where, row).value_or(-99999)));
	}
}

void freeze_case(OracleWriter& out, const Case& c, std::string_view variant = {}) {
	::MenuItem* item = item_of(c);
	const SettingInfo& row = row_of(c);
	if (item == nullptr) {
		return;
	}
	if (row.kind == SettingKind::CHOICE
	    && deluge::plugin::mapping_of(row).kind == deluge::plugin::DisplayMappingKind::LINEAR) {
		freeze_choices(out, c, item, variant);
	}
	else {
		freeze_numeric(out, c, item, variant);
	}
}

void freeze_cases(OracleWriter& out, const std::vector<Case>& cases) {
	AudioEngine::micPluggedIn = false;
	AudioEngine::lineInPluggedIn = false;
	for (const Case& c : cases) {
		freeze_case(out, c);
	}
}

// ---- Where each row shows.

/// @brief Whether @p item is a case's item or a submenu with one under it.
bool leads_to_a_case(const ::MenuItem* item) {
	static const std::set<const ::MenuItem*> cases = [] {
		std::set<const ::MenuItem*> out;
		for (const Case* c : shown_cases()) {
			out.insert(item_of(*c));
		}
		return out;
	}();
	if (cases.contains(item)) {
		return true;
	}
	auto* submenu = dynamic_cast<legacy::Submenu*>(const_cast<::MenuItem*>(item));
	return submenu != nullptr && std::ranges::any_of(SubmenuPeek::items_of(*submenu), leads_to_a_case);
}

/// @brief Every case item under @p menu that legacy shows, and the submenus over them: relevant, under submenus that
/// are
///        all relevant. A submenu at depth @p depth lists its items with `soundEditor.navigationDepth` at that depth,
///        as the sound editor does. Only the way to a case's item is asked: legacy asks an item when its submenu is
///        open, and the rest (a MIDI row's actions, say) is no row's.
void walk_shown(::MenuItem* menu, uint8_t depth, std::set<const ::MenuItem*>& shown) {
	auto* submenu = dynamic_cast<legacy::Submenu*>(menu);
	if (submenu == nullptr) {
		return;
	}
	for (::MenuItem* item : SubmenuPeek::items_of(*submenu)) {
		soundEditor.navigationDepth = depth;
		if (item == nullptr || !leads_to_a_case(item)
		    || !item->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
			continue;
		}
		shown.insert(item);
		walk_shown(item, depth + 1, shown);
	}
}

/// @brief The root `SoundEditor::setup` opens for @p ctx.
::MenuItem* root_opened_for(const MenuContext& ctx) {
	using Root = MenuContext::Root;
	switch (ctx.root) {
	case Root::Sound:
		return &soundEditorRootMenu;
	case Root::Drum:
		return &soundEditorRootMenuDrum;
	case Root::MidiOrCv:
		return &soundEditorRootMenuMIDIOrCV;
	case Root::MidiDrum:
		return &soundEditorRootMenuMidiDrum;
	case Root::GateDrum:
		return &soundEditorRootMenuGateDrum;
	case Root::AudioClip:
		return &soundEditorRootMenuAudioClip;
	case Root::Song:
		return &soundEditorRootMenuSongView;
	case Root::KitGlobalFx:
		return &soundEditorRootMenuKitGlobalFX;
	default:
		return nullptr;
	}
}

/// @brief For each fixture, family and row with an item, whether legacy shows one of the row's items.
void freeze_applicability(OracleWriter& out) {
	AudioEngine::micPluggedIn = false;
	AudioEngine::lineInPluggedIn = false;
	for (const Fixture& fixture : fixtures()) {
		enter(fixture.where);
		fixture.setup(settle(fixture.where));
		const MenuContext& ctx = settle(fixture.where);
		std::set<const ::MenuItem*> shown;
		walk_shown(root_opened_for(ctx), 0, shown);
		if (std::ranges::find(fixture.families, Family::NOTE_ROW) != fixture.families.end()) {
			walk_shown(&noteRowEditorRootMenu, 0, shown);
		}
		for (const Family family : fixture.families) {
			for (const SettingInfo& row : table_of(family)) {
				const std::vector<const ::MenuItem*> items = items_for(family, row.path);
				if (items.empty()) {
					continue;
				}
				const bool legacy_shows =
				    std::ranges::any_of(items, [&](const ::MenuItem* item) { return shown.contains(item); });
				out.row("applies/" + std::string{fixture.name} + "/" + std::string{name_of(family)} + "/"
				            + std::string{row.path},
				        "legacy_shows", legacy_shows ? 1 : 0);
			}
		}
		unload_files(ctx);
	}
}

// ---- The named exceptions.

/// @brief What each named exception's item answers, for the port half of the exception to be measured against.
void freeze_exceptions(OracleWriter& out) {
	{ // An unset note shows as the note it plays.
		const MenuContext& ctx = enter(Where::KIT_LANE);
		Sound& sound = *spec::menu_engine(ctx).sound;
		const uint8_t before = sound.outputMidiNoteForDrum;
		sound.outputMidiNoteForDrum = MIDI_NOTE_NONE;
		out.row("exception/midiOutput/noteForDrum", "probe", legacy_read(&outputMidiNoteForDrumMenu));
		sound.outputMidiNoteForDrum = before;
	}
	{ // A stored right or stereo input, with no input plugged in.
		const bool mic = AudioEngine::micPluggedIn;
		const bool line_in = AudioEngine::lineInPluggedIn;
		AudioEngine::micPluggedIn = false;
		AudioEngine::lineInPluggedIn = false;
		struct Input {
			std::string_view path;
			int8_t source;
			::MenuItem* item;
		};
		for (const Input& input : {Input{"osc1/type", 0, &osc0TypeMenu}, Input{"osc2/type", 1, &osc1TypeMenu}}) {
			const MenuContext& ctx = enter(Where::SYNTH, input.source);
			const SettingInfo& row = table_row(Family::INSTRUMENT, input.path);
			const std::optional<int32_t> before = get_stored(Family::INSTRUMENT, ctx, Where::SYNTH, row);
			std::vector<std::string> reads;
			for (const OscType type : {OscType::INPUT_R, OscType::INPUT_STEREO}) {
				set_stored(Family::INSTRUMENT, ctx, Where::SYNTH, row, std::to_underlying(type));
				reads.push_back(num(legacy_read(input.item)));
			}
			if (before) {
				set_stored(Family::INSTRUMENT, ctx, Where::SYNTH, row, *before);
			}
			out.row("exception/" + std::string{input.path}, "probe", std::move(reads));
		}
		AudioEngine::micPluggedIn = mic;
		AudioEngine::lineInPluggedIn = line_in;
	}
	{ // The top step of the compressor shape, and what the item stores for it.
		const MenuContext& ctx = enter(Where::SONG);
		const SettingInfo& row = table_row(Family::SONG, "reverb/compressor/shape");
		const std::optional<int32_t> before = get_stored(Family::SONG, ctx, Where::SONG, row);
		const int32_t top = legacy_range(&reverbSidechainShapeMenu).second;
		legacy_write(&reverbSidechainShapeMenu, top);
		const std::optional<int32_t> stored = get_stored(Family::SONG, ctx, Where::SONG, row);
		if (before) {
			set_stored(Family::SONG, ctx, Where::SONG, row, *before);
		}
		out.row("exception/reverb/compressor/shape", "probe",
		        std::vector<std::string>{num(top), stored ? num(*stored) : std::string{"none"}});
	}
	// The item's least value, which its encoder never lands on.
	out.row("exception/swingInterval", "probe", legacy_range(&swingIntervalMenu).first);
	{ // The stored volume the item truncates.
		const MenuContext& ctx = enter(Where::SONG);
		const SettingInfo& row = table_row(Family::SONG, "reverb/compressor/volume");
		const std::optional<int32_t> before = get_stored(Family::SONG, ctx, Where::SONG, row);
		set_stored(Family::SONG, ctx, Where::SONG, row, 5 * kDuckingVolumeStep + 1000);
		out.row("exception/reverb/compressor/volume", "probe", legacy_read(&reverbSidechainVolumeMenu));
		if (before) {
			set_stored(Family::SONG, ctx, Where::SONG, row, *before);
		}
	}
}

// ---- The names.

/// @brief A legacy `getOptions` as a row's values.
std::vector<std::string> options_in(legacy::Selection& selection, legacy::Selection::OptType type) {
	std::vector<std::string> names;
	for (const std::string_view o : selection.getOptions(type)) {
		names.emplace_back(o);
	}
	return names;
}

/// @brief One case's title, name and, for a list, its options (full and short) in the context the case enters.
void freeze_names(OracleWriter& out, const Case& c, std::string_view variant = {}) {
	::MenuItem* item = item_of(c);
	if (item == nullptr) {
		return;
	}
	enter(c.where, c.source);
	const std::string key = case_key(c);
	const auto column = [&](const std::string& base) { return base + std::string{variant}; };
	out.row(key, column("title"), std::string{item->getTitle()});
	out.row(key, column("name"), std::string{item->getName()});
	if (auto* list = dynamic_cast<legacy::Selection*>(item); list != nullptr) {
		out.row(key, column("options/full"), options_in(*list, legacy::Selection::OptType::FULL));
		out.row(key, column("options/short"), options_in(*list, legacy::Selection::OptType::SHORT));
	}
}

/// @brief The names of every case, then the oscillator types again with an input plugged in.
void freeze_all_names(OracleWriter& out) {
	AudioEngine::micPluggedIn = false;
	AudioEngine::lineInPluggedIn = false;
	for (const Case* c : all_name_cases()) {
		freeze_names(out, *c);
	}
	AudioEngine::micPluggedIn = true;
	for (const Case* c : all_cases()) {
		if (c->path.starts_with("osc") && c->path.ends_with("/type")) {
			freeze_names(out, *c, "@plugged");
		}
	}
	AudioEngine::micPluggedIn = false;
}

// ---- The node pairing.

/// @brief The nodes a legacy item is shown by on a page only a select press chains to, which the lockstep walk of the
///        ten roots cannot reach: the preset's two chained pages are the octave mode item with a chain, and the note
///        mode pages they chain to are the note mode items.
const std::vector<std::pair<::MenuItem*, std::vector<const Node*>>>& chained_pages() {
	static const std::vector<std::pair<::MenuItem*, std::vector<const Node*>>> pages{
	    {&arpOctaveModeMenu, {&nodes::arp_octave_mode_to_note_mode, &nodes::arp_octave_mode_to_note_mode_drums}},
	    {&arpNoteModeMenu, {&nodes::arp_note_mode_from_octave_mode}},
	    {&arpNoteModeMenuForDrums, {&nodes::arp_note_mode_from_octave_mode_drums}},
	};
	return pages;
}

/// @brief For each case, the tree paths (`tables_3`'s key space) of the nodes the ten roots pair with its item, and
///        `chain/<name>` for the chained pages.
void freeze_pairing(OracleWriter& out) {
	std::map<const ::MenuItem*, std::vector<std::string>> paths;
	for (const spec::tables3::TablesRoot& root : spec::tables3::roots()) {
		std::vector<Pair> pairs;
		std::string problem;
		check(walk(*root.node, *legacy_root_for(root.name), root.name, pairs, problem), "walk: " + problem);
		for (const auto& [e, item] : pairs) {
			paths[item].push_back(e.path);
		}
	}
	for (const auto& [item, pages] : chained_pages()) {
		for (const Node* page : pages) {
			paths[item].push_back("chain/" + chain_label(page));
			check(!spec::param_comparable(*page), "descriptors: a chained page is a param node");
		}
	}
	for (const Case* c : all_name_cases()) {
		::MenuItem* item = item_of(*c);
		out.row(case_key(*c), "paths", paths[item]);
	}
}

void freeze(OracleWriter& out) {
	std::set<std::string> keys;
	for (const Case* c : all_name_cases()) {
		check(keys.insert(case_key(*c)).second, "descriptors: two cases share " + case_key(*c));
	}
	for (const Case& c : applicability_cases()) {
		check(keys.insert(case_key(c)).second, "descriptors: two cases share " + case_key(c));
	}
	freeze_all_names(out);
	freeze_cases(out, voice_cases());
	freeze_cases(out, oscillator_cases());
	{ // The oscillator types with an input plugged in.
		AudioEngine::micPluggedIn = true;
		for (const size_t index : {size_t{0}, size_t{1}, size_t{3}}) {
			freeze_case(out, oscillator_cases()[index], "@plugged");
		}
		AudioEngine::micPluggedIn = false;
	}
	freeze_cases(out, lfo_and_modulator_cases());
	freeze_cases(out, unison_and_midi_cases());
	freeze_cases(out, effect_cases());
	freeze_cases(out, arp_cases());
	freeze_cases(out, arp_host_cases());
	freeze_cases(out, model_cases());
	AudioEngine::micPluggedIn = false;
	AudioEngine::lineInPluggedIn = false;
	for (const Case& c : scaled_cases()) {
		freeze_scaled(out, c, item_of(c));
	}
	freeze_applicability(out);
	freeze_exceptions(out);
	freeze_pairing(out);
}

} // namespace descriptors

// ---------------------------------------------------------------------------------------------------------------
// Family `shortcuts`: the shortcut layouts, the blink plans, the overlay colours and the landing pages.

namespace shortcuts {

namespace fx = spec::shortcuts;
namespace t3 = spec::tables3;

/// @brief The sentinel the shortcut tables hold in a cell that is "not implemented": a popup and no navigation.
::MenuItem* coming_soon() {
	return reinterpret_cast<::MenuItem*>(0xFFFFFFFFu);
}

bool is_empty_cell(const ::MenuItem* item) {
	return item == nullptr || item == coming_soon();
}

/// @brief One legacy shortcut array: a 16x8 grid, or the one-dimensional table that lives in column 11.
struct Layout {
	const char* name;
	::MenuItem* (*grid)[kDisplayHeight];
	::MenuItem** column;
};

const Layout kLayouts[] = {
    {"sounds", paramShortcutsForSounds, nullptr},
    {"sounds_second", paramShortcutsForSoundsSecondLayer, nullptr},
    {"audio_clips", paramShortcutsForAudioClips, nullptr},
    {"song_view", paramShortcutsForSongView, nullptr},
    {"kit_global_fx", paramShortcutsForKitGlobalFX, nullptr},
    {"midi_cv", nullptr, midiOrCVParamShortcuts},
    {"gate_drum", nullptr, gateDrumParamShortcuts},
};

const Layout& layout_named(std::string_view name) {
	for (const Layout& l : kLayouts) {
		if (name == l.name) {
			return l;
		}
	}
	std::abort();
}

::MenuItem* cell_of(const Layout& l, int32_t x, int32_t y) {
	if (l.grid != nullptr) {
		return l.grid[x][y];
	}
	return x == 11 ? l.column[y] : nullptr;
}

/// @brief Every tree path of every root at which each legacy item sits, joined by spaces, in the order the roots and
///        their children are walked.
const std::map<const ::MenuItem*, std::vector<std::string>>& item_paths() {
	static const std::map<const ::MenuItem*, std::vector<std::string>> paths = [] {
		std::map<const ::MenuItem*, std::vector<std::string>> out;
		for (const t3::TablesRoot& root : t3::roots()) {
			t3::install(root.ctx, root.song);
			std::vector<Pair> pairs;
			std::string where;
			check(walk(*root.node, *legacy_root_for(root.name), root.name, pairs, where), "walk: " + where);
			for (const auto& [e, item] : pairs) {
				out[item].push_back(e.path);
			}
		}
		return out;
	}();
	return paths;
}

/// @brief The menu groups the chains name. No tree lists a group and every group's name is "None", so the rows name
///        each by the port node that stands for it.
std::string group_label(const ::MenuItem* item) {
	const std::pair<const ::MenuItem*, const char*> groups[] = {
	    {&arpMenuGroup, "group:arp_group"},
	    {&arpMenuGroupKit, "group:arp_group_kit"},
	    {&arpMenuGroupMIDIOrCV, "group:arp_group_midi"},
	    {&voiceMenuGroup, "group:voice_group"},
	    {&envMenuGroup, "group:env_group"},
	    {&lfoMenuGroup, "group:lfo_group"},
	    {&reverbMenuGroup, "group:reverb_group"},
	    {&filtersMenuGroup, "group:filters_group"},
	    {&globalFiltersMenuGroup, "group:global_filters_group"},
	    {&globalReverbMenuGroup, "group:global_reverb_group"},
	    {&sourceMenuGroup, "group:source_group"},
	};
	for (const auto& [group, label] : groups) {
		if (item == group) {
			return label;
		}
	}
	return {};
}

/// @brief How the cell rows name @p item: its tree paths, `-` for no item, `group:<node>` for a menu group, or
///        `name:<its name>` for an item no root reaches. With @p root, only the paths in that root's own tree where the
///        item sits in it.
std::string paths_of(::MenuItem* item, std::string_view root = {}) {
	if (is_empty_cell(item)) {
		return "-";
	}
	if (std::string group = group_label(item); !group.empty()) {
		return group;
	}
	const auto found = item_paths().find(item);
	if (found == item_paths().end()) {
		return "name:" + std::string{item->getName()};
	}
	return root.empty() ? fx::join_paths(found->second) : fx::paths_in_root(found->second, root);
}

/// @brief Select `SHORTCUTS_VERSION_1` on both sides for the scope, and put everything back.
///
/// `SoundEditor::setShortcutsVersion` writes into the legacy arrays, and its `SHORTCUTS_VERSION_3` arm is a comment,
/// not an undo, so nothing but this guard restores them. Twelve cells are snapshotted: the six of
/// `paramShortcutsForSounds` and four of `paramShortcutsForAudioClips` the V1 arm assigns, plus the two
/// `modSourceShortcuts` ones.
struct LegacyVersion1 {
	::MenuItem* sounds[6];
	::MenuItem* clips[4];
	PatchSource sources[2];
	uint8_t legacy_version;
	menu::ShortcutsVersion port_version;

	LegacyVersion1() {
		const std::pair<uint8_t, uint8_t> sound_cells[6] = {{0, 6}, {1, 6}, {2, 6}, {3, 6}, {2, 7}, {3, 7}};
		for (size_t i = 0; i < 6; i++) {
			sounds[i] = paramShortcutsForSounds[sound_cells[i].first][sound_cells[i].second];
		}
		const std::pair<uint8_t, uint8_t> clip_cells[4] = {{0, 7}, {1, 7}, {0, 6}, {1, 6}};
		for (size_t i = 0; i < 4; i++) {
			clips[i] = paramShortcutsForAudioClips[clip_cells[i].first][clip_cells[i].second];
		}
		sources[0] = modSourceShortcuts[0][7];
		sources[1] = modSourceShortcuts[1][7];
		legacy_version = soundEditor.shortcutsVersion;
		port_version = menu::shortcuts_version();

		soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_1);
		menu::set_shortcuts_version(menu::ShortcutsVersion::V1);
	}

	~LegacyVersion1() {
		const std::pair<uint8_t, uint8_t> sound_cells[6] = {{0, 6}, {1, 6}, {2, 6}, {3, 6}, {2, 7}, {3, 7}};
		for (size_t i = 0; i < 6; i++) {
			paramShortcutsForSounds[sound_cells[i].first][sound_cells[i].second] = sounds[i];
		}
		const std::pair<uint8_t, uint8_t> clip_cells[4] = {{0, 7}, {1, 7}, {0, 6}, {1, 6}};
		for (size_t i = 0; i < 4; i++) {
			paramShortcutsForAudioClips[clip_cells[i].first][clip_cells[i].second] = clips[i];
		}
		modSourceShortcuts[0][7] = sources[0];
		modSourceShortcuts[1][7] = sources[1];
		soundEditor.shortcutsVersion = legacy_version;
		menu::set_shortcuts_version(port_version);
	}
	LegacyVersion1(const LegacyVersion1&) = delete;
	LegacyVersion1& operator=(const LegacyVersion1&) = delete;
};

std::string cell_key(std::string_view table, std::string_view version, int32_t x, int32_t y) {
	return "cell/" + std::string{table} + "/" + std::string{version} + "/" + std::to_string(x) + ","
	       + std::to_string(y);
}

/// @brief Every cell of @p table: which item it holds, and, on the V3 layout, the item's name and whether the cell is a
///        coming-soon one.
void emit_layout(OracleWriter& out, const Layout& table, std::string_view version) {
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (table.grid == nullptr && x != 11) {
				continue;
			}
			::MenuItem* item = cell_of(table, x, y);
			const std::string key = cell_key(table.name, version, x, y);
			out.row(key, "item", paths_of(item));
			if (version != "v3") {
				continue;
			}
			if (item == coming_soon()) {
				out.row(key, "soon");
			}
			else if (item != nullptr) {
				out.row(key, "name", std::string{item->getName()});
			}
		}
	}
}

/// @brief Everything `updatePadLightsFor` planned, spelled so two plans compare as text.
std::string legacy_plan() {
	std::string out;
	if (soundEditor.currentParamShortcutX != kNoSelection) {
		out += "param " + std::to_string(soundEditor.currentParamShortcutX) + ","
		       + std::to_string(soundEditor.currentParamShortcutY) + " f"
		       + std::to_string(soundEditor.paramShortcutBlinkFrequency) + " "
		       + fx::colour_name(soundEditor.currentParamColour) + ";";
		for (int32_t sx = 0; sx < 2; sx++) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				const uint8_t f = soundEditor.sourceShortcutBlinkFrequency(sx, y);
				if (f != 255) {
					out += " src " + std::to_string(sx) + "," + std::to_string(y) + " f" + std::to_string(f) + " "
					       + fx::colour_name(soundEditor.sourceShortcutBlinkColour(sx, y))
					       + (soundEditor.sourceShortcutBlinkAlternates(sx, y) ? " alt" : "");
				}
			}
		}
	}
	return out;
}

/// @brief Legacy's plan for @p item, from a clean slate.
std::string legacy_plan_for(::MenuItem* item) {
	soundEditor.currentParamShortcutX = kNoSelection;
	soundEditor.updatePadLightsFor(item);
	return legacy_plan();
}

void emit_blinks(OracleWriter& out, const MenuContext& ctx) {
	check(fx::seed_blink_cables(ctx), "seeding the blink cables");
	soundEditor.currentSourceIndex = 0;
	namespace p = deluge::modulation::params;
	const auto plan = [&](const std::string& where, ::MenuItem* item) {
		out.row("blink/" + where, "plan", legacy_plan_for(item));
	};

	// Every leaf of the sound root; the patch cables page plans from its own timer, below.
	std::vector<Pair> pairs;
	std::string problem;
	check(walk(nodes::sound_root, soundEditorRootMenu, "sound", pairs, problem), "walk: " + problem);
	for (const auto& [e, item] : pairs) {
		if (e.node->kind != menu::Kind::Submenu && e.node != &nodes::patch_cables) {
			plan("leaf " + e.path, item);
		}
	}

	// The source pickers and strength pages, over params on both layers and the volume triple.
	const uint8_t kParams[] = {p::LOCAL_LPF_FREQ,     p::GLOBAL_VOLUME_POST_FX,
	                           p::LOCAL_VOLUME,       p::GLOBAL_VOLUME_POST_REVERB_SEND,
	                           p::LOCAL_ENV_2_ATTACK, p::LOCAL_PITCH_ADJUST};
	const PatchSource kSources[] = {PatchSource::ENVELOPE_1, PatchSource::ENVELOPE_3, PatchSource::LFO_GLOBAL_1,
	                                PatchSource::AFTERTOUCH, PatchSource::RANDOM};
	for (const uint8_t param : kParams) {
		soundEditor.patchingParamSelected = param;
		plan("regular source list p" + std::to_string(param), &legacy::source_selection::regularMenu);
		for (const PatchSource source : kSources) {
			legacy::source_selection::regularMenu.s = source;
			const std::string tag = " p" + std::to_string(param) + " s" + std::to_string(static_cast<int>(source));
			plan("regular strength" + tag, &legacy::patch_cable_strength::regularMenu);
			plan("range source list" + tag, &legacy::source_selection::rangeMenu);
			for (const PatchSource range : {PatchSource::LFO_LOCAL_1, PatchSource::VELOCITY}) {
				legacy::source_selection::rangeMenu.s = range;
				plan("range strength" + tag + " r" + std::to_string(static_cast<int>(range)),
				     &legacy::patch_cable_strength::rangeMenu);
			}
		}
	}

	PatchCableSet* set = spec::menu_engine(ctx).params->getPatchCableSetAllowJibberish();
	for (int32_t i = 0; i < set->numPatchCables; i++) {
		patchCablesMenu.currentValue = i;
		soundEditor.currentParamShortcutX = kNoSelection;
		patchCablesMenu.blinkShortcuts();
		out.row("blink/patch cables row " + std::to_string(i), "plan", legacy_plan());
	}
}

/// @brief The colour main paints on each of the sound root's 128 main pads while SHIFT is held.
void emit_overlay(OracleWriter& out) {
	RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
	uint8_t occupancy[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
	SoundEditor::renderMainShortcutsOnly(soundEditor.currentModControllable, image, occupancy, false);
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			const RGB c = image[y][x];
			out.row("overlay/sound/" + std::to_string(x) + "," + std::to_string(y), "rgb",
			        {std::to_string(c.r), std::to_string(c.g), std::to_string(c.b)});
		}
	}
}

/// @brief `SoundEditor::maybeGetParentMenu`'s answer for @p item against @p chain: null for the start markers, the
/// audio
///        clip's end marker and the file selectors, else the first chain menu that lists it.
legacy::HorizontalMenu* legacy_parent(const deluge::vector<legacy::HorizontalMenu*>& chain, ::MenuItem* item) {
	if (item == &sample0StartMenu || item == &sample1StartMenu || item == &audioClipSampleMarkerEditorMenuEnd) {
		return nullptr;
	}
	const auto listing = std::ranges::find_if(chain, [&](legacy::HorizontalMenu* m) { return m->hasItem(item); });
	if (listing == chain.end()) {
		return nullptr;
	}
	if (item == &legacy::file0SelectorMenu || item == &legacy::file1SelectorMenu) {
		return nullptr;
	}
	return *listing;
}

/// @brief A port root's label, the shortcut arrays it presses, and the chain legacy searches for it.
struct LandingRoot {
	const char* label;
	const char* pages_in; ///< the root whose tree the chain's pages are named in
	const char* table;
	const char* second_table; ///< null where the root has no distinct second layer
	bool drum_name_editor;    ///< a kit row swaps the name editor for the drum's
	deluge::vector<legacy::HorizontalMenu*>* chain;
};

/// @brief Every root with shortcut cells, paired as `getCurrentHorizontalMenusChain` pairs them: a kit row and its
///        MIDI/gate drums take the sound chain, the session roots the song chain.
std::vector<LandingRoot> landing_roots() {
	return {
	    {"sound", "sound", "sounds", "sounds_second", false, &horizontalMenusChainForSound},
	    {"drum", "drum", "sounds", "sounds_second", true, &horizontalMenusChainForSound},
	    {"midi drum", "midi drum", "midi_cv", nullptr, true, &horizontalMenusChainForSound},
	    {"gate drum", "gate drum", "gate_drum", nullptr, true, &horizontalMenusChainForSound},
	    {"kit global fx", "kit global fx", "kit_global_fx", nullptr, false, &horizontalMenusChainForKit},
	    {"song", "song", "song_view", nullptr, false, &horizontalMenusChainForSong},
	    {"performance", "song", "song_view", nullptr, false, &horizontalMenusChainForSong},
	    {"audio clip", "audio clip", "audio_clips", nullptr, false, &horizontalMenusChainForAudioClip},
	    {"midi/cv", "midi/cv", "midi_cv", nullptr, false, &horizontalMenusChainForMidiOrCv},
	};
}

/// @brief The page each pressable cell of each root opens on: where the chain search finds the item's menu.
void emit_landings(OracleWriter& out) {
	const bool was_on = runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::HorizontalMenus);
	runtimeFeatureSettings.set(RuntimeFeatureSettingType::HorizontalMenus, RuntimeFeatureStateToggle::On);
	for (const LandingRoot& lr : landing_roots()) {
		for (const char* table : {lr.table, lr.second_table}) {
			if (table == nullptr) {
				continue;
			}
			const bool second = table == lr.second_table;
			const Layout& layout = layout_named(table);
			for (int32_t x = 0; x < kDisplayWidth; x++) {
				for (int32_t y = 0; y < kDisplayHeight; y++) {
					::MenuItem* item = cell_of(layout, x, y);
					if (is_empty_cell(item)) {
						continue;
					}
					if (lr.drum_name_editor && item == &editNameMenu) {
						item = &drumNameEditMenu;
					}
					out.row("land/" + std::string{lr.label} + (second ? "/2/" : "/") + std::to_string(x) + ","
					            + std::to_string(y),
					        "page", paths_of(legacy_parent(*lr.chain, item), lr.pages_in));
				}
			}
		}
	}
	runtimeFeatureSettings.set(RuntimeFeatureSettingType::HorizontalMenus,
	                           was_on ? RuntimeFeatureStateToggle::On : RuntimeFeatureStateToggle::Off);
}

void freeze(OracleWriter& out) {
	const fx::FreeverbModel freeverb;
	(void)item_paths();
	const MenuContext& ctx = fx::install_sound_context();
	soundEditor.navigationDepth = 0;
	for (const Layout& table : kLayouts) {
		emit_layout(out, table, "v3");
	}
	{
		const LegacyVersion1 guard;
		emit_layout(out, layout_named("sounds"), "v1");
		emit_layout(out, layout_named("audio_clips"), "v1");
	}
	spec::UiStackGuard ui;
	setRootUILowLevel(&instrumentClipView);
	emit_blinks(out, ctx);
	emit_overlay(out);
	emit_landings(out);
}

} // namespace shortcuts

// ---------------------------------------------------------------------------------------------------------------
// Family `menu_ranges`: the three range items' bounds, spellings, edits, edge turns, entry seeds and panels.

namespace menu_ranges {

std::string_view edge_name(legacy::RangeEdit e) {
	using legacy::RangeEdit;
	return e == RangeEdit::LEFT ? "left" : e == RangeEdit::RIGHT ? "right" : "off";
}

/// @brief The cases `menu_ranges_spec`'s examples drive, in its order: what each start, edge and turn leaves.
/// @return Whether every panel file was written.
bool freeze(OracleWriter& out) {
	bool panels = true;
	using legacy::RangeEdit;
	auto& tempo = FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO];
	auto& key = FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_KEY];
	out.row("default_tempo", "bounds",
	        {std::to_string(defaultTempoMenu.minValue), std::to_string(defaultTempoMenu.maxValue)});
	out.row("default_swing_amount", "bounds",
	        {std::to_string(defaultSwingAmountMenu.minValue), std::to_string(defaultSwingAmountMenu.maxValue)});
	for (int32_t v = defaultTempoMenu.minValue; v <= defaultTempoMenu.maxValue; v++) {
		defaultTempoMenu.lower = v;
		defaultTempoMenu.upper = v;
		char buf[64] = {};
		int32_t left = 0;
		int32_t right = 0;
		defaultTempoMenu.getText(buf, &left, &right, /*mayShowJustOne=*/true);
		out.row("tempo/" + std::to_string(v), "text", std::string(buf, static_cast<size_t>(left)));
	}
	for (const bool sharps : {true, false}) {
		FlashStorage::defaultUseSharps = sharps;
		for (int32_t v = 0; v <= 11; v++) {
			defaultKeyMenu.lower = v;
			defaultKeyMenu.upper = v;
			char buf[64] = {};
			int32_t left = 0;
			int32_t right = 0;
			defaultKeyMenu.getText(buf, &left, &right, /*mayShowJustOne=*/true);
			out.row(std::string{"key/"} + (sharps ? "sharps/" : "flats/") + std::to_string(v), "text",
			        std::string(buf, static_cast<size_t>(left)));
		}
	}
	struct Edit {
		const char* item;
		int32_t lower;
		int32_t upper;
		RangeEdit edge;
		int32_t detents;
	};
	const Edit edits[] = {
	    {"tempo", 120, 120, RangeEdit::OFF, 5},    {"tempo", 90, 150, RangeEdit::OFF, 5},
	    {"tempo", 120, 120, RangeEdit::LEFT, 7},   {"tempo", 120, 150, RangeEdit::LEFT, -7},
	    {"tempo", 120, 120, RangeEdit::RIGHT, -7}, {"tempo", 200, 238, RangeEdit::RIGHT, 9},
	    {"tempo", 62, 200, RangeEdit::LEFT, -9},   {"key", 5, 5, RangeEdit::OFF, 3},
	    {"key", 2, 9, RangeEdit::OFF, 3},          {"key", 5, 5, RangeEdit::LEFT, 4},
	    {"key", 2, 9, RangeEdit::LEFT, 4},         {"key", 2, 9, RangeEdit::RIGHT, -10},
	    {"key", 0, 10, RangeEdit::RIGHT, 5},       {"tempo", 5, 5, RangeEdit::LEFT, 1},
	    {"key", 5, 5, RangeEdit::LEFT, 1},
	};
	for (const Edit& e : edits) {
		const bool is_tempo = std::string_view{e.item} == "tempo";
		auto& slot = is_tempo ? tempo : key;
		slot = {.lower = e.lower, .upper = e.upper};
		soundEditor.editingRangeEdge = e.edge;
		if (is_tempo) {
			defaultTempoMenu.selectEncoderAction(e.detents);
		}
		else {
			defaultKeyMenu.selectEncoderAction(e.detents);
		}
		out.row(std::string{e.item} + "/" + std::to_string(e.lower) + "-" + std::to_string(e.upper) + "/"
		            + std::string{edge_name(e.edge)} + "/" + std::to_string(e.detents),
		        "edit", {std::to_string(slot.lower), std::to_string(slot.upper)});
	}
	for (const RangeEdit from : {RangeEdit::OFF, RangeEdit::LEFT, RangeEdit::RIGHT}) {
		for (const int32_t h : {-1, 1}) {
			soundEditor.editingRangeEdge = from;
			defaultTempoMenu.horizontalEncoderAction(h);
			out.row("tempo/" + std::string{edge_name(from)} + "/" + std::to_string(h), "edge",
			        std::string{edge_name(soundEditor.editingRangeEdge)});
		}
	}
	struct Seed {
		const char* item;
		int32_t lower;
		int32_t upper;
	};
	for (const Seed s : {Seed{"tempo", 120, 120}, Seed{"tempo", 90, 150}, Seed{"key", 5, 5}, Seed{"key", 2, 9}}) {
		const bool is_tempo = std::string_view{s.item} == "tempo";
		(is_tempo ? tempo : key) = {.lower = s.lower, .upper = s.upper};
		soundEditor.editingRangeEdge = RangeEdit::RIGHT;
		if (is_tempo) {
			defaultTempoMenu.beginSession(nullptr);
		}
		else {
			defaultKeyMenu.beginSession(nullptr);
		}
		out.row(std::string{s.item} + "/" + std::to_string(s.lower) + "-" + std::to_string(s.upper), "seed",
		        std::string{edge_name(soundEditor.editingRangeEdge)});
	}
	struct Shot {
		const char* name;
		int32_t lower;
		int32_t upper;
		RangeEdit edge;
	};
	for (const Shot s : {Shot{"point", 120, 120, RangeEdit::OFF}, Shot{"split", 90, 150, RangeEdit::OFF},
	                     Shot{"left", 90, 150, RangeEdit::LEFT}, Shot{"right", 90, 150, RangeEdit::RIGHT}}) {
		tempo = {.lower = s.lower, .upper = s.upper};
		soundEditor.editingRangeEdge = s.edge;
		deluge::hid::display::OLED::main.clear();
		defaultTempoMenu.renderOLED();
		const auto& canvas = deluge::hid::display::OLED::main;
		panels = write_pixels(std::string{"menu_range_"} + s.name,
		                      page_major_to_pbm(canvas.bytes().data(), OLED_MAIN_WIDTH_PIXELS, OLED_MAIN_HEIGHT_PIXELS))
		         && panels;
	}
	return panels;
}

} // namespace menu_ranges

// ---------------------------------------------------------------------------------------------------------------
// Family `menu_scales`: the preset-scale list, the toggles it writes and its panels.

namespace menu_scales {

using spec::scales::bits_hex;
using spec::scales::ScalesGuard;
using spec::scales::sentinel_bits;

/// @brief Move the legacy item's own `currentPos` to @p row, from wherever the last example left it.
///
/// `currentPos` is private and persists across visits (`beginSession` is only a redraw), so it is driven to a known
/// row rather than assumed: enough single down-detents to reach the bottom, then @p row single up-detents.
///
/// @note **One detent at a time, deliberately.** `ActiveScaleMenu::selectEncoderAction` narrows `currentPos + offset`
///       to `int8_t` before it clamps, so a large offset wraps and lands somewhere unrelated:
///       `selectEncoderAction(-1000)` from row 0 gives `(int8_t)(-1000) == 24`, which clamps up to `LAST_PRESET_SCALE`.
///       It is unreachable on the device -- the encoder delivers one detent per pass -- and the port clamps in
///       `int32_t`.
void legacy_walk_to(legacy::ActiveScaleMenu& item, int32_t row) {
	for (int32_t i = 0; i <= static_cast<int32_t>(LAST_PRESET_SCALE); i++) {
		item.selectEncoderAction(-1);
	}
	for (int32_t i = 0; i < row; i++) {
		item.selectEncoderAction(1);
	}
}

/// @return Whether every panel file was written.
bool freeze(OracleWriter& out) {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	bool panels = true;
	{
		// The rows legacy draws: walked well past the end, the last row the cursor reaches is the one a press toggles.
		spec::SongFixture fixture = spec::make_synth_song(); // legacy reads `currentSong` directly
		ScalesGuard guard;
		currentSong->project.scale.disabled_preset_scales.reset();
		legacy_walk_to(activeScaleMenu, 1000);
		activeScaleMenu.selectButtonPress();
		const auto& after = currentSong->project.scale.disabled_preset_scales;
		check(after.count() == 1, "the walk past the end toggled exactly one scale");
		size_t last = 0;
		for (size_t i = 0; i < NUM_PRESET_SCALES; i++) {
			if (after[i]) {
				last = i;
			}
		}
		out.row("scales", "count", static_cast<int64_t>(last + 1));
		for (size_t i = 0; i <= last; i++) {
			out.row("scales/" + std::to_string(i), "name", std::string{scalelikeNames[i]});
		}
	}
	for (const int32_t row : {0, 1, 4, static_cast<int32_t>(LAST_PRESET_SCALE)}) {
		spec::SongFixture fixture = spec::make_synth_song();
		ScalesGuard guard;
		currentSong->project.scale.disabled_preset_scales = sentinel_bits();
		legacy_walk_to(activeScaleMenu, row);
		activeScaleMenu.selectButtonPress();
		out.row("scales/toggle/" + std::to_string(row), "bits",
		        bits_hex(currentSong->project.scale.disabled_preset_scales));
	}
	for (const int32_t row : {0, 4, static_cast<int32_t>(LAST_PRESET_SCALE)}) {
		spec::SongFixture fixture = spec::make_synth_song();
		ScalesGuard guard;
		currentSong->project.scale.disabled_preset_scales = sentinel_bits();
		legacy_walk_to(activeScaleMenu, row);
		deluge::hid::display::OLED::main.clear();
		activeScaleMenu.renderOLED();
		const std::string name = row == static_cast<int32_t>(LAST_PRESET_SCALE) ? "last" : std::to_string(row);
		panels = write_pixels("menu_scales_" + name, legacy_panel_pbm()) && panels;
	}
	return panels;
}

} // namespace menu_scales

// ---------------------------------------------------------------------------------------------------------------
// Family `midi_device`: the per-device page's shape, its per-cable writes and the MPE arm under it.

namespace midi_device {

using spec::mididev::baseline;
using spec::mididev::CableState;
using spec::mididev::Fixture;
using spec::mididev::SpecCable;

/// @brief Point the `soundEditor` globals the legacy items read at the same cable and song.
void install_legacy(::MIDICable* cable, Song* song) {
	currentSong = song;
	soundEditor.currentMIDICable = cable;
}

/// @brief A legacy page pointer as the name `mididev::chain_name` gives the port's.
std::string legacy_chain_name(::MenuItem* page) {
	if (page == nullptr) {
		return "<ascend>";
	}
	if (page == &midiDeviceMenu) {
		return "device";
	}
	if (page == &legacy::mpe::zoneSelectorMenu) {
		return "mpe_zone";
	}
	if (page == &legacy::mpe::zoneNumMemberChannelsMenu) {
		return "mpe_members";
	}
	return "UNMAPPED";
}

std::vector<std::string> as_strings(const deluge::vector<std::string_view>& options) {
	std::vector<std::string> out;
	for (const std::string_view v : options) {
		out.emplace_back(v);
	}
	return out;
}

void freeze(OracleWriter& out) {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	Fixture& f = spec::mididev::fixture();

	// The shape.
	const auto& items = SubmenuPeek::items_of(midiDeviceMenu);
	out.row("device", "kids", static_cast<int64_t>(items.size()));
	for (size_t i = 0; i < items.size(); i++) {
		out.row("device/" + std::to_string(i), "name", std::string{items[i]->getName()});
	}
	install_legacy(f.a.get(), f.song.song);
	out.row("devices", "chain", legacy_chain_name(legacy::midi::devicesMenu.selectButtonPress()));
	out.row("mpe_direction", "chain", legacy_chain_name(legacy::mpe::directionSelectorMenu.selectButtonPress()));
	out.row("mpe_zone", "chain", legacy_chain_name(legacy::mpe::zoneSelectorMenu.selectButtonPress()));

	// The default velocity.
	for (const int32_t value : {0, 1, 25, kMaxMenuValue}) {
		baseline(*f.a, *f.b);
		install_legacy(f.a.get(), f.song.song);
		defaultVelocityToLevelMenu.setValue(value);
		ValuePeek<int32_t>::write(defaultVelocityToLevelMenu);
		const std::string path = "velocity/" + std::to_string(value);
		out.row(path, "cable/0", std::to_string(f.a->defaultVelocityToLevel));
		out.row(path, "cable/1", std::to_string(f.b->defaultVelocityToLevel));
		out.row(path, "state", CableState::capture(*f.a, *f.b));
		defaultVelocityToLevelMenu.readCurrentValue();
		out.row(path, "read", defaultVelocityToLevelMenu.getValue());
	}

	// The four toggles.
	struct Row {
		const char* name;
		legacy::Toggle* item;
	};
	const Row rows[] = {
	    {"sendClock", &device_send_clock_menu},
	    {"receiveClock", &device_receive_clock_menu},
	    {"is_relative", &device_is_relative_menu},
	    {"midi_thru", &device_send_thru_menu},
	};
	for (const Row& row : rows) {
		for (const int32_t value : {0, 1}) {
			baseline(*f.a, *f.b);
			install_legacy(f.a.get(), f.song.song);
			row.item->setValue(value != 0);
			ValuePeek<bool>::write(*row.item);
			const std::string path = std::string{"toggle/"} + row.name + "/" + std::to_string(value);
			out.row(path, "fields",
			        {std::to_string(int(f.a->sendClock)), std::to_string(int(f.a->receiveClock)),
			         std::to_string(int(f.a->is_relative)), std::to_string(int(f.a->midi_thru))});
			out.row(path, "state", CableState::capture(*f.a, *f.b));
			row.item->readCurrentValue();
			out.row(path, "read", row.item->getValue() ? 1 : 0);
		}
	}

	// The member-channel page: what it reads and bounds, per cable.
	for (SpecCable* cable : {f.a.get(), f.b.get()}) {
		baseline(*f.a, *f.b);
		install_legacy(cable, f.song.song);
		const std::string which = cable == f.a.get() ? "a" : "b";
		for (uint8_t direction = 0; direction < 2; direction++) {
			for (uint8_t zone = 0; zone < 2; zone++) {
				legacy::mpe::directionSelectorMenu.whichDirection = direction;
				legacy::mpe::zoneSelectorMenu.whichZone = zone;
				legacy::mpe::zoneNumMemberChannelsMenu.readCurrentValue();
				const std::string path = "mpe/" + std::to_string(direction) + "/" + std::to_string(zone);
				out.row(path, "read@" + which, legacy::mpe::zoneNumMemberChannelsMenu.getValue());
				out.row(path, "max@" + which, legacy::mpe::zoneNumMemberChannelsMenu.getMaxValue());
			}
		}
	}

	// The member-channel write: the port it lands on, and the MCM tail it sends.
	for (uint8_t direction = 0; direction < 2; direction++) {
		for (uint8_t zone = 0; zone < 2; zone++) {
			for (const int32_t value : {0, 4, 7}) {
				baseline(*f.a, *f.b);
				install_legacy(f.a.get(), f.song.song);
				legacy::mpe::directionSelectorMenu.whichDirection = direction;
				legacy::mpe::zoneSelectorMenu.whichZone = zone;
				legacy::mpe::zoneNumMemberChannelsMenu.setValue(value);
				ValuePeek<int32_t>::write(legacy::mpe::zoneNumMemberChannelsMenu);
				const std::string path =
				    "mpe/" + std::to_string(direction) + "/" + std::to_string(zone) + "/" + std::to_string(value);
				out.row(path, "ports",
				        {std::to_string(f.a->ports[0].mpeLowerZoneLastMemberChannel),
				         std::to_string(f.a->ports[0].mpeUpperZoneLastMemberChannel),
				         std::to_string(f.a->ports[1].mpeLowerZoneLastMemberChannel),
				         std::to_string(f.a->ports[1].mpeUpperZoneLastMemberChannel)});
				out.row(path, "state", CableState::capture(*f.a, *f.b));
				out.row(path, "sent", f.a->sent);
			}
		}
	}

	// The titles and the options.
	for (uint8_t direction = 0; direction < 2; direction++) {
		legacy::mpe::directionSelectorMenu.whichDirection = direction;
		out.row("mpe/" + std::to_string(direction), "title",
		        std::string{legacy::mpe::directionSelectorMenu.getTitle()});
	}
	out.row("mpe_direction", "options",
	        as_strings(legacy::mpe::directionSelectorMenu.getOptions(legacy::Selection::OptType::FULL)));
	out.row("mpe_zone", "options",
	        as_strings(legacy::mpe::zoneSelectorMenu.getOptions(legacy::Selection::OptType::FULL)));
}

} // namespace midi_device

// ---------------------------------------------------------------------------------------------------------------
// Family `menu_actions`: the performance editing mode item's options and its mode for each pair of view flags.

namespace menu_actions {

void freeze(OracleWriter& out) {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	std::vector<std::string> names;
	for (const std::string_view v : performEditorMenu.getOptions(legacy::Selection::OptType::FULL)) {
		names.emplace_back(v);
	}
	out.row("perform", "options", names);

	const bool saved_default = performanceView.defaultEditingMode;
	const bool saved_param = performanceView.editingParam;
	// `{false, true}` is unreachable in legacy -- `readCurrentValue` tests `defaultEditingMode` first.
	for (const auto& [default_mode, editing_param] :
	     std::array<std::pair<bool, bool>, 4>{{{false, false}, {true, false}, {true, true}, {false, true}}}) {
		performanceView.defaultEditingMode = default_mode;
		performanceView.editingParam = editing_param;
		performEditorMenu.readCurrentValue();
		out.row("perform/" + std::to_string(int(default_mode)) + "," + std::to_string(int(editing_param)), "mode",
		        performEditorMenu.getValue());
	}
	performanceView.defaultEditingMode = saved_default;
	performanceView.editingParam = saved_param;
}

} // namespace menu_actions

// ---------------------------------------------------------------------------------------------------------------
// Family `song_settings_edit`: the mode every kit row's arp takes from the legacy mode item with affect-entire held.

namespace song_settings_edit {

void freeze(OracleWriter& out) {
	spec::SongFixture f = spec::make_kit_song();
	SoundDrum* sound = spec::add_drum<SoundDrum>(f);
	MIDIDrum* midi = spec::add_drum<MIDIDrum>(f);
	f.song->setCurrentClip(f.clip);
	soundEditor.currentArpSettings = &midi->arpSettings;
	soundEditor.setupKitGlobalFXMenu = false;
	currentUIMode = UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR;
	arpModeMenu.setValue(1);
	ValuePeek<int32_t>::write(arpModeMenu);
	currentUIMode = UI_MODE_NONE;
	out.row("kit_arp/sound", "mode", static_cast<int64_t>(sound->arpSettings.mode));
	out.row("kit_arp/midi", "mode", static_cast<int64_t>(midi->arpSettings.mode));
}

} // namespace song_settings_edit

// ---------------------------------------------------------------------------------------------------------------
// Family `output_items`: the MIDI device-definition link's value, the specific-output selector's landings, walks and
// relevance, and its two panels.

namespace output_items {

namespace oi = spec::outputitems;

/// @brief Begin a legacy selector session over whatever `currentSong` says, as the spec's legacy half did.
void begin_selector() {
	midiDeviceLinkedMenu.readCurrentValue(); // unrelated; keeps the two globals' state independent
	specificOutputSelectorMenu.beginSession(nullptr);
}

/// @return Whether both panel files were written.
bool freeze(OracleWriter& out) {
	static oi::QuietDisplay quiet;
	deluge::hid::Display* const saved_display = display;
	if (display == nullptr) {
		display = &quiet;
	}
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;

	{
		oi::MidiRig rig;
		for (const oi::LinkedCase& c : oi::kLinkedCases) {
			rig.instrument()->setDeviceDefinitionFileName(c.path);
			midiDeviceLinkedMenu.readCurrentValue();
			out.row(std::string{"linked/"} + c.name, "value", midiDeviceLinkedMenu.getValue() ? 1 : 0);
		}
	}
	for (const auto& [name, start] :
	     {std::pair{"invalid_source", oi::Start::None}, std::pair{"valid_source", oi::Start::SynthB}}) {
		oi::ClipRig rig;
		oi::store_source(rig, start);
		begin_selector();
		out.row(std::string{"selector/"} + name, "lands", rig.recording_from_index());
	}
	for (const oi::Walk& walk : oi::kWalks) {
		oi::ClipRig rig;
		if (walk.extra_synths) {
			(void)rig.add_synth("C");
			(void)rig.add_synth("D");
		}
		const int32_t step = walk.detents >= 0 ? 1 : -1;
		const int32_t steps = walk.detents >= 0 ? walk.detents : -walk.detents;
		oi::store_source(rig, walk.start);
		begin_selector();
		for (int32_t i = 1; i <= steps; i++) {
			specificOutputSelectorMenu.selectEncoderAction(step);
			out.row(std::string{"selector/"} + walk.name + "/" + std::to_string(i), "at", rig.recording_from_index());
		}
		if (std::string_view{walk.name} == "shift") {
			// One click with Shift held reaches the item as the offset times five.
			oi::store_source(rig, walk.start);
			begin_selector();
			specificOutputSelectorMenu.selectEncoderAction(5);
			out.row("selector/shift/click", "at", rig.recording_from_index());
		}
	}
	bool panels = true;
	{
		oi::ClipRig rig;
		rig.song.output->setOutputRecordingFrom(rig.synth_a);
		deluge::hid::display::OLED::main.clear();
		specificOutputSelectorMenu.beginSession(nullptr);
		specificOutputSelectorMenu.renderOLED();
		panels = write_pixels("output_selector", legacy_panel_pbm()) && panels;
	}
	{
		// A song with only the audio track in it: nothing can be recorded from, which is the "No track" arm.
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		f.output->setInputChannel(AudioInputChannel::SPECIFIC_OUTPUT);
		f.output->setOutputRecordingFrom(nullptr);
		deluge::hid::display::OLED::main.clear();
		specificOutputSelectorMenu.beginSession(nullptr);
		specificOutputSelectorMenu.renderOLED();
		panels = write_pixels("output_selector_empty", legacy_panel_pbm()) && panels;
	}
	{
		oi::ClipRig rig;
		for (const auto& [name, channel] :
		     {std::pair{"specific", AudioInputChannel::SPECIFIC_OUTPUT}, std::pair{"left", AudioInputChannel::LEFT}}) {
			rig.song.output->setInputChannel(channel);
			out.row(std::string{"selector/"} + name, "rel", specificOutputSelectorMenu.isRelevant(nullptr, 0) ? 1 : 0);
		}
	}
	display = saved_display;
	return panels;
}

} // namespace output_items

// ---------------------------------------------------------------------------------------------------------------
// Family `sample_items`: whether the file selector, the loop start and the loop end are offered, for each oscillator
// configuration and each oscillator.

namespace sample_items {

namespace si = spec::sampleitems;

void freeze(OracleWriter& out) {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	spec::SongFixture song = spec::make_synth_song();
	auto* sound = static_cast<Sound*>(static_cast<ModControllableAudio*>(song.instrument->toModControllable()));
	Sample* sample = si::make_sample(32);
	for (const si::OscCase& c : si::kOscCases) {
		for (int32_t which = 0; which < 2; which++) {
			si::seed_osc_case(*sound, *song.song, sample, c, which);
			soundEditor.currentSound = sound;
			soundEditor.currentSourceIndex = static_cast<int8_t>(which);
			soundEditor.currentSource = &sound->sources[which];
			const bool file =
			    (which == 0 ? legacy::file0SelectorMenu : legacy::file1SelectorMenu).isRelevant(sound, which);
			const bool loop = (which == 0 ? sample0StartMenu : sample1StartMenu).isRelevant(sound, which);
			const bool end = (which == 0 ? sample0EndMenu : sample1EndMenu).isRelevant(sound, which);
			out.row(std::string{"sample/"} + c.name + "/" + std::to_string(which), "rel",
			        {file ? "1" : "0", loop ? "1" : "0", end ? "1" : "0"});
		}
	}
}

} // namespace sample_items

// ---------------------------------------------------------------------------------------------------------------
// Family `dx_pages`: the DX7 parameter page's limits, titles, row targets, steps, layouts, pad maps and blinks, and the
// panels of the four DX7 pages.

namespace dx_pages {

namespace dx = deluge::scenes::menu::dx;
namespace dxp = spec::dxpages;
namespace rig = spec::dx_rig;

/// @brief Legacy's param page on @p param, as `readValueAgain` leaves it.
void legacy_on(int16_t param) {
	legacy::dxParam.param = param;
	legacy::dxParam.readValueAgain();
}

/// @brief Hold or release Shift on legacy's side, which is all `isShiftButtonPressed` reads.
void set_shift(bool on) {
	::Buttons::shiftCurrentlyPressed = on;
	::Buttons::shiftCurrentlyStuck = false;
}

/// @brief Legacy's `renderMainShortcutsOnly` with `soundEditor` the current UI on @p item: the overlay `main` paints.
std::array<std::array<RGB, 18>, 8> legacy_overlay(::MenuItem& item) {
	RootUI* const root = getRootUI();
	const int32_t open = numUIsOpen;
	setRootUILowLevel(&soundEditor);
	const uint8_t depth = soundEditor.navigationDepth;
	::MenuItem* const was = soundEditor.menuItemNavigationRecord[depth];
	soundEditor.menuItemNavigationRecord[depth] = &item;
	RGB image[8][18]{};
	uint8_t occupancy[8][18]{};
	SoundEditor::renderMainShortcutsOnly(nullptr, image, occupancy, false);
	soundEditor.menuItemNavigationRecord[depth] = was;
	if (root != nullptr) {
		setRootUILowLevel(root);
	}
	numUIsOpen = open;
	std::array<std::array<RGB, 18>, 8> out{};
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 18; x++) {
			out[y][x] = image[y][x];
		}
	}
	return out;
}

/// @brief The page a row of the global list or an operator list leads to, as `selectButtonPress` answers it.
std::string target_of(const ::MenuItem* next) {
	if (next == &legacy::dxParam) {
		return "param " + std::to_string(legacy::dxParam.param);
	}
	if (next == &legacy::dxOperatorParams) {
		return "op " + std::to_string(legacy::dxOperatorParams.op);
	}
	return "other";
}

/// @return Whether every panel file was written.
bool freeze(OracleWriter& out) {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	bool panels = true;
	const auto panel = [&](const std::string& name) { panels = write_pixels(name, legacy_panel_pbm()) && panels; };

	{
		rig::DxRig r;
		for (int16_t p = dx::kRandomDetune; p <= dx::kLastParam; p++) {
			legacy_on(p);
			const std::string path = "dx/" + std::to_string(p);
			out.row(path, "max", static_cast<int64_t>(legacy::dxParam.upper_limit));
			out.row(path, "title", std::string{legacy::dxParam.getTitle()});
		}
		for (int32_t row = 0; row < dx::kGlobalRows; row++) {
			legacy::dxGlobalParams.currentValue = row;
			out.row("dx/row/" + std::to_string(row), "to", target_of(legacy::dxGlobalParams.selectButtonPress()));
		}
		for (int32_t op = 0; op < 6; op++) {
			for (int32_t row = 0; row < dx::kOperatorRows; row++) {
				legacy::dxOperatorParams.op = op;
				legacy::dxOperatorParams.currentValue = row;
				out.row("dx/op/" + std::to_string(op) + "/row/" + std::to_string(row), "to",
				        target_of(legacy::dxOperatorParams.selectButtonPress()));
			}
		}
		for (int16_t p = dx::kRandomDetune; p <= dx::kLastParam; p++) {
			for (const int32_t offset : {-1, 1}) {
				legacy_on(p);
				legacy::dxParam.horizontalEncoderAction(offset);
				out.row("dx/h/" + std::to_string(p) + "/" + std::to_string(offset), "to",
				        static_cast<int64_t>(legacy::dxParam.param));
			}
		}
		for (int32_t mask = 0; mask < 64; mask++) {
			r.patch().params[dx::kOperatorSwitch] = static_cast<uint8_t>(mask);
			for (int16_t p = dx::kRandomDetune; p <= dx::kLastParam; p++) {
				if (mask == 0 && p == dx::kFirstGlobal) {
					continue; // legacy's search never ends here
				}
				std::vector<std::string> to;
				for (const int32_t offset : {-1, 1}) {
					legacy_on(p);
					set_shift(true);
					legacy::dxParam.horizontalEncoderAction(offset);
					set_shift(false);
					to.push_back(std::to_string(legacy::dxParam.param));
				}
				out.row("dx/shift_h/" + std::to_string(p) + "/" + std::to_string(mask), "to", to);
			}
		}
		const ::MenuItem* const rows[] = {&legacy::dxBrowseMenu, &legacy::dxGlobalParams, &legacy::dxEngineSelect};
		const auto kids = SubmenuPeek::items_of(::dxMenu);
		out.row("dx/source", "kids", static_cast<int64_t>(kids.size()));
		out.row("dx/source", "title", std::string{::dxMenu.getTitle()});
		check(kids.size() == std::size(rows) && kids[0] == rows[0] && kids[1] == rows[1] && kids[2] == rows[2],
		      "dxMenu lists the browse, parameter and engine rows in that order");
		for (size_t i = 0; i < kids.size(); i++) {
			out.row("dx/source/" + std::to_string(i), "name", std::string{kids[i]->getName()});
		}
	}
	{
		rig::MenuRig r;
		deluge::instrument::DxPatchState patch = deluge::bridge::dxPatchOf(r.sound->sources[0]).value();
		dxp::for_each_layout(
		    patch,
		    [&] { check(menu::host_write_sink().write_dx_patch(r.slot(), 0, patch), "the layout patch was written"); },
		    [&](const std::string& name, int16_t param) {
			    legacy_on(param);
			    deluge::hid::display::OLED::main.clear();
			    legacy::dxParam.renderOLED();
			    panel("dx_" + name);
		    });
		const auto render = [&](::MenuItem& item, const std::string& name) {
			deluge::hid::display::OLED::main.clear();
			item.renderOLED();
			panel("dx_source_" + name);
		};
		::dxMenu.beginSession(nullptr); // its current row, as entering it on the device sets
		render(::dxMenu, "menu");
		for (int32_t row = 0; row < dx::kGlobalRows; row++) {
			legacy::dxGlobalParams.currentValue = row;
			legacy::dxGlobalParams.scrollPos = std::clamp(row - 1, 0, dx::kGlobalRows - 3);
			render(legacy::dxGlobalParams, "global_" + std::to_string(row));
		}
		for (int32_t op = 0; op < 6; op++) {
			for (int32_t row = 0; row < dx::kOperatorRows; row++) {
				legacy::dxOperatorParams.op = op;
				legacy::dxOperatorParams.format(6 - op);
				legacy::dxOperatorParams.currentValue = row;
				legacy::dxOperatorParams.scrollPos = std::clamp(row - 1, 0, dx::kOperatorRows - 3);
				render(legacy::dxOperatorParams, "operator_" + std::to_string(op) + "_" + std::to_string(row));
			}
		}
		for (uint8_t mode = 0; mode < 3; mode++) {
			deluge::instrument::DxPatchState modes = deluge::bridge::dxPatchOf(r.sound->sources[0]).value();
			modes.engine_mode = mode;
			check(menu::host_write_sink().write_dx_patch(r.slot(), 0, modes), "the engine mode was written");
			legacy::dxEngineSelect.readValueAgain();
			render(legacy::dxEngineSelect, "engine_" + std::to_string(mode));
		}
	}
	{
		rig::DxRig r;
		for (int16_t param = dx::kRandomDetune; param <= dx::kLastParam; param++) {
			soundEditor.currentParamShortcutX = 255; // legacy leaves the pad alone where none blinks
			soundEditor.currentParamShortcutY = 255;
			legacy_on(param);
			const std::string path = "dx/" + std::to_string(param);
			if (soundEditor.currentParamShortcutX == 255) {
				out.row(path, "blink", "-");
			}
			else {
				out.row(path, "blink",
				        {std::to_string(soundEditor.currentParamShortcutX) + ","
				             + std::to_string(soundEditor.currentParamShortcutY),
				         std::to_string(soundEditor.paramShortcutBlinkFrequency)});
			}
			out.row(path, "flash_row", static_cast<int64_t>(legacy::dxParam.flash_row));
		}
	}
	{
		rig::DxRig r;
		rig::Popups p; // `flashParamName` asks the display whether it is a 7-segment one
		for (uint8_t x = 0; x < 16; x++) {
			for (uint8_t y = 0; y < 8; y++) {
				legacy_on(dxp::kUntouched);
				(void)legacy::dxParam.potentialShortcutPadAction(x, y, true);
				out.row("dx/pad/" + std::to_string(x) + "," + std::to_string(y), "param",
				        static_cast<int64_t>(legacy::dxParam.param));
			}
		}
	}
	{
		rig::DxRig r;
		dxp::DxEngineOn on;
		const auto want = legacy_overlay(legacy::dxParam);
		for (uint8_t y = 0; y < 8; y++) {
			for (uint8_t x = 0; x < 16; x++) {
				const RGB c = want[y][x];
				out.row("dx/overlay/" + std::to_string(x) + "," + std::to_string(y), "rgb",
				        {std::to_string(c.r), std::to_string(c.g), std::to_string(c.b)});
			}
		}
	}
	{
		rig::DxRig r;
		RootUI* const root = getRootUI();
		const int32_t open = numUIsOpen;
		setRootUILowLevel(&soundEditor); // the editor is open...
		const uint8_t depth = soundEditor.navigationDepth;
		::MenuItem* const was = soundEditor.menuItemNavigationRecord[depth];
		soundEditor.menuItemNavigationRecord[depth] = &legacy::dxParam; // ...on the parameter page
		for (int32_t op = 0; op < 8; op++) {
			for (int16_t p = dx::kRandomDetune; p <= dx::kLastParam; p++) {
				legacy_on(p);
				legacy::dxParam.openForOpOrGlobal(op);
				out.row("dx/column/" + std::to_string(op) + "," + std::to_string(p) + "/on", "param",
				        static_cast<int64_t>(legacy::dxParam.param));
			}
		}
		soundEditor.menuItemNavigationRecord[depth] = was;
		if (root != nullptr) {
			setRootUILowLevel(root);
		}
		numUIsOpen = open;
	}
	{
		// Off the page: the editor is not open on the parameter page, so main's `setup()` opens it and the param is the
		// operator's level, or the algorithm.
		rig::DxRig r;
		dxp::IsomorphicRoot clip_screen{r.song.clip}; // a clip-minder root, which `setup()` resolves the sound from
		for (int32_t op = 0; op < 8; op++) {
			RootUI* const root = getRootUI();
			const int32_t open = numUIsOpen;
			const uint8_t depth = soundEditor.navigationDepth;
			::MenuItem* const was = soundEditor.menuItemNavigationRecord[depth];
			soundEditor.menuItemNavigationRecord[depth] = &legacy::dxGlobalParams;
			legacy_on(dxp::kUntouched);
			legacy::dxParam.openForOpOrGlobal(op);
			out.row("dx/column/" + std::to_string(op), "off", static_cast<int64_t>(legacy::dxParam.param));
			soundEditor.menuItemNavigationRecord[depth] = was;
			if (root != nullptr) {
				setRootUILowLevel(root);
			}
			numUIsOpen = open;
		}
	}
	{
		rig::DxRig r;
		dxp::IsomorphicRoot root{r.song.clip};
		for (const int32_t scroll : dxp::kScrolls) {
			for (int32_t interval = dxp::kIntervalFirst; interval <= dxp::kIntervalLast; interval++) {
				r.song.clip->keyboardState.isomorphic.scrollOffset = scroll;
				r.song.clip->keyboardState.isomorphic.rowInterval = interval;
				for (int32_t breakpoint = 0; breakpoint < dxp::kBreakpointEnd; breakpoint += dxp::kBreakpointStep) {
					r.patch().params[2 * 21 + 8] = static_cast<uint8_t>(breakpoint);
					legacy_on(2 * 21 + 8);
					soundEditor.currentParamShortcutX = 255;
					uiTimerManager.setTimer(TimerName::SHORTCUT_BLINK, 1000);
					legacy::dxParam.renderOLED();
					const std::string path = "dx/breakpoint/" + std::to_string(scroll) + "/" + std::to_string(interval)
					                         + "/" + std::to_string(breakpoint);
					if (soundEditor.currentParamShortcutX == 255) {
						check(!uiTimerManager.isTimerSet(TimerName::SHORTCUT_BLINK),
						      "no pad blinks where the blink timer was cancelled: " + path);
						out.row(path, "pad", "-");
					}
					else {
						out.row(path, "pad",
						        {std::to_string(soundEditor.currentParamShortcutX) + ","
						             + std::to_string(soundEditor.currentParamShortcutY),
						         std::to_string(soundEditor.paramShortcutBlinkFrequency)});
					}
				}
			}
		}
	}
	return panels;
}

} // namespace dx_pages

// ---------------------------------------------------------------------------------------------------------------
// Family `dx_cartridge`: the loaded bank's program names, a program unpacked over a patch, and the cartridge page's
// panel at each cursor.

namespace dx_cartridge {

/// @brief Parse a fresh bank into legacy's cartridge item, as a finished read does.
void load_bank() {
	std::array<std::byte, kSysexSize> bank = spec::make_bank();
	(void)legacy::dxCartridge.tryLoadFromBuffer(bank);
}

/// @return Whether every panel file was written.
bool freeze(OracleWriter& out) {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	bool panels = true;
	load_bank();
	std::vector<std::string> names;
	char programs[32][11];
	legacy::dxCartridge.pd->getProgramNames(programs);
	for (int32_t i = 0; i < legacy::dxCartridge.pd->numPatches(); i++) {
		names.emplace_back(programs[i]);
	}
	out.row("bank", "names", names);
	{
		spec::dx_rig::DxRig r;
		deluge::instrument::DxPatchState patch = deluge::bridge::dxPatchOf(r.sound->sources[0]).value();
		legacy::dxCartridge.pd->unpackProgram(patch.params, 5);
		std::vector<std::string> params;
		for (const uint8_t b : patch.params) {
			params.push_back(std::to_string(b));
		}
		out.row("bank/5", "params", params);
	}
	for (const int32_t cursor : {0, 1, 2, 5, 30, 31}) {
		legacy::dxCartridge.currentValue = cursor;
		legacy::dxCartridge.scrollPos = std::clamp(cursor - 1, 0, legacy::dxCartridge.pd->numPatches() - 3);
		deluge::hid::display::OLED::main.clear();
		legacy::dxCartridge.renderOLED();
		panels = write_pixels("dx_cartridge_" + std::to_string(cursor), legacy_panel_pbm()) && panels;
	}
	return panels;
}

} // namespace dx_cartridge

// ---------------------------------------------------------------------------------------------------------------
// Family `context_menu_scene`: the witness each settings reset leaves from `kPerturbed`, and the confirmation window
// for each action.

namespace context_menu_scene {

namespace rw = deluge::scenes::spec::reset;
using deluge::gui::context_menu::ResetSettingsAction;

/// @brief One reset through the shipped `ResetSettings` object, from `rw::kPerturbed`: `setAction`, then the
///        `acceptCurrentOption()` that `ContextMenu::buttonAction` calls. @return the witness afterwards.
rw::ResetWitness run_legacy_reset(ResetSettingsAction action) {
	rw::restore(rw::kPerturbed);
	deluge::gui::context_menu::resetSettings.setAction(action);
	(void)deluge::gui::context_menu::resetSettings.acceptCurrentOption();
	return rw::witness();
}

/// @brief The confirmation window for @p action as the shipped object draws it: the `setAction` /
///        `setupAndCheckAvailability` / `renderOLED` sequence `reset_settings::Reset::selectButtonPress` runs, minus
///        the `openUI`, on a 128x48 canvas.
std::string legacy_reset_pbm(ResetSettingsAction action) {
	static deluge::hid::display::oled_canvas::Canvas canvas;
	canvas.clear();
	deluge::gui::context_menu::resetSettings.setAction(action);
	(void)deluge::gui::context_menu::resetSettings.setupAndCheckAvailability();
	deluge::gui::context_menu::resetSettings.renderOLED(canvas);
	return page_major_to_pbm(canvas.bytes().data(), 128, 48);
}

/// @brief The six witness rows of one reset: `reset/<name>	<column>	<n>`.
void emit_reset(OracleWriter& out, std::string_view name, const rw::ResetWitness& w) {
	const auto values = rw::witness_values(w);
	for (size_t i = 0; i < values.size(); i++) {
		out.row("reset/" + std::string{name}, rw::kWitnessColumns[i], static_cast<int64_t>(values[i]));
	}
}

} // namespace context_menu_scene

} // namespace

// clang-format off
describe freeze_frame("menu oracle freezer: its run", $ {
	it("has a directory to write into and exactly one family to write", _ {
		const char* dir = std::getenv("MENU_ORACLE_OUT");
		expect(dir != nullptr && dir[0] != '\0').to_be_true();
		const char* family = std::getenv("MENU_ORACLE_FAMILIES");
		expect(family != nullptr && std::string_view{family}.find(',') == std::string_view::npos).to_be_true();
	});
});
// clang-format on

// clang-format off
describe freeze_tables("menu oracle freezer: the batch-1 roots, the sound root, the ten roots, the setting rows and the shortcut layouts", $ {
	it("writes tables_1.tsv", _ {
		if (!writes("tables_1")) {
			return;
		}
		freeze_problems().clear();
		OracleWriter out{"tables_1"};
		tables_1::freeze(out);
		expect(freeze_problems()).to_equal(std::string{});
		expect(out.duplicates()).to_equal(std::string{});
		expect(out.size() > size_t{800}).to_be_true();
		expect(out.flush()).to_be_true();
	});

	it("writes tables_3.tsv", _ {
		if (!writes("tables_3")) {
			return;
		}
		freeze_problems().clear();
		OracleWriter out{"tables_3"};
		tables_3::freeze(out);
		expect(freeze_problems()).to_equal(std::string{});
		expect(out.duplicates()).to_equal(std::string{});
		expect(out.size() > size_t{9000}).to_be_true();
		expect(out.flush()).to_be_true();
	});

	it("writes descriptors.tsv", _ {
		if (!writes("descriptors")) {
			return;
		}
		freeze_problems().clear();
		OracleWriter out{"descriptors"};
		descriptors::freeze(out);
		expect(freeze_problems()).to_equal(std::string{});
		expect(out.duplicates()).to_equal(std::string{});
		expect(out.size() > size_t{3000}).to_be_true();
		expect(out.flush()).to_be_true();
	});

	it("writes shortcuts.tsv", _ {
		if (!writes("shortcuts")) {
			return;
		}
		freeze_problems().clear();
		OracleWriter out{"shortcuts"};
		shortcuts::freeze(out);
		expect(freeze_problems()).to_equal(std::string{});
		expect(out.duplicates()).to_equal(std::string{});
		expect(out.size() > size_t{2000}).to_be_true();
		expect(out.flush()).to_be_true();
	});

	it("writes tables_2.tsv", _ {
		if (!writes("tables_2")) {
			return;
		}
		freeze_problems().clear();
		OracleWriter out{"tables_2"};
		tables_2::freeze(out);
		expect(freeze_problems()).to_equal(std::string{});
		expect(out.duplicates()).to_equal(std::string{});
		expect(out.size() > size_t{2400}).to_be_true();
		expect(out.flush()).to_be_true();
	});

	it("writes menu_ranges.tsv and its panels", _ {
		if (!writes("menu_ranges")) {
			return;
		}
		freeze_problems().clear();
		std::array<FlashStorage::DefaultRange, FlashStorage::NUM_DEFAULT_RANGE_SLOTS> saved_ranges{};
		std::ranges::copy(FlashStorage::defaultRanges, saved_ranges.begin());
		const bool saved_sharps = FlashStorage::defaultUseSharps;
		const auto saved_edge = soundEditor.editingRangeEdge;
		OracleWriter out{"menu_ranges"};
		const bool panels = menu_ranges::freeze(out);
		std::ranges::copy(saved_ranges, FlashStorage::defaultRanges);
		FlashStorage::defaultUseSharps = saved_sharps;
		soundEditor.editingRangeEdge = saved_edge;
		expect(panels).to_be_true();
		expect(freeze_problems()).to_equal(std::string{});
		expect(out.duplicates()).to_equal(std::string{});
		expect(out.size() > size_t{200}).to_be_true();
		expect(out.flush()).to_be_true();
	});

	it("writes menu_scales.tsv and its panels", _ {
		if (!writes("menu_scales")) {
			return;
		}
		freeze_problems().clear();
		OracleWriter out{"menu_scales"};
		const bool panels = menu_scales::freeze(out);
		expect(panels).to_be_true();
		expect(freeze_problems()).to_equal(std::string{});
		expect(out.duplicates()).to_equal(std::string{});
		expect(out.size() > size_t{20}).to_be_true();
		expect(out.flush()).to_be_true();
	});

	it("writes midi_device.tsv", _ {
		if (!writes("midi_device")) {
			return;
		}
		freeze_problems().clear();
		OracleWriter out{"midi_device"};
		midi_device::freeze(out);
		expect(freeze_problems()).to_equal(std::string{});
		expect(out.duplicates()).to_equal(std::string{});
		expect(out.size() > size_t{100}).to_be_true();
		expect(out.flush()).to_be_true();
	});

	it("writes menu_actions.tsv", _ {
		if (!writes("menu_actions")) {
			return;
		}
		freeze_problems().clear();
		OracleWriter out{"menu_actions"};
		menu_actions::freeze(out);
		expect(freeze_problems()).to_equal(std::string{});
		expect(out.duplicates()).to_equal(std::string{});
		expect(out.size() > size_t{4}).to_be_true();
		expect(out.flush()).to_be_true();
	});

	it("writes output_items.tsv and its panels", _ {
		if (!writes("output_items")) {
			return;
		}
		freeze_problems().clear();
		OracleWriter out{"output_items"};
		const bool panels = output_items::freeze(out);
		expect(panels).to_be_true();
		expect(freeze_problems()).to_equal(std::string{});
		expect(out.duplicates()).to_equal(std::string{});
		expect(out.size() > size_t{20}).to_be_true();
		expect(out.flush()).to_be_true();
	});

	it("writes sample_items.tsv", _ {
		if (!writes("sample_items")) {
			return;
		}
		freeze_problems().clear();
		OracleWriter out{"sample_items"};
		sample_items::freeze(out);
		expect(freeze_problems()).to_equal(std::string{});
		expect(out.duplicates()).to_equal(std::string{});
		expect(out.size() > size_t{15}).to_be_true();
		expect(out.flush()).to_be_true();
	});

	it("writes dx_pages.tsv and its panels", _ {
		if (!writes("dx_pages")) {
			return;
		}
		freeze_problems().clear();
		OracleWriter out{"dx_pages"};
		const bool panels = dx_pages::freeze(out);
		expect(panels).to_be_true();
		expect(freeze_problems()).to_equal(std::string{});
		expect(out.duplicates()).to_equal(std::string{});
		expect(out.size() > size_t{10000}).to_be_true();
		expect(out.flush()).to_be_true();
	});

	it("writes dx_cartridge.tsv and its panels", _ {
		if (!writes("dx_cartridge")) {
			return;
		}
		freeze_problems().clear();
		OracleWriter out{"dx_cartridge"};
		const bool panels = dx_cartridge::freeze(out);
		expect(panels).to_be_true();
		expect(freeze_problems()).to_equal(std::string{});
		expect(out.duplicates()).to_equal(std::string{});
		expect(out.size() > size_t{1}).to_be_true();
		expect(out.flush()).to_be_true();
	});

	it("writes song_settings_edit.tsv", _ {
		if (!writes("song_settings_edit")) {
			return;
		}
		freeze_problems().clear();
		OracleWriter out{"song_settings_edit"};
		song_settings_edit::freeze(out);
		expect(freeze_problems()).to_equal(std::string{});
		expect(out.duplicates()).to_equal(std::string{});
		expect(out.size() > size_t{1}).to_be_true();
		expect(out.flush()).to_be_true();
	});
});
// clang-format on

// clang-format off
describe freeze_context_menu_scene("menu oracle freezer: the settings resets", $ {
	it("writes context_menu_scene.tsv and the five confirmation panels", _ {
		if (!writes("context_menu_scene")) {
			return;
		}
		namespace cm = context_menu_scene;
		OracleWriter out{"context_menu_scene"};
		// The spec's order: flash, MIDI follow, community features, MIDI devices. The full factory reset is not run:
		// it rebuilds the process's MIDI singletons under everything after it.
		const cm::rw::ResetWitness entry = cm::rw::witness();
		{
			deluge::scenes::spec::ErasedFlash flash; // the flash reset programs the settings sector
			cm::emit_reset(out, "flash", cm::run_legacy_reset(cm::ResetSettingsAction::Flash));
			cm::rw::restore(entry);
		}
		cm::emit_reset(out, "midi_follow", cm::run_legacy_reset(cm::ResetSettingsAction::MidiFollow));
		cm::rw::restore(entry);
		cm::emit_reset(out, "community_features", cm::run_legacy_reset(cm::ResetSettingsAction::CommunityFeatures));
		cm::rw::restore(entry);
		cm::emit_reset(out, "midi_devices", cm::run_legacy_reset(cm::ResetSettingsAction::MidiDevices));
		cm::rw::restore(entry);

		const std::pair<std::string_view, cm::ResetSettingsAction> kPanels[] = {
		    {"flash", cm::ResetSettingsAction::Flash},
		    {"community_features", cm::ResetSettingsAction::CommunityFeatures},
		    {"midi_follow", cm::ResetSettingsAction::MidiFollow},
		    {"midi_devices", cm::ResetSettingsAction::MidiDevices},
		    {"all", cm::ResetSettingsAction::All},
		};
		bool panels = true;
		for (const auto& [name, action] : kPanels) {
			cm::rw::pin_panel_globals();
			panels = write_pixels(std::string{"context_menu_reset_"} + std::string{name}, cm::legacy_reset_pbm(action))
			         && panels;
		}
		expect(panels).to_be_true();
		expect(out.duplicates()).to_equal(std::string{});
		expect(out.size() > size_t{23}).to_be_true();
		expect(out.flush()).to_be_true();
	});
});
// clang-format on

// clang-format off
describe freeze_clip_shortcut_overview("menu oracle freezer: the clip view's shortcut overview", $ {
	it("writes clip_shortcut_overview.tsv", _ {
		if (!writes("clip_shortcut_overview")) {
			return;
		}
		namespace co = deluge::scenes::spec::clip_overview;
		OracleWriter out{"clip_shortcut_overview"};
		for (const co::CaseInfo& c : co::kCases) {
			co::RootGuard root;
			ClipView& view = co::set_up(c.id);
			RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
			uint8_t occupancy[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
			// What `ClipView::maybeRenderShortcutsOverview` draws while SHIFT is held. `getModControllableAudioOrNone` is
			// protected on `ClipView` and public on `InstrumentClipView`; the audio clip view keeps `ClipView`'s null.
			ModControllableAudio* forThing =
			    &view == &instrumentClipView ? instrumentClipView.getModControllableAudioOrNone() : nullptr;
			if (forThing != nullptr) {
				// Legacy's relevance also reads the editor's own members (`FilterInfo::getMode` reads
				// `currentModControllable`, `editingKitAffectEntire` reads `setupKitGlobalFXMenu`, the kit-global latch),
				// which hold what the menu last edited: here this clip as the clip view's own affect-entire resolves it.
				// The latch is each case's own (the `kit_affect_entire` cases with it set, as a pad press leaves it; the
				// `_unlatched` ones with it clear, as a menu close leaves it), kept across the legacy context's write.
				const bool latch = deluge::gui::ui::shellKitGlobalFx();
				const menu::MenuContext ctx =
				    menu::context_for(*currentSong, deluge::gui::menuViewOf(getCurrentClip()), 0, false, false, false,
				                      SoundEditor::shouldEditKitAffectEntire());
				install_legacy_context(ctx, spec::sound_allows_note_tails(ctx));
				deluge::gui::ui::shellKitGlobalFxLatch() = latch; // the legacy context writes `setupKitGlobalFXMenu`, the latch
			}
			SoundEditor::renderMainShortcutsOnly(forThing, image, occupancy, SoundEditor::shouldEditKitAffectEntire());
			const std::string path = "overview/" + std::string{c.name};
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				std::vector<std::string> colours;
				std::vector<std::string> occupied;
				for (int32_t x = 0; x < kDisplayWidth; x++) {
					colours.push_back(co::rgb_text(image[y][x]));
					occupied.push_back(std::to_string(occupancy[y][x]));
				}
				out.row(path, "rgb/" + std::to_string(y), std::move(colours));
				out.row(path, "occupancy/" + std::to_string(y), std::move(occupied));
			}
		}
		expect(out.duplicates()).to_equal(std::string{});
		expect(out.size()).to_equal(std::size(co::kCases) * size_t{16}); // eight colour and eight occupancy rows each
		expect(out.flush()).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(freeze_menu_oracles_spec, freeze_frame, freeze_tables, freeze_context_menu_scene,
            freeze_clip_shortcut_overview /*, one family per later task, appended in task order */)
