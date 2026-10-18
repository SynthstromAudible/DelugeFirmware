#pragma once

/// @file
/// @brief The setting rows a menu shows, each with the legacy menu item that shows it, and the named skips: what
///        `descriptor_legacy_spec.cpp` and `setting_names_spec.cpp` both walk.
///
/// @warning Include this BEFORE `<cppspec.hpp>` (see `card_access.h`); it does so itself, in that order.

#include "engine-api/types/arp.h"
#include "engine-api/types/setting.h"
#include "engines/deluge/settings.h"
#include "engines/effects/settings.h"

#include "deluge-project/setting_group.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "memory/heaps.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/edits/arp_setting_for.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/project_bridge/setting_projection.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "scenes/menu/context.h"

// The legacy items' headers, in `gui/ui/menus.cpp`'s own order: several reach names (`OLED`, `oled_canvas`, `Icon`,
// `FormattedTitle`) only through a header an earlier one includes there.
#include "hid/display/oled.h"
namespace deluge::gui::menu_item {
using deluge::hid::display::OLED;
} // namespace deluge::gui::menu_item
#include "gui/l10n/setting_names.h"
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
#include "gui/menu_item/battery/level.h"
#include "gui/menu_item/bend_range/main.h"
#include "gui/menu_item/bend_range/per_finger.h"
#include "gui/menu_item/colour.h"
#include "gui/menu_item/cpu/voice_usage.h"
#include "gui/menu_item/cv/cv2Mapping.h"
#include "gui/menu_item/cv/selection.h"
#include "gui/menu_item/cv/submenu.h"
#include "gui/menu_item/cv/transpose.h"
#include "gui/menu_item/cv/volts.h"
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
#include "gui/menu_item/dx/engine_select.h"
#include "gui/menu_item/dx/global_params.h"
#include "gui/menu_item/edit_name.h"
#include "gui/menu_item/envelope/envelope_menu.h"
#include "gui/menu_item/envelope/segment.h"
#include "gui/menu_item/eq/eq_menu.h"
#include "gui/menu_item/eq/eq_unpatched_param.h"
#include "gui/menu_item/file_selector.h"
#include "gui/menu_item/filter/filter_container.h"
#include "gui/menu_item/filter/mode.h"
#include "gui/menu_item/filter/param.h"
#include "gui/menu_item/filter_route.h"
#include "gui/menu_item/firmware/version.h"
#include "gui/menu_item/flash/status.h"
#include "gui/menu_item/fx/clipping.h"
#include "gui/menu_item/gate/mode.h"
#include "gui/menu_item/gate/off_time.h"
#include "gui/menu_item/gate/selection.h"
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
#include "gui/menu_item/patch_cable_strength/fixed.h"
#include "gui/menu_item/patch_cables.h"
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
#include "gui/menu_item/record/loop_command.h"
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
#include "gui/menu_item/sequence/direction.h"
#include "gui/menu_item/shortcuts/version.h"
#include "gui/menu_item/sidechain/attack.h"
#include "gui/menu_item/sidechain/release.h"
#include "gui/menu_item/sidechain/send.h"
#include "gui/menu_item/sidechain/shape.h"
#include "gui/menu_item/sidechain/sync.h"
#include "gui/menu_item/sidechain/volume.h"
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
#include "gui/menu_item/unpatched_param.h"
#include "gui/menu_item/unpatched_param/pan.h"
#include "gui/menu_item/voice/polyphony.h"
#include "gui/menu_item/voice/portamento.h"
#include "gui/menu_item/voice/priority.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "model/instrument/non_audio_instrument.h"
#include "modulation/params/param.h"
#include "playback/playback_handler.h"
#include "storage/flash_storage.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "spec/support/spec_main.h"

namespace legacy = deluge::gui::menu_item;
namespace spec = deluge::scenes::spec;

/// @name The legacy items, `gui/ui/menus.cpp`'s globals.
/// @{
extern legacy::voice::PolyphonyType polyphonyMenu;
extern legacy::voice::Priority priorityMenu;
extern legacy::sidechain::Send sidechainSendMenu;
extern legacy::SynthModeSelection synthModeMenu;
extern legacy::MasterTranspose masterTransposeMenu;
extern legacy::osc::Type osc0TypeMenu;
extern legacy::osc::Type osc1TypeMenu;
extern legacy::sample::Transpose source0TransposeMenu;
extern legacy::sample::Transpose source1TransposeMenu;
extern legacy::osc::RetriggerPhase osc0PhaseMenu;
extern legacy::osc::RetriggerPhase osc1PhaseMenu;
extern legacy::sample::Reverse sample0ReverseMenu;
extern legacy::sample::Reverse sample1ReverseMenu;
extern legacy::sample::Repeat sample0RepeatMenu;
extern legacy::sample::Repeat sample1RepeatMenu;
extern legacy::sample::TimeStretch sample0TimeStretchMenu;
extern legacy::sample::TimeStretch sample1TimeStretchMenu;
extern legacy::sample::PitchSpeed sample0PitchSpeedMenu;
extern legacy::sample::PitchSpeed sample1PitchSpeedMenu;
extern legacy::sample::Interpolation sample0InterpolationMenu;
extern legacy::sample::Interpolation sample1InterpolationMenu;
extern legacy::osc::Sync oscSyncMenu;
extern legacy::lfo::Type lfo1TypeMenu;
extern legacy::lfo::Type lfo2TypeMenu;
extern legacy::lfo::Type lfo3TypeMenu;
extern legacy::lfo::Type lfo4TypeMenu;
extern legacy::lfo::Sync lfo1SyncMenu;
extern legacy::lfo::Sync lfo2SyncMenu;
extern legacy::lfo::Sync lfo3SyncMenu;
extern legacy::lfo::Sync lfo4SyncMenu;
extern legacy::modulator::Transpose modulator0TransposeMenu;
extern legacy::modulator::Transpose modulator1TransposeMenu;
extern legacy::osc::RetriggerPhase modulator0PhaseMenu;
extern legacy::osc::RetriggerPhase modulator1PhaseMenu;
extern legacy::modulator::Destination modulatorDestMenu;
extern legacy::unison::CountToStereoSpread numUnisonMenu;
extern legacy::unison::Detune unisonDetuneMenu;
extern legacy::midi::sound::OutputMidiChannel outputMidiChannelMenu;
extern legacy::midi::sound::OutputMidiNoteForDrum outputMidiNoteForDrumMenu;
extern legacy::mod_fx::Type modFXTypeMenu;
extern legacy::filter::FilterModeSelection lpfModeMenu;
extern legacy::filter::FilterModeSelection hpfModeMenu;
extern legacy::FilterRouting filterRoutingMenu;
extern legacy::fx::Clipping clippingMenu;
extern legacy::delay::PingPong delayPingPongMenu;
extern legacy::delay::Type delayTypeMenu;
extern legacy::delay::Sync delaySyncMenu;
extern legacy::sidechain::Attack sidechainAttackMenu;
extern legacy::sidechain::Release sidechainReleaseMenu;
extern legacy::sidechain::Sync sidechainSyncMenu;
extern legacy::audio_compressor::Attack compAttack;
extern legacy::audio_compressor::Release compRelease;
extern legacy::audio_compressor::Ratio compRatio;
extern legacy::audio_compressor::SideHPF compHPF;
extern legacy::audio_compressor::Blend compBlend;
extern legacy::stutter::QuantizedStutter stutterQuantizedMenu;
extern legacy::arpeggiator::Mode arpModeMenu;
extern legacy::arpeggiator::OctaveMode arpOctaveModeMenu;
extern legacy::arpeggiator::NoteMode arpNoteModeMenu;
extern legacy::arpeggiator::NoteModeForDrums arpNoteModeMenuForDrums;
extern legacy::arpeggiator::ArpMpeVelocity arpMpeVelocityMenu;
extern legacy::arpeggiator::Octaves arpOctavesMenu;
extern legacy::arpeggiator::StepRepeat arpStepRepeatMenu;
extern legacy::arpeggiator::ChordType arpChordSimulatorMenuKit;
extern legacy::arpeggiator::Sync arpSyncMenu;
extern legacy::arpeggiator::IncludeInKitArp arpIncludeInKitArpMenu;
extern legacy::arpeggiator::PresetMode arpPresetModeMenu;
extern legacy::randomizer::RandomizerLock randomizerLockMenu;
extern legacy::arpeggiator::midi_cv::Gate arpGateMenuMIDIOrCV;
extern legacy::arpeggiator::midi_cv::Rate arpRateMenuMIDIOrCV;
extern legacy::arpeggiator::midi_cv::Rhythm arpRhythmMenuMIDIOrCV;
extern legacy::arpeggiator::midi_cv::SequenceLength arpSequenceLengthMenuMIDIOrCV;
extern legacy::randomizer::midi_cv::SpreadGate spreadGateMenuMIDIOrCV;
extern legacy::randomizer::midi_cv::SpreadOctave spreadOctaveMenuMIDIOrCV;
extern legacy::randomizer::midi_cv::SpreadVelocity spreadVelocityMenuMIDIOrCV;
extern legacy::randomizer::midi_cv::RatchetAmount ratchetAmountMenuMIDIOrCV;
extern legacy::randomizer::midi_cv::RatchetProbability ratchetProbabilityMenuMIDIOrCV;
extern legacy::randomizer::midi_cv::ChordPolyphony chordPolyphonyMenuMIDIOrCV;
extern legacy::randomizer::midi_cv::ChordProbability chordProbabilityMenuMIDIOrCV;
extern legacy::randomizer::midi_cv::NoteProbability randomizerNoteProbabilityMenuMIDIOrCV;
extern legacy::randomizer::midi_cv::SwapProbability swapProbabilityMenuMIDIOrCV;
extern legacy::randomizer::midi_cv::BassProbability bassProbabilityMenuMIDIOrCV;
extern legacy::randomizer::midi_cv::GlideProbability glideProbabilityMenuMIDIOrCV;
extern legacy::audio_clip::Transpose audioClipTransposeMenu;
extern legacy::AudioInterpolation audioClipInterpolationMenu;
extern legacy::reverb::Pan reverbPanMenu;
extern legacy::sidechain::Attack reverbSidechainAttackMenu;
extern legacy::reverb::RoomSize reverbRoomSizeMenu;
extern legacy::reverb::Damping reverbDampingMenu;
extern legacy::reverb::Width reverbWidthMenu;
extern legacy::reverb::HPF reverbHPFMenu;
extern legacy::reverb::LPF reverbLPFMenu;
extern legacy::reverb::Model reverbModelMenu;
extern legacy::reverb::sidechain::Shape reverbSidechainShapeMenu;
extern legacy::swing::Interval swingIntervalMenu;
extern legacy::audio_clip::Reverse audioClipReverseMenu;
extern legacy::audio_clip::Attack audioClipAttackMenu;
extern legacy::sequence::Direction sequenceDirectionMenu;
extern legacy::midi::MPEYToModWheel mpeyToModWheelMenu;
extern legacy::cv::DualCVSelection cv2SourceMenu;
extern legacy::midi::AftertouchToMono midiAftertouchCollapseMenu;
extern legacy::midi::MPEToMono midiMPECollapseMenu;
extern legacy::midi::device_definition::HideUnlabeled hideUnlabeledCCMenu;
extern legacy::midi::device_definition::Linked midiDeviceLinkedMenu;
extern legacy::audio_clip::AudioSourceSelector audioSourceSelectorMenu;
extern legacy::audio_clip::SpecificSourceOutputSelector specificOutputSelectorMenu;
extern legacy::midi::Bank midiBankMenu;
extern legacy::midi::Sub midiSubMenu;
extern legacy::midi::PGM midiPGMMenu;
extern legacy::bend_range::Main mainBendRangeMenu;
extern legacy::bend_range::PerFinger perFingerBendRangeMenu;
extern legacy::bend_range::PerFinger drumBendRangeMenu;
extern legacy::sidechain::Release reverbSidechainReleaseMenu;
extern legacy::sidechain::Sync reverbSidechainSyncMenu;
/// @}

/// @name The legacy root menus `SoundEditor::setup` opens, and the note-row editor's.
/// @{
extern legacy::Submenu soundEditorRootMenu;
extern legacy::Submenu soundEditorRootMenuDrum;
extern legacy::Submenu soundEditorRootMenuMIDIOrCV;
extern legacy::Submenu soundEditorRootMenuMidiDrum;
extern legacy::Submenu soundEditorRootMenuGateDrum;
extern legacy::Submenu soundEditorRootMenuAudioClip;
extern legacy::Submenu soundEditorRootMenuSongView;
extern legacy::Submenu soundEditorRootMenuKitGlobalFX;
extern legacy::HorizontalMenu noteRowEditorRootMenu;
/// @}

namespace {

using deluge::engine::from_menu;
using deluge::engine::menu_range;
using deluge::engine::SettingContext;
using deluge::engine::SettingInfo;
using deluge::engine::SettingKind;
using deluge::engine::to_menu;
using deluge::scenes::menu::MenuContext;
using deluge::serialization::Value;
using Owner = SettingContext::Owner;
using Range = std::pair<int32_t, int32_t>;

// ---------------------------------------------------------------------------------------------
// The legacy items, read and written as the sound editor does.

/// `Value<T>::writeCurrentValue` is protected; name it through a derived class ([class.protected]), as
/// `tables_2_spec.cpp` does.
template <typename T>
struct ValuePeek : legacy::Value<T> {
	static void write(legacy::Value<T>& value) { (value.*(&ValuePeek::writeCurrentValue))(); }
};

/// `Number::getMinValue` and `getMaxValue` are protected; named the same way.
struct NumberPeek : legacy::Number {
	static Range range_of(const legacy::Number& number) {
		return {(number.*(&NumberPeek::getMinValue))(), (number.*(&NumberPeek::getMaxValue))()};
	}
};

/// The item's menu range: a number's least and greatest value, an enumeration's options, a toggle's two.
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

/// The item's value after its `readCurrentValue`.
int32_t legacy_read(::MenuItem* item) {
	if (auto* v = dynamic_cast<legacy::Value<int32_t>*>(item); v != nullptr) {
		v->readCurrentValue();
		return v->getValue();
	}
	if (auto* v = dynamic_cast<legacy::Value<bool>*>(item); v != nullptr) {
		v->readCurrentValue();
		return v->getValue() ? 1 : 0;
	}
	return std::numeric_limits<int32_t>::min();
}

/// The item's `writeCurrentValue` of menu value @p menu.
void legacy_write(::MenuItem* item, int32_t menu) {
	if (auto* v = dynamic_cast<legacy::Value<int32_t>*>(item); v != nullptr) {
		v->setValue(menu);
		ValuePeek<int32_t>::write(*v);
	}
	else if (auto* b = dynamic_cast<legacy::Value<bool>*>(item); b != nullptr) {
		b->setValue(menu != 0);
		ValuePeek<bool>::write(*b);
	}
}

// ---------------------------------------------------------------------------------------------
// The fixtures and the contexts.

/// Point the `soundEditor` globals at what @p ctx edits, as the sound editor's setup does.
void install(const MenuContext& ctx, Song* song) {
	currentSong = song;
	if (ctx.clip != nullptr) {
		song->setCurrentClip(ctx.clip);
	}
	soundEditor.currentSound = ctx.sound;
	soundEditor.currentModControllable = ctx.mod;
	soundEditor.currentParamManager = ctx.params;
	soundEditor.currentSourceIndex = ctx.source_index;
	soundEditor.currentSource = ctx.source;
	soundEditor.currentMultiRange = ctx.range;
	soundEditor.currentMultiRangeIndex = ctx.range_index;
	soundEditor.currentArpSettings = ctx.arp;
	soundEditor.setupKitGlobalFXMenu = ctx.is_kit_global_fx;
	soundEditor.allowsNoteTails = true;
	currentUIMode = 0;
}

/// Where a case's row is shown, and what the sound there is set up as first.
enum class Where : uint8_t {
	SYNTH,         ///< a synth track, subtractive, both oscillators square
	SYNTH_SAMPLE,  ///< a synth track, subtractive, both oscillators samples
	SYNTH_FM,      ///< a synth track in FM
	SYNTH_RINGMOD, ///< a synth track in ring mod, both oscillators square
	KIT_LANE,      ///< a kit lane's sound, subtractive
	KIT,           ///< the whole kit (affect-entire): its own effects and arp
	MIDI_DRUM,     ///< a kit's MIDI row (its arp's host rows are settings there)
	GATE_DRUM,     ///< a kit's gate row
	MIDI_CLIP,     ///< a MIDI clip (its arp's host rows are settings there)
	CV_CLIP,       ///< a CV clip, both CV channels in use
	AUDIO_CLIP,    ///< an audio clip
	SONG,          ///< the song's own settings
};

struct Synth {
	spec::SongFixture song;
	MenuContext ctx{};
};
Synth& synth() {
	static Synth s = [] {
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		Synth r;
		r.song = spec::make_synth_song();
		r.ctx = deluge::scenes::menu::context_for(*r.song.song, r.song.clip, 0, false, false, false, false);
		return r;
	}();
	return s;
}

struct KitLane {
	spec::SongFixture song;
	SoundDrum* drum = nullptr;
	MenuContext ctx{};
};
KitLane& kit_lane() {
	static KitLane k = [] {
		KitLane r;
		r.song = spec::make_kit_song();
		r.drum = spec::add_drum<SoundDrum>(r.song);
		static_cast<Kit*>(r.song.instrument)->selectedDrum = r.drum;
		r.ctx = deluge::scenes::menu::context_for(*r.song.song, r.song.clip, 0, false, false, false, false);
		return r;
	}();
	return k;
}

/// A kit whose one row is a non-sound drum of type @p T, selected.
template <typename T>
struct NonSoundRow {
	spec::SongFixture song;
	T* drum = nullptr;
};
template <typename T>
NonSoundRow<T>& non_sound_row() {
	static NonSoundRow<T> r = [] {
		NonSoundRow<T> out;
		out.song = spec::make_kit_song();
		out.drum = spec::add_drum<T>(out.song);
		return out;
	}();
	return r;
}

struct MidiClip {
	spec::SongFixture song;
	MenuContext ctx{};
};
MidiClip& midi_clip() {
	static MidiClip m = [] {
		MidiClip r;
		r.song = spec::make_midi_song();
		r.ctx = deluge::scenes::menu::context_for(*r.song.song, r.song.clip, 0, false, false, false, false);
		return r;
	}();
	return m;
}

struct CvClip {
	spec::SongFixture song;
	MenuContext ctx{};
};
CvClip& cv_clip() {
	static CvClip c = [] {
		CvClip r;
		r.song = spec::make_cv_song();
		r.ctx = deluge::scenes::menu::context_for(*r.song.song, r.song.clip, 0, false, false, false, false);
		return r;
	}();
	return c;
}

struct AudioClipRig {
	spec::AudioSongFixture song;
	MenuContext ctx{};
};
AudioClipRig& audio_clip() {
	static AudioClipRig a = [] {
		AudioClipRig r;
		r.song = spec::make_audio_clip_song();
		r.ctx = deluge::scenes::menu::context_for(*r.song.song, r.song.clip, 0, false, false, false, false);
		return r;
	}();
	return a;
}

/// Set @p sound's setting at @p path to the word or number @p word.
void sound_setting(Sound& sound, std::string_view path, std::string_view word) {
	deluge::edits::settingFor(sound, path, Value::makeScalar(std::string(word)));
}

/// The song @p where's rig is in.
Song& song_of(Where where);

/// Resolve the context @p where's rig is in now, as `SoundEditor::setup` would, install it and return it. The song
/// is its own root here (the song view's), where `enter` edits the song from the audio clip's.
const MenuContext& settle(Where where) {
	static MenuContext ctx;
	const auto clip_of = [](Where w) -> Clip* {
		switch (w) {
		case Where::KIT_LANE:
		case Where::KIT:
			return kit_lane().song.clip;
		case Where::MIDI_DRUM:
			return non_sound_row<MIDIDrum>().song.clip;
		case Where::GATE_DRUM:
			return non_sound_row<GateDrum>().song.clip;
		case Where::MIDI_CLIP:
			return midi_clip().song.clip;
		case Where::CV_CLIP:
			return cv_clip().song.clip;
		case Where::AUDIO_CLIP:
			return audio_clip().song.clip;
		case Where::SONG:
			return nullptr;
		default:
			return synth().song.clip;
		}
	};
	Song& song = song_of(where);
	const bool song_root = where == Where::SONG;
	ctx = deluge::scenes::menu::context_for(song, clip_of(where), 0, false, song_root, song_root, where == Where::KIT);
	install(ctx, &song);
	return ctx;
}

/// Set up the sound @p where names, install its context and return it.
const MenuContext& enter(Where where, int8_t source = 0) {
	switch (where) {
	case Where::SYNTH:
	case Where::SYNTH_SAMPLE:
	case Where::SYNTH_FM:
	case Where::SYNTH_RINGMOD: {
		Synth& s = synth();
		Sound& sound = *s.ctx.sound;
		sound_setting(sound, "mode", "subtractive");
		const std::string_view type = where == Where::SYNTH_SAMPLE ? "sample" : "square";
		sound_setting(sound, "osc1/type", type);
		sound_setting(sound, "osc2/type", type);
		if (where == Where::SYNTH_FM) {
			sound_setting(sound, "mode", "fm");
		}
		if (where == Where::SYNTH_RINGMOD) {
			sound_setting(sound, "mode", "ringmod");
		}
		s.ctx = deluge::scenes::menu::context_for(*s.song.song, s.song.clip, source, false, false, false, false);
		install(s.ctx, s.song.song);
		return s.ctx;
	}
	case Where::KIT_LANE: {
		KitLane& k = kit_lane();
		sound_setting(*k.drum, "mode", "subtractive");
		k.ctx = deluge::scenes::menu::context_for(*k.song.song, k.song.clip, source, false, false, false, false);
		install(k.ctx, k.song.song);
		return k.ctx;
	}
	case Where::KIT:
	case Where::MIDI_DRUM:
	case Where::GATE_DRUM:
		return settle(where);
	case Where::MIDI_CLIP: {
		MidiClip& m = midi_clip();
		install(m.ctx, m.song.song);
		return m.ctx;
	}
	case Where::CV_CLIP: {
		CvClip& v = cv_clip();
		install(v.ctx, v.song.song);
		return v.ctx;
	}
	case Where::AUDIO_CLIP:
	case Where::SONG: {
		AudioClipRig& a = audio_clip();
		install(a.ctx, a.song.song);
		return a.ctx;
	}
	}
	return synth().ctx;
}

Song& song_of(Where where) {
	switch (where) {
	case Where::KIT_LANE:
	case Where::KIT:
		return *kit_lane().song.song;
	case Where::MIDI_DRUM:
		return *non_sound_row<MIDIDrum>().song.song;
	case Where::GATE_DRUM:
		return *non_sound_row<GateDrum>().song.song;
	case Where::MIDI_CLIP:
		return *midi_clip().song.song;
	case Where::CV_CLIP:
		return *cv_clip().song.song;
	case Where::AUDIO_CLIP:
	case Where::SONG:
		return *audio_clip().song.song;
	default:
		return *synth().song.song;
	}
}

/// The descriptor context @p where stands for. Which sources hold a loaded file is read off the sound by the caller.
SettingContext context_of(Where where) {
	using Instrument = SettingContext::Instrument;
	SettingContext shown{};
	switch (where) {
	case Where::KIT_LANE:
		shown.owner = Owner::KIT_LANE;
		shown.instrument = Instrument::SOUND_DRUM;
		break;
	case Where::MIDI_DRUM:
		shown.owner = Owner::KIT_LANE;
		shown.instrument = Instrument::MIDI_DRUM;
		break;
	case Where::GATE_DRUM:
		shown.owner = Owner::KIT_LANE;
		shown.instrument = Instrument::GATE_DRUM;
		break;
	case Where::KIT:
		shown.owner = Owner::KIT;
		shown.instrument = Instrument::KIT;
		break;
	case Where::MIDI_CLIP:
		shown.owner = Owner::CLIP;
		shown.instrument = Instrument::MIDI;
		break;
	case Where::CV_CLIP:
		shown.owner = Owner::CLIP;
		shown.instrument = Instrument::CV;
		break;
	case Where::AUDIO_CLIP:
		shown.owner = Owner::CLIP;
		shown.instrument = Instrument::AUDIO;
		break;
	case Where::SONG:
		shown.owner = Owner::SONG;
		break;
	default:
		shown.owner = Owner::TRACK;
		shown.instrument = Instrument::SYNTH;
		break;
	}
	shown.audio_inputs_present = AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn;
	return shown;
}

// ---------------------------------------------------------------------------------------------
// A row's stored value, set through its Edit and read back from its record.

/// Which table a row comes from, and so how its value is set and read.
enum class Family : uint8_t {
	INSTRUMENT,
	EFFECT,
	ARP,
	ARP_HOST,
	AUDIO_CLIP,
	SONG,
	CLIP_VALUES,
	// The groups whose rows are named but not driven by the range, read and write checks.
	NOTE_ROW,
	CLIP,
	INSTRUMENT_DEVICE,
	AUDIO_INPUT,
};

std::span<const SettingInfo> table_of(Family family) {
	switch (family) {
	case Family::INSTRUMENT:
		return deluge::instrument::kSettings;
	case Family::EFFECT:
		return deluge::effects::kEffectSettings;
	case Family::ARP:
		return deluge::engine::kArpSettingRows;
	case Family::ARP_HOST:
		return deluge::engine::kArpHostRows;
	case Family::AUDIO_CLIP:
		return deluge::project::table_of(deluge::project::SettingGroupId::AUDIO_CLIP);
	case Family::SONG:
		return deluge::project::table_of(deluge::project::SettingGroupId::SONG);
	case Family::CLIP_VALUES:
		return deluge::project::table_of(deluge::project::SettingGroupId::CLIP_VALUES);
	case Family::NOTE_ROW:
		return deluge::project::table_of(deluge::project::SettingGroupId::NOTE_ROW);
	case Family::CLIP:
		return deluge::project::table_of(deluge::project::SettingGroupId::CLIP);
	case Family::INSTRUMENT_DEVICE:
		return deluge::project::table_of(deluge::project::SettingGroupId::INSTRUMENT_DEVICE);
	case Family::AUDIO_INPUT:
		return deluge::project::table_of(deluge::project::SettingGroupId::AUDIO_INPUT);
	}
	return {};
}

/// @p stored in its file form: a CHOICE's word, else the number.
Value file_form(const SettingInfo& row, int32_t stored) {
	if (row.kind == SettingKind::CHOICE) {
		return Value::makeScalar(std::string(row.choices[static_cast<size_t>(stored)]));
	}
	return Value::makeNumber(stored);
}

/// @p value as the row stores it: a CHOICE's index, else the number.
std::optional<int32_t> stored_form(const SettingInfo& row, const Value& value) {
	const std::string text = value.str();
	if (row.kind == SettingKind::CHOICE) {
		for (size_t i = 0; i < row.choices.size(); ++i) {
			if (row.choices[i] == text) {
				return static_cast<int32_t>(i);
			}
		}
		return std::nullopt;
	}
	const std::optional<int64_t> n = deluge::engine::whole_number(text);
	return n ? std::optional<int32_t>{static_cast<int32_t>(*n)} : std::nullopt;
}

/// An arp's stored value at @p path, read from the engine's arp as the record projects it.
std::optional<int32_t> arp_field(const ArpeggiatorSettings& arp, std::string_view path) {
	const std::string_view name = path.substr(std::string_view{"arpeggiator/"}.size());
	const auto sync_type_choice = [](SyncType type) {
		return type == SYNC_TYPE_TRIPLET ? 1 : type == SYNC_TYPE_DOTTED ? 2 : 0;
	};
	const std::pair<std::string_view, int64_t> fields[] = {
	    {"arpMode", std::to_underlying(arp.mode)},
	    {"octaveMode", std::to_underlying(arp.octaveMode)},
	    {"noteMode", std::to_underlying(arp.noteMode)},
	    {"mpeVelocity", std::to_underlying(arp.mpeVelocity)},
	    {"numOctaves", arp.numOctaves},
	    {"stepRepeat", arp.numStepRepeats},
	    {"chordType", arp.chordTypeIndex},
	    {"syncLevel", std::to_underlying(arp.syncLevel)},
	    {"syncType", sync_type_choice(arp.syncType)},
	    {"randomizerLock", arp.randomizerLock ? 1 : 0},
	    {"kitArp", arp.includeInKitArp ? 1 : 0},
	    {"preset", std::to_underlying(arp.preset)},
	    {"gate", arp.gate},
	    {"rate", arp.rate},
	    {"rhythm", static_cast<int32_t>(arp.rhythm)},
	    {"sequenceLength", static_cast<int32_t>(arp.sequenceLength)},
	    {"chordPolyphony", static_cast<int32_t>(arp.chordPolyphony)},
	    {"ratchetAmount", static_cast<int32_t>(arp.ratchetAmount)},
	    {"noteProbability", static_cast<int32_t>(arp.noteProbability)},
	    {"bassProbability", static_cast<int32_t>(arp.bassProbability)},
	    {"swapProbability", static_cast<int32_t>(arp.swapProbability)},
	    {"glideProbability", static_cast<int32_t>(arp.glideProbability)},
	    {"chordProbability", static_cast<int32_t>(arp.chordProbability)},
	    {"ratchetProbability", static_cast<int32_t>(arp.ratchetProbability)},
	    {"spreadVelocity", static_cast<int32_t>(arp.spreadVelocity)},
	    {"spreadGate", static_cast<int32_t>(arp.spreadGate)},
	    {"spreadOctave", static_cast<int32_t>(arp.spreadOctave)},
	};
	for (const auto& [field, value] : fields) {
		if (field == name) {
			return static_cast<int32_t>(value);
		}
	}
	return std::nullopt;
}

/// Set @p row's stored value through its Edit, on what @p ctx edits.
void set_stored(Family family, const MenuContext& ctx, Where where, const SettingInfo& row, int32_t stored) {
	const Value value = file_form(row, stored);
	switch (family) {
	case Family::INSTRUMENT:
		deluge::edits::settingFor(*ctx.sound, row.path, value);
		return;
	case Family::EFFECT:
		deluge::edits::effectSettingFor(*ctx.mod, row.path, value);
		return;
	case Family::ARP:
	case Family::ARP_HOST:
		deluge::edits::arpSettingFor(*ctx.arp, row.path, value);
		return;
	case Family::AUDIO_CLIP:
		deluge::edits::audioClipSettingFor(*audio_clip().song.clip, row.path, value);
		return;
	case Family::SONG:
		deluge::edits::songSettingFor(row.path, value);
		return;
	case Family::CLIP_VALUES:
		deluge::edits::clipValueSettingFor(*static_cast<InstrumentClip*>(ctx.clip), row.path, value);
		return;
	case Family::NOTE_ROW: {
		auto& clip = *static_cast<InstrumentClip*>(ctx.clip);
		deluge::edits::noteRowSettingFor(*clip.getNoteRowForDrum(kit_lane().drum), clip, row.path, value);
		return;
	}
	case Family::CLIP:
		deluge::edits::clipSettingFor(*ctx.clip, row.path, value);
		return;
	case Family::INSTRUMENT_DEVICE:
		deluge::edits::trackSettingFor(*ctx.output, row.path, value);
		return;
	case Family::AUDIO_INPUT:
		deluge::edits::audioInputSettingFor(static_cast<AudioOutput&>(*ctx.output), row.path, value);
		return;
	}
	(void)where;
}

/// @p row's stored value on what @p ctx edits, or nothing where the row does not apply.
std::optional<int32_t> get_stored(Family family, const MenuContext& ctx, Where where, const SettingInfo& row) {
	std::optional<Value> value;
	switch (family) {
	case Family::INSTRUMENT:
		value = deluge::instrument::setting_value(*ctx.sound->boundState(), row.path);
		break;
	case Family::EFFECT:
		value = deluge::effects::effect_setting_value(ctx.mod->effects(), row.path);
		break;
	case Family::ARP:
	case Family::ARP_HOST:
		return arp_field(*ctx.arp, row.path);
	case Family::AUDIO_CLIP: {
		AudioClipRig& a = audio_clip();
		const auto address = deluge::bridge::audioClipAddressOf(*a.song.clip, *a.song.song);
		if (!address) {
			return std::nullopt;
		}
		value = deluge::project::read_setting(a.song.song->project,
		                                      {deluge::project::SettingGroupId::AUDIO_CLIP, *address}, row.path);
		break;
	}
	case Family::SONG:
		value = deluge::project::read_setting(
		    song_of(where).project, {deluge::project::SettingGroupId::SONG, deluge::project::SongAddress{}}, row.path);
		break;
	case Family::CLIP_VALUES: {
		Song& song = song_of(where);
		const auto address = deluge::bridge::clipAddressOf(*static_cast<InstrumentClip*>(ctx.clip), song);
		if (!address) {
			return std::nullopt;
		}
		value = deluge::project::read_setting(song.project, {deluge::project::SettingGroupId::CLIP_VALUES, *address},
		                                      row.path);
		break;
	}
	case Family::NOTE_ROW: {
		auto& clip = *static_cast<InstrumentClip*>(ctx.clip);
		NoteRow* note_row = clip.getNoteRowForDrum(kit_lane().drum);
		const auto address = deluge::bridge::noteRowAddressOf(*note_row, clip, song_of(where));
		if (!address) {
			return std::nullopt;
		}
		value = deluge::project::read_setting(song_of(where).project,
		                                      {deluge::project::SettingGroupId::NOTE_ROW, *address}, row.path);
		break;
	}
	case Family::CLIP: {
		const auto address = deluge::bridge::clipAddressOf(*ctx.clip, song_of(where));
		if (!address) {
			return std::nullopt;
		}
		value = deluge::project::read_setting(song_of(where).project, {deluge::project::SettingGroupId::CLIP, *address},
		                                      row.path);
		break;
	}
	case Family::INSTRUMENT_DEVICE:
	case Family::AUDIO_INPUT: {
		const auto address = deluge::bridge::trackAddressOf(*ctx.output, song_of(where));
		if (!address) {
			return std::nullopt;
		}
		const auto group = family == Family::AUDIO_INPUT ? deluge::project::SettingGroupId::AUDIO_INPUT
		                                                 : deluge::project::SettingGroupId::INSTRUMENT_DEVICE;
		value = deluge::project::read_setting(song_of(where).project, {group, *address}, row.path);
		break;
	}
	}
	if (!value) {
		return std::nullopt;
	}
	return stored_form(row, *value);
}

// ---------------------------------------------------------------------------------------------
// The cases.

/// One row and the legacy item that shows it.
struct Case {
	std::string_view path;
	Family family;
	::MenuItem* item;
	Where where{Where::SYNTH};
	int8_t source{0};              ///< the oscillator the item edits
	std::vector<int32_t> stored{}; ///< stored values to read, beyond the row's stored edges and default
	std::vector<Range> pairs{};    ///< two-row kinds: {stored, partner stored} pairs to read
	bool writes{true};             ///< false where the legacy write is not compared (a booted UI, a named exception)
	bool lossy{false};             ///< the record holds the value as a float: a set value reads back rounded
};

/// Legacy writes this spec cannot drive: each reaches a UI global only a booted app has.
///   * `SynthModeSelection::writeCurrentValue` calls `view.setKnobIndicatorLevels()` (`synth_mode.h`);
///   * `arpeggiator::Mode` and `PresetMode` silence notes over `soundEditor.getCurrentModelStack` (`mode.h`,
///     `preset_mode.h`).
constexpr bool kReadOnly = false;

const SettingInfo& row_of(const Case& c) {
	const SettingInfo* row = deluge::engine::find_setting(table_of(c.family), c.path);
	static const SettingInfo kMissing{.path = "<missing>"};
	return row != nullptr ? *row : kMissing;
}

/// @p row's partner row, or nullptr for a one-row kind.
const SettingInfo* partner_of(const Case& c, const SettingInfo& row) {
	const std::string_view partner = deluge::engine::mapping_of(row).partner;
	return partner.empty() ? nullptr : deluge::engine::find_setting(table_of(c.family), partner);
}

std::string show(const Case& c, std::string_view what, int64_t at, int64_t value) {
	return std::string{c.path} + " " + std::string{what} + "(" + std::to_string(at) + ") = " + std::to_string(value);
}

/// The menu values to write: the edges, one in from each, and the middle.
std::vector<int32_t> menu_probes(Range range) {
	std::vector<int32_t> probes{range.first, range.second};
	if (range.second > range.first) {
		probes.push_back(range.first + 1);
		probes.push_back(range.second - 1);
		probes.push_back(static_cast<int32_t>((int64_t{range.first} + range.second) / 2));
	}
	return probes;
}

/// Each numeric case: its range, its reads at the stored probes and its writes at the menu edges.
void check_numeric(auto& self, const Case& c) {
	const MenuContext& ctx = enter(c.where, c.source);
	const SettingInfo& row = row_of(c);
	expect(std::string{c.path} + " has a row").to_equal(std::string{row.path} + " has a row");
	const Range range = menu_range(row);
	expect(std::string{c.path} + " range " + std::to_string(range.first) + ".." + std::to_string(range.second))
	    .to_equal(std::string{c.path} + " range " + std::to_string(legacy_range(c.item).first) + ".."
	              + std::to_string(legacy_range(c.item).second));

	const SettingInfo* partner = partner_of(c, row);
	if (partner == nullptr) {
		const auto [least, greatest] = deluge::engine::stored_bounds(row);
		std::vector<int32_t> probes{least, greatest, row.default_value};
		probes.insert(probes.end(), c.stored.begin(), c.stored.end());
		for (int32_t stored : probes) {
			set_stored(c.family, ctx, c.where, row, stored);
			const std::optional<int32_t> now = get_stored(c.family, ctx, c.where, row);
			const int32_t held = c.lossy ? now.value_or(-99999) : stored;
			expect(show(c, "set", stored, now.value_or(-99999))).to_equal(show(c, "set", stored, held));
			expect(show(c, "read", held, legacy_read(c.item))).to_equal(show(c, "read", held, to_menu(row, held)));
		}
	}
	else {
		for (const auto& [stored, partner_stored] : c.pairs) {
			set_stored(c.family, ctx, c.where, row, stored);
			set_stored(c.family, ctx, c.where, *partner, partner_stored);
			const int64_t at = int64_t{stored} * 1000 + partner_stored;
			expect(show(c, "read", at, legacy_read(c.item)))
			    .to_equal(show(c, "read", at, to_menu(row, stored, partner_stored)));
		}
	}

	if (!c.writes) {
		return;
	}
	// The legacy item's own range bounds its writes: a value past it indexes past its tables.
	for (int32_t menu : menu_probes(legacy_range(c.item))) {
		legacy_write(c.item, menu);
		const std::optional<std::pair<int32_t, int32_t>> want = from_menu(row, menu);
		const int64_t got = get_stored(c.family, ctx, c.where, row).value_or(-99999);
		int64_t expected = want ? want->first : -12345;
		if (c.lossy && want) {
			// The record rounds what either write stores alike: compare what it holds after each.
			set_stored(c.family, ctx, c.where, row, want->first);
			expected = get_stored(c.family, ctx, c.where, row).value_or(-12345);
		}
		expect(show(c, "write", menu, got)).to_equal(show(c, "write", menu, expected));
		if (partner != nullptr) {
			const int64_t got_partner = get_stored(c.family, ctx, c.where, *partner).value_or(-99999);
			expect(show(c, "write partner", menu, got_partner))
			    .to_equal(show(c, "write partner", menu, want ? want->second : -12345));
		}
	}
}

/// The table a @p family row's conditions read: an arp's host rows read its setting rows too.
std::span<const SettingInfo> condition_table_of(Family family) {
	return family == Family::ARP || family == Family::ARP_HOST ? deluge::engine::kArpRows : table_of(family);
}

/// The owner @p ctx edits, read by path as a firmware reader reads it: the value the owner holds whether or not the
/// row applies (a sound's oscillator type in FM included), through the spec's own read path for the family. A path
/// the family's table does not hold is recorded, and `applicable` fails on it rather than reading 0.
struct SpecReader {
	const MenuContext& ctx;
	Where where;
	Family family{Family::INSTRUMENT};
	mutable std::vector<std::string> unknown{};

	[[nodiscard]] deluge::engine::SettingReader reader() const {
		return {.read = [](const void* self, std::string_view path) -> std::optional<int32_t> {
			        const auto& owner = *static_cast<const SpecReader*>(self);
			        const SettingInfo* row = deluge::engine::find_setting(condition_table_of(owner.family), path);
			        if (row == nullptr) {
				        owner.unknown.emplace_back(path);
				        return std::nullopt;
			        }
			        switch (owner.family) {
			        case Family::INSTRUMENT: {
				        const std::optional<int32_t> held =
				            deluge::instrument::held_setting(*owner.ctx.sound->boundState(), path);
				        if (!held) {
					        owner.unknown.emplace_back(path);
				        }
				        return held;
			        }
			        case Family::ARP:
			        case Family::ARP_HOST:
				        return arp_field(*owner.ctx.arp, path);
			        default:
				        return get_stored(owner.family, owner.ctx, owner.where, *row);
			        }
		        },
		        .self = this};
	}
};

/// The choices of @p row that apply in @p shown on the owner @p ctx edits, in order, through the engine's own position
/// helpers. Throws where a condition read a path the owner does not hold, or a position does not round-trip.
std::vector<int32_t> applicable(const SettingInfo& row, const SettingContext& shown, const MenuContext& ctx,
                                Where where) {
	const SpecReader owner{ctx, where};
	const deluge::engine::SettingReader reader = owner.reader();
	std::vector<int32_t> out;
	const int32_t count = deluge::engine::applicable_choice_count(row, shown, reader);
	for (int32_t position = 0; position < count; ++position) {
		const std::optional<int32_t> stored = deluge::engine::choice_at_position(row, shown, reader, position);
		if (!stored || deluge::engine::choice_position(row, shown, reader, *stored) != position) {
			throw std::runtime_error(std::string{row.path} + ": choice position " + std::to_string(position)
			                         + " does not round-trip");
		}
		out.push_back(*stored);
	}
	if (!owner.unknown.empty()) {
		throw std::runtime_error(std::string{row.path} + " reads " + owner.unknown.front()
		                         + ", which its owner does not hold");
	}
	return out;
}

/// The same, on the owner @p where names, set up as `enter` leaves it.
std::vector<int32_t> applicable(const SettingInfo& row, Where where) {
	return applicable(row, context_of(where), enter(where), where);
}

std::string list_of(const std::vector<int32_t>& choices) {
	std::string out;
	for (int32_t choice : choices) {
		out += std::to_string(choice) + ",";
	}
	return out;
}

// ---------------------------------------------------------------------------------------------
// The host's names for a row.

using deluge::l10n::NamesOwner;
using deluge::l10n::SettingNames;
using deluge::project::SettingGroupId;
namespace engine = deluge::engine;

/// @brief The owners whose table a family's rows are in.
[[maybe_unused]] std::vector<NamesOwner> owners_of(Family family) {
	switch (family) {
	case Family::INSTRUMENT:
		return {engine::kDelugeInstrument};
	case Family::EFFECT:
		return {engine::kDelugeInstrument, engine::kGlobalEffectable};
	case Family::ARP:
		return {engine::kDelugeInstrument, SettingGroupId::CLIP_ARP, SettingGroupId::LANE_ARP};
	case Family::ARP_HOST:
		return {SettingGroupId::CLIP_ARP, SettingGroupId::LANE_ARP};
	case Family::AUDIO_CLIP:
		return {SettingGroupId::AUDIO_CLIP};
	case Family::SONG:
		return {SettingGroupId::SONG};
	case Family::CLIP_VALUES:
		return {SettingGroupId::CLIP_VALUES};
	case Family::NOTE_ROW:
		return {SettingGroupId::NOTE_ROW};
	case Family::CLIP:
		return {SettingGroupId::CLIP};
	case Family::INSTRUMENT_DEVICE:
		return {SettingGroupId::INSTRUMENT_DEVICE};
	case Family::AUDIO_INPUT:
		return {SettingGroupId::AUDIO_INPUT};
	}
	return {};
}

[[maybe_unused]] std::string text(deluge::l10n::String id) {
	return std::string{deluge::l10n::getView(id)};
}

/// @brief A title with its `*` replaced by the owner's number, which the path's first digit gives as legacy's
/// formatted titles do.
[[maybe_unused]] std::string title_of(const SettingNames& names, std::string_view path) {
	std::string title = text(names.title);
	const size_t star = title.find('*');
	const size_t digit = path.find_first_of("0123456789");
	if (star != std::string::npos && digit != std::string_view::npos) {
		title[star] = path[digit];
	}
	return title;
}

/// @brief The names table's text for choice @p index, full or short (short falls back to full where there are none).
[[maybe_unused]] std::string choice_text(const SettingNames& names, size_t index, bool short_form) {
	const auto list = short_form && !names.short_choices.empty() ? names.short_choices : names.choices;
	return index < list.size() ? text(list[index]) : std::string{"<no choice>"};
}

/// A CHOICE case in its context: the list's length is the options' count, each applicable choice reads as its
/// position in the list, and each position writes that choice.
void check_choices(auto& self, const Case& c) {
	const MenuContext& ctx = enter(c.where, c.source);
	const SettingInfo& row = row_of(c);
	const std::vector<int32_t> choices = applicable(row, context_of(c.where), ctx, c.where);
	const Range legacy = legacy_range(c.item);
	expect(std::string{c.path} + " offers " + std::to_string(choices.size()) + ": " + list_of(choices))
	    .to_equal(std::string{c.path} + " offers " + std::to_string(legacy.second + 1) + ": " + list_of(choices));
	for (size_t position = 0; position < choices.size(); ++position) {
		set_stored(c.family, ctx, c.where, row, choices[position]);
		expect(show(c, "read", choices[position], legacy_read(c.item)))
		    .to_equal(show(c, "read", choices[position], static_cast<int64_t>(position)));
	}
	if (!c.writes) {
		return;
	}
	for (size_t position = 0; position < choices.size() && static_cast<int32_t>(position) <= legacy.second;
	     ++position) {
		legacy_write(c.item, static_cast<int32_t>(position));
		expect(show(c, "write", static_cast<int64_t>(position), get_stored(c.family, ctx, c.where, row).value_or(-1)))
		    .to_equal(show(c, "write", static_cast<int64_t>(position), choices[position]));
	}
}

void check(auto& self, const Case& c) {
	const SettingInfo& row = row_of(c);
	if (row.kind == SettingKind::CHOICE
	    && deluge::engine::mapping_of(row).kind == deluge::engine::DisplayMappingKind::LINEAR) {
		check_choices(self, c);
	}
	else {
		check_numeric(self, c);
	}
}

constexpr int32_t kQ31Max = std::numeric_limits<int32_t>::max();
constexpr int32_t kQ31Min = std::numeric_limits<int32_t>::min();

/// The sync pairs worth reading: {level, type choice}.
const std::vector<Range> kSyncPairs{{0, 0}, {1, 0}, {9, 0}, {1, 1}, {9, 1}, {1, 2}, {9, 2}};
/// The transpose pairs worth reading: {semitones, cents}.
const std::vector<Range> kTransposePairs{{0, 0}, {-96, 0}, {96, 0}, {2, -50}, {-1, 49}, {1, -50}, {0, 50}};
/// Retrigger phases worth reading: free, the degree boundaries, 180 and 360.
const std::vector<int32_t> kPhases{-1, 0, 11930463, 11930464, 2147483520, -11930720, -256};

// ---------------------------------------------------------------------------------------------
// Each family's cases.

const std::vector<Case>& voice_cases() {
	static const std::vector<Case> cases{
	    {.path = "polyphonic", .family = Family::INSTRUMENT, .item = &polyphonyMenu},
	    {.path = "polyphonic", .family = Family::INSTRUMENT, .item = &polyphonyMenu, .where = Where::KIT_LANE},
	    {.path = "voicePriority", .family = Family::INSTRUMENT, .item = &priorityMenu},
	    {.path = "maxVoices",
	     .family = Family::INSTRUMENT,
	     .item = &legacy::voice::polyphonicVoiceCountMenu,
	     .stored = {1, 16, 17, 126, 127}},
	    {.path = "sideChainSend",
	     .family = Family::INSTRUMENT,
	     .item = &sidechainSendMenu,
	     .where = Where::KIT_LANE,
	     .stored = {21474836, 21474837, 42949673, 1073741823, 2126008811, 2126008812}},
	    {.path = "mode", .family = Family::INSTRUMENT, .item = &synthModeMenu, .writes = kReadOnly},
	    {.path = "transpose", .family = Family::INSTRUMENT, .item = &masterTransposeMenu, .pairs = kTransposePairs},
	};
	return cases;
}

const std::vector<Case>& oscillator_cases() {
	static const std::vector<Case> cases{
	    {.path = "osc1/type", .family = Family::INSTRUMENT, .item = &osc0TypeMenu},
	    {.path = "osc2/type", .family = Family::INSTRUMENT, .item = &osc1TypeMenu, .source = 1},
	    {.path = "osc1/type", .family = Family::INSTRUMENT, .item = &osc0TypeMenu, .where = Where::SYNTH_RINGMOD},
	    {.path = "osc1/type", .family = Family::INSTRUMENT, .item = &osc0TypeMenu, .where = Where::KIT_LANE},
	    {.path = "osc1/transpose",
	     .family = Family::INSTRUMENT,
	     .item = &source0TransposeMenu,
	     .pairs = kTransposePairs},
	    {.path = "osc2/transpose",
	     .family = Family::INSTRUMENT,
	     .item = &source1TransposeMenu,
	     .source = 1,
	     .pairs = kTransposePairs},
	    {.path = "osc1/retrigPhase", .family = Family::INSTRUMENT, .item = &osc0PhaseMenu, .stored = kPhases},
	    {.path = "osc2/retrigPhase",
	     .family = Family::INSTRUMENT,
	     .item = &osc1PhaseMenu,
	     .source = 1,
	     .stored = kPhases},
	    {.path = "osc1/loopMode",
	     .family = Family::INSTRUMENT,
	     .item = &sample0RepeatMenu,
	     .where = Where::SYNTH_SAMPLE},
	    {.path = "osc2/loopMode",
	     .family = Family::INSTRUMENT,
	     .item = &sample1RepeatMenu,
	     .where = Where::SYNTH_SAMPLE,
	     .source = 1},
	    {.path = "osc1/reversed",
	     .family = Family::INSTRUMENT,
	     .item = &sample0ReverseMenu,
	     .where = Where::SYNTH_SAMPLE},
	    {.path = "osc2/reversed",
	     .family = Family::INSTRUMENT,
	     .item = &sample1ReverseMenu,
	     .where = Where::SYNTH_SAMPLE,
	     .source = 1},
	    {.path = "osc1/timeStretchEnable",
	     .family = Family::INSTRUMENT,
	     .item = &sample0PitchSpeedMenu,
	     .where = Where::SYNTH_SAMPLE},
	    {.path = "osc2/timeStretchEnable",
	     .family = Family::INSTRUMENT,
	     .item = &sample1PitchSpeedMenu,
	     .where = Where::SYNTH_SAMPLE,
	     .source = 1},
	    {.path = "osc1/timeStretchAmount",
	     .family = Family::INSTRUMENT,
	     .item = &sample0TimeStretchMenu,
	     .where = Where::SYNTH_SAMPLE},
	    {.path = "osc2/timeStretchAmount",
	     .family = Family::INSTRUMENT,
	     .item = &sample1TimeStretchMenu,
	     .where = Where::SYNTH_SAMPLE,
	     .source = 1},
	    {.path = "osc1/linearInterpolation",
	     .family = Family::INSTRUMENT,
	     .item = &sample0InterpolationMenu,
	     .where = Where::SYNTH_SAMPLE},
	    {.path = "osc2/linearInterpolation",
	     .family = Family::INSTRUMENT,
	     .item = &sample1InterpolationMenu,
	     .where = Where::SYNTH_SAMPLE,
	     .source = 1},
	    {.path = "osc2/oscillatorSync", .family = Family::INSTRUMENT, .item = &oscSyncMenu},
	};
	return cases;
}

const std::vector<Case>& lfo_and_modulator_cases() {
	static const std::vector<Case> cases{
	    {.path = "lfo1/type", .family = Family::INSTRUMENT, .item = &lfo1TypeMenu},
	    {.path = "lfo2/type", .family = Family::INSTRUMENT, .item = &lfo2TypeMenu},
	    {.path = "lfo3/type", .family = Family::INSTRUMENT, .item = &lfo3TypeMenu},
	    {.path = "lfo4/type", .family = Family::INSTRUMENT, .item = &lfo4TypeMenu},
	    {.path = "lfo1/syncLevel", .family = Family::INSTRUMENT, .item = &lfo1SyncMenu, .pairs = kSyncPairs},
	    {.path = "lfo2/syncLevel", .family = Family::INSTRUMENT, .item = &lfo2SyncMenu, .pairs = kSyncPairs},
	    {.path = "lfo3/syncLevel", .family = Family::INSTRUMENT, .item = &lfo3SyncMenu, .pairs = kSyncPairs},
	    {.path = "lfo4/syncLevel", .family = Family::INSTRUMENT, .item = &lfo4SyncMenu, .pairs = kSyncPairs},
	    {.path = "modulator1/transpose",
	     .family = Family::INSTRUMENT,
	     .item = &modulator0TransposeMenu,
	     .where = Where::SYNTH_FM,
	     .pairs = kTransposePairs},
	    {.path = "modulator2/transpose",
	     .family = Family::INSTRUMENT,
	     .item = &modulator1TransposeMenu,
	     .where = Where::SYNTH_FM,
	     .source = 1,
	     .pairs = kTransposePairs},
	    {.path = "modulator1/retrigPhase",
	     .family = Family::INSTRUMENT,
	     .item = &modulator0PhaseMenu,
	     .where = Where::SYNTH_FM,
	     .stored = kPhases},
	    {.path = "modulator2/retrigPhase",
	     .family = Family::INSTRUMENT,
	     .item = &modulator1PhaseMenu,
	     .where = Where::SYNTH_FM,
	     .source = 1,
	     .stored = kPhases},
	    {.path = "modulator2/toModulator1",
	     .family = Family::INSTRUMENT,
	     .item = &modulatorDestMenu,
	     .where = Where::SYNTH_FM},
	};
	return cases;
}

const std::vector<Case>& unison_and_midi_cases() {
	static const std::vector<Case> cases{
	    {.path = "unison/num", .family = Family::INSTRUMENT, .item = &numUnisonMenu},
	    {.path = "unison/detune", .family = Family::INSTRUMENT, .item = &unisonDetuneMenu},
	    {.path = "unison/spread", .family = Family::INSTRUMENT, .item = &legacy::unison::stereoSpreadMenu},
	    {.path = "midiOutput/channel", .family = Family::INSTRUMENT, .item = &outputMidiChannelMenu},
	    {.path = "midiOutput/noteForDrum",
	     .family = Family::INSTRUMENT,
	     .item = &outputMidiNoteForDrumMenu,
	     .where = Where::KIT_LANE},
	};
	return cases;
}

const std::vector<Case>& effect_cases() {
	static const std::vector<Case> cases{
	    {.path = "modFXType", .family = Family::EFFECT, .item = &modFXTypeMenu},
	    {.path = "lpfMode", .family = Family::EFFECT, .item = &lpfModeMenu},
	    {.path = "hpfMode", .family = Family::EFFECT, .item = &hpfModeMenu},
	    {.path = "filterRoute", .family = Family::EFFECT, .item = &filterRoutingMenu},
	    {.path = "clippingAmount", .family = Family::EFFECT, .item = &clippingMenu},
	    {.path = "delay/pingPong", .family = Family::EFFECT, .item = &delayPingPongMenu},
	    {.path = "delay/type", .family = Family::EFFECT, .item = &delayTypeMenu},
	    {.path = "delay/syncLevel", .family = Family::EFFECT, .item = &delaySyncMenu, .pairs = kSyncPairs},
	    {.path = "sidechain/attack",
	     .family = Family::EFFECT,
	     .item = &sidechainAttackMenu,
	     .stored = {327244, 1048576, 256, 255}},
	    {.path = "sidechain/release",
	     .family = Family::EFFECT,
	     .item = &sidechainReleaseMenu,
	     .stored = {936, 261528, 256, 264}},
	    {.path = "sidechain/syncLevel",
	     .family = Family::EFFECT,
	     .item = &sidechainSyncMenu,
	     .pairs = {{0, 0}, {1, 0}, {9, 0}, {4, 0}}},
	    {.path = "audioCompressor/attack",
	     .family = Family::EFFECT,
	     .item = &compAttack,
	     .stored = {5 << 24, (5 << 24) + 0xFFFFFF, 127 << 24}},
	    {.path = "audioCompressor/release",
	     .family = Family::EFFECT,
	     .item = &compRelease,
	     .stored = {5 << 24, 127 << 24}},
	    {.path = "audioCompressor/ratio", .family = Family::EFFECT, .item = &compRatio, .stored = {64 << 24, 1}},
	    {.path = "audioCompressor/compHPF", .family = Family::EFFECT, .item = &compHPF, .stored = {(3 << 24) - 1}},
	    {.path = "audioCompressor/compBlend", .family = Family::EFFECT, .item = &compBlend, .stored = {127 << 24}},
	    {.path = "stutter/quantized", .family = Family::EFFECT, .item = &stutterQuantizedMenu},
	};
	return cases;
}

const std::vector<Case>& arp_cases() {
	static const std::vector<Case> cases{
	    {.path = "arpeggiator/arpMode", .family = Family::ARP, .item = &arpModeMenu, .writes = kReadOnly},
	    {.path = "arpeggiator/octaveMode", .family = Family::ARP, .item = &arpOctaveModeMenu},
	    {.path = "arpeggiator/noteMode", .family = Family::ARP, .item = &arpNoteModeMenu},
	    {.path = "arpeggiator/noteMode",
	     .family = Family::ARP,
	     .item = &arpNoteModeMenuForDrums,
	     .where = Where::KIT_LANE},
	    {.path = "arpeggiator/mpeVelocity", .family = Family::ARP, .item = &arpMpeVelocityMenu},
	    {.path = "arpeggiator/numOctaves", .family = Family::ARP, .item = &arpOctavesMenu},
	    {.path = "arpeggiator/stepRepeat", .family = Family::ARP, .item = &arpStepRepeatMenu},
	    {.path = "arpeggiator/chordType",
	     .family = Family::ARP,
	     .item = &arpChordSimulatorMenuKit,
	     .where = Where::KIT_LANE},
	    {.path = "arpeggiator/syncLevel", .family = Family::ARP, .item = &arpSyncMenu, .pairs = kSyncPairs},
	    {.path = "arpeggiator/randomizerLock", .family = Family::ARP, .item = &randomizerLockMenu},
	    {.path = "arpeggiator/kitArp",
	     .family = Family::ARP,
	     .item = &arpIncludeInKitArpMenu,
	     .where = Where::KIT_LANE},
	    {.path = "arpeggiator/preset", .family = Family::ARP, .item = &arpPresetModeMenu, .writes = kReadOnly},
	};
	return cases;
}

/// The host rows' standard and unsigned values worth reading.
const std::vector<int32_t> kQ31Probes{0, -1, 1, 85899345, 42949672, 42949673, kQ31Min + 42949672};

const std::vector<Case>& arp_host_cases() {
	static const std::vector<Case> cases = [] {
		const std::pair<std::string_view, ::MenuItem*> items[] = {
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
		};
		std::vector<Case> out;
		for (const auto& [path, item] : items) {
			out.push_back({.path = path,
			               .family = Family::ARP_HOST,
			               .item = item,
			               .where = Where::MIDI_CLIP,
			               .stored = kQ31Probes});
		}
		return out;
	}();
	return cases;
}

/// Q31 fractions either side of each rounding edge a fiftieths menu has, (k - 1/2) / 50 of 2^31, for a few k.
const std::vector<int32_t> kFractionProbes = [] {
	std::vector<int32_t> out;
	for (int32_t k : {1, 13, 25, 37, 49, 50}) {
		const int64_t edge = ((2 * int64_t{k} - 1) << 31) / 100;
		for (int64_t d : {-256, -128, -1, 0, 1, 128, 256}) {
			out.push_back(static_cast<int32_t>(edge + d));
		}
	}
	return out;
}();

const std::vector<Case>& model_cases() {
	static const std::vector<Case> cases{
	    {.path = "transpose",
	     .family = Family::AUDIO_CLIP,
	     .item = &audioClipTransposeMenu,
	     .where = Where::AUDIO_CLIP,
	     .pairs = kTransposePairs},
	    {.path = "linearInterpolation",
	     .family = Family::AUDIO_CLIP,
	     .item = &audioClipInterpolationMenu,
	     .where = Where::AUDIO_CLIP},
	    {.path = "reverb/roomSize",
	     .family = Family::SONG,
	     .item = &reverbRoomSizeMenu,
	     .where = Where::SONG,
	     .stored = kFractionProbes,
	     .lossy = true},
	    {.path = "reverb/dampening",
	     .family = Family::SONG,
	     .item = &reverbDampingMenu,
	     .where = Where::SONG,
	     .stored = kFractionProbes,
	     .lossy = true},
	    {.path = "reverb/width",
	     .family = Family::SONG,
	     .item = &reverbWidthMenu,
	     .where = Where::SONG,
	     .stored = kFractionProbes,
	     .lossy = true},
	    {.path = "reverb/hpf",
	     .family = Family::SONG,
	     .item = &reverbHPFMenu,
	     .where = Where::SONG,
	     .stored = kFractionProbes,
	     .lossy = true},
	    {.path = "reverb/lpf",
	     .family = Family::SONG,
	     .item = &reverbLPFMenu,
	     .where = Where::SONG,
	     .stored = kFractionProbes,
	     .lossy = true},
	    {.path = "reverb/pan",
	     .family = Family::SONG,
	     .item = &reverbPanMenu,
	     .where = Where::SONG,
	     .stored = {0, 1, -1, 85899344}},
	    {.path = "reverb/model", .family = Family::SONG, .item = &reverbModelMenu, .where = Where::SONG},
	    {.path = "reverb/compressor/shape",
	     .family = Family::SONG,
	     .item = &reverbSidechainShapeMenu,
	     .where = Where::SONG,
	     .stored = {0, -601295438, 85899345},
	     .writes = false},
	    {.path = "swingInterval", .family = Family::SONG, .item = &swingIntervalMenu, .where = Where::SONG},
	    {.path = "priority", .family = Family::AUDIO_CLIP, .item = &priorityMenu, .where = Where::AUDIO_CLIP},
	    {.path = "reversed", .family = Family::AUDIO_CLIP, .item = &audioClipReverseMenu, .where = Where::AUDIO_CLIP},
	    {.path = "pitchSpeedIndependent",
	     .family = Family::AUDIO_CLIP,
	     .item = &sample0PitchSpeedMenu,
	     .where = Where::AUDIO_CLIP},
	    {.path = "attack",
	     .family = Family::AUDIO_CLIP,
	     .item = &audioClipAttackMenu,
	     .where = Where::AUDIO_CLIP,
	     .stored = {0, -1, 1, 85899345},
	     .writes = false},
	    {.path = "midiBank", .family = Family::CLIP_VALUES, .item = &midiBankMenu, .where = Where::MIDI_CLIP},
	    {.path = "midiSub", .family = Family::CLIP_VALUES, .item = &midiSubMenu, .where = Where::MIDI_CLIP},
	    {.path = "midiPGM", .family = Family::CLIP_VALUES, .item = &midiPGMMenu, .where = Where::MIDI_CLIP},
	    {.path = "bendRange", .family = Family::CLIP_VALUES, .item = &mainBendRangeMenu},
	    {.path = "bendRangeMPE", .family = Family::CLIP_VALUES, .item = &perFingerBendRangeMenu},
	    {.path = "reverb/compressor/attack",
	     .family = Family::SONG,
	     .item = &reverbSidechainAttackMenu,
	     .where = Where::SONG,
	     .stored = {327244, 1048576, 256, 255}},
	    {.path = "reverb/compressor/release",
	     .family = Family::SONG,
	     .item = &reverbSidechainReleaseMenu,
	     .where = Where::SONG,
	     .stored = {936, 261528, 256, 264}},
	};
	return cases;
}

// ---------------------------------------------------------------------------------------------
// The named skips: rows with a menu but no single legacy item to compare, and why.

struct Skip {
	std::string_view path;
	std::string_view reason;
};

constexpr Skip kSkips[] = {
    {"cents", "the master transpose's partner: shown only within `transpose`'s menu value"},
    {"osc1/cents", "osc 1 transpose's partner: shown only within its menu value"},
    {"osc2/cents", "osc 2 transpose's partner: shown only within its menu value"},
    {"modulator1/cents", "modulator 1 transpose's partner: shown only within its menu value"},
    {"modulator2/cents", "modulator 2 transpose's partner: shown only within its menu value"},
    {"lfo1/syncType", "LFO 1 sync's partner: shown only within its combined sync menu"},
    {"lfo2/syncType", "LFO 2 sync's partner: shown only within its combined sync menu"},
    {"lfo3/syncType", "LFO 3 sync's partner: shown only within its combined sync menu"},
    {"lfo4/syncType", "LFO 4 sync's partner: shown only within its combined sync menu"},
    {"delay/syncType", "the delay sync's partner: shown only within its combined sync menu"},
    {"sidechain/syncType", "the sidechain sync's partner: shown only within its combined sync menu"},
    {"arpeggiator/syncType", "the arp sync's partner: shown only within its combined sync menu"},
    {"osc1/sampleRanges", "structured: the sample browser and range editors, no value menu"},
    {"osc2/sampleRanges", "structured: the sample browser and range editors, no value menu"},
    {"osc1/wavetableRanges", "structured: the wavetable browser, no value menu"},
    {"osc2/wavetableRanges", "structured: the wavetable browser, no value menu"},
    {"osc1/fileName", "structured: the sample browser, no value menu"},
    {"osc2/fileName", "structured: the sample browser, no value menu"},
    {"osc1/zone", "structured: the sample marker editor, no value menu"},
    {"osc2/zone", "structured: the sample marker editor, no value menu"},
    {"osc1/dx7patch", "structured: the DX7 editor, no value menu"},
    {"osc2/dx7patch", "structured: the DX7 editor, no value menu"},
    {"osc1/dx7enginemode", "structured: the DX7 editor, no value menu"},
    {"osc2/dx7enginemode", "structured: the DX7 editor, no value menu"},
    {"osc1/dx7randomdetune", "structured: the DX7 editor, no value menu"},
    {"osc2/dx7randomdetune", "structured: the DX7 editor, no value menu"},
    {"stutter/reverse",
     "one of three rows `stutter::StutterDirection` shows as one list (with pingPong, useSongStutter)"},
    {"stutter/pingPong",
     "one of three rows `stutter::StutterDirection` shows as one list (with reverse, useSongStutter)"},
    {"stutter/useSongStutter",
     "one of three rows `stutter::StutterDirection` shows as one list (with reverse, pingPong); its first entry "
     "appears only off the song"},
    {"audioCompressor/thresh", "no menu item: a sound's threshold is a param; the knob gesture writes the setting"},
    {"arpeggiator/reverseProbability", "no MIDI or CV item: the reverse probability is a param-only menu"},
    {"reverb/compressor/syncLevel",
     "its menu shows one value over this level and the engine's own sync type, which no row of the song holds"},
    {"reverb/compressor/volume", "a named exception: menu -1..50 truncates (stored / 21474836)"},
    {"syncScalingClip", "no menu: the clip whose length scales the tempo, set by a gesture"},
    {"scales/disabled/", "every preset scale's row: one active-scales menu shows them all"},
    {"startSamplePos", "the sample marker editor, no value menu"},
    {"endSamplePos", "the sample marker editor, no value menu"},
    {"overdubsShouldCloneAudioTrack", "no menu: a recording gesture sets it"},
};

/// The rows a legacy item shows but this spec reads only through named exceptions, and why.
constexpr Skip kExceptions[] = {
    {"midiOutput/noteForDrum",
     "unset (stored 255, outside the row) shows kNoteForDrum (60); the descriptor maps only 0..127"},
    {"osc1/type",
     "with no input plugged in, the one input entry is INPUT_L under the name \"INPUT\"; a stored INPUT_R or "
     "INPUT_STEREO then reads past the list"},
    {"osc2/type", "as osc1/type"},
    {"attack",
     "the AUDIO_CLIP group's: the read is STANDARD; legacy's write stores computeCurrentValueForStandardMenuItem of "
     "the menu value (menu 50 stores 25), a buggy write"},
    {"reverb/compressor/shape",
     "the read is STANDARD; legacy's write omits the top step's special case (menu 50 stores 2147483602, not "
     "2147483647)"},
    {"swingInterval", "the encoder wraps over 1..9 and never offers 0 (off), though the list is 0..9"},
    {"reverb/compressor/volume", "menu -1..50 truncates (stored / 21474836); no mapping kind expresses it"},
};

/// Whether @p path is a named skip; a skip ending in '/' names every path under it.
bool skipped(std::string_view path) {
	for (const Skip& skip : kSkips) {
		if (skip.path == path || (skip.path.ends_with('/') && path.starts_with(skip.path))) {
			return true;
		}
	}
	return false;
}

/// The rows of the groups the range, read and write checks do not drive, each with the legacy item that shows it. They
/// are named and compared by name only.
const std::vector<Case>& group_cases() {
	static const std::vector<Case> cases{
	    {.path = "sequenceDirection", .family = Family::CLIP, .item = &sequenceDirectionMenu},
	    {.path = "sequenceDirection",
	     .family = Family::NOTE_ROW,
	     .item = &sequenceDirectionMenu,
	     .where = Where::KIT_LANE},
	    {.path = "bendRange", .family = Family::NOTE_ROW, .item = &drumBendRangeMenu, .where = Where::KIT_LANE},
	    {.path = "outputMpeY",
	     .family = Family::INSTRUMENT_DEVICE,
	     .item = &mpeyToModWheelMenu,
	     .where = Where::MIDI_CLIP},
	    {.path = "cv2Source", .family = Family::INSTRUMENT_DEVICE, .item = &cv2SourceMenu, .where = Where::CV_CLIP},
	    {.path = "collapseAftertouch",
	     .family = Family::INSTRUMENT_DEVICE,
	     .item = &midiAftertouchCollapseMenu,
	     .where = Where::MIDI_CLIP},
	    {.path = "collapseMpe",
	     .family = Family::INSTRUMENT_DEVICE,
	     .item = &midiMPECollapseMenu,
	     .where = Where::MIDI_CLIP},
	    {.path = "hideUnlabeledCC",
	     .family = Family::INSTRUMENT_DEVICE,
	     .item = &hideUnlabeledCCMenu,
	     .where = Where::MIDI_CLIP},
	    {.path = "deviceDefinitionFileName",
	     .family = Family::INSTRUMENT_DEVICE,
	     .item = &midiDeviceLinkedMenu,
	     .where = Where::MIDI_CLIP},
	    {.path = "mode", .family = Family::AUDIO_INPUT, .item = &audioSourceSelectorMenu, .where = Where::AUDIO_CLIP},
	    {.path = "inputChannel",
	     .family = Family::AUDIO_INPUT,
	     .item = &audioSourceSelectorMenu,
	     .where = Where::AUDIO_CLIP},
	    {.path = "outputRecordingFrom",
	     .family = Family::AUDIO_INPUT,
	     .item = &specificOutputSelectorMenu,
	     .where = Where::AUDIO_CLIP},
	};
	return cases;
}

/// Every case of every family the range, read and write checks drive.
std::vector<const Case*> all_cases() {
	std::vector<const Case*> out;
	for (const auto* family :
	     {&voice_cases(), &oscillator_cases(), &lfo_and_modulator_cases(), &unison_and_midi_cases(), &effect_cases(),
	      &arp_cases(), &arp_host_cases(), &model_cases()}) {
		for (const Case& c : *family) {
			out.push_back(&c);
		}
	}
	return out;
}

/// Every case, the named-only ones included.
std::vector<const Case*> all_name_cases() {
	std::vector<const Case*> out = all_cases();
	for (const Case& c : group_cases()) {
		out.push_back(&c);
	}
	return out;
}

} // namespace
