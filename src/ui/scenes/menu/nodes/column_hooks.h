#pragma once

/// @file
/// @brief Legacy's horizontal-menu overrides that read the value or the context, as `Binding::column_art` and
///        `Binding::value_text` hooks: what a column draws (`renderInHorizontalMenu`), how it is labelled
///        (`getColumnLabel`) and what its notification says (`getNotificationValue`). The overrides that are a fixed
///        string are `Node::column_name`, and `showColumnLabel()`/`showNotification()` are `Node` flags.

#include "ui/scenes/menu/node.h"
#include "ui/widgets/oled/columns.h"

#include <cstdint>
#include <optional>
#include <span>

namespace deluge::ui::scenes::menu {
struct MenuContext;
} // namespace deluge::ui::scenes::menu

namespace deluge::ui::scenes::menu::nodes {

/// @name Column drawings (`Binding::column_art`)
/// @{

/// `delay::PingPong::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/delay/ping_pong.h`): the switch, a
/// row above the region.
std::optional<deluge::ui::widgets::oled::ArtColumn> switch_art(const MenuContext& ctx, int32_t value);
/// `randomizer::RandomizerLock::renderInHorizontalMenu`
/// (`origin/main:src/deluge/gui/menu_item/randomizer/randomizer_lock.h`): the big padlock.
std::optional<deluge::ui::widgets::oled::ArtColumn> lock_art(const MenuContext& ctx, int32_t value);
/// `sample::PitchSpeed::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/sample/pitch_speed.h`): the
/// keyboard, crossed out when pitch and speed are independent.
std::optional<deluge::ui::widgets::oled::ArtColumn> keyboard_art(const MenuContext& ctx, int32_t value);
/// `sample::Reverse` and `audio_clip::Reverse::renderInHorizontalMenu`
/// (`origin/main:src/deluge/gui/menu_item/sample/reverse.h`,
/// `origin/main:src/deluge/gui/menu_item/audio_clip/reverse.h`): the direction arrow, mirrored when reversed.
std::optional<deluge::ui::widgets::oled::ArtColumn> direction_art(const MenuContext& ctx, int32_t value);
/// `lfo::Type::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/lfo/type.h`): the shape's icon.
std::optional<deluge::ui::widgets::oled::ArtColumn> lfo_type_art(const MenuContext& ctx, int32_t value);
/// `osc::Type::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/osc/type.h`) for source 1 and 2: the
/// shape's icon with the analog or supersaw letter.
std::optional<deluge::ui::widgets::oled::ArtColumn> osc0_type_art(const MenuContext& ctx, int32_t value);
std::optional<deluge::ui::widgets::oled::ArtColumn> osc1_type_art(const MenuContext& ctx, int32_t value);
/// `sample::Repeat::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/sample/repeat.h`): the mode's icon,
/// from the region's left.
std::optional<deluge::ui::widgets::oled::ArtColumn> repeat_art(const MenuContext& ctx, int32_t value);
/// `sequence::Direction::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/sequence/direction.h`): the
/// arrow, mirrored for reverse, a mirrored pair for ping-pong; obey-parent is the option's text.
std::optional<deluge::ui::widgets::oled::ArtColumn> sequence_direction_art(const MenuContext& ctx, int32_t value);
/// `arpeggiator::Rhythm` and `midi_cv::Rhythm::renderInHorizontalMenu`
/// (`origin/main:src/deluge/gui/menu_item/arpeggiator/rhythm.h`,
/// `origin/main:src/deluge/gui/menu_item/arpeggiator/midi_cv/rhythm.h`): the pattern spaced out, '0' as X, or the name
/// of pattern 0.
std::optional<deluge::ui::widgets::oled::ArtColumn> arp_rhythm_art(const MenuContext& ctx, int32_t value);
std::optional<deluge::ui::widgets::oled::ArtColumn> arp_rhythm_midi_art(const MenuContext& ctx, int32_t value);
/// `unison::Count::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/unison/count.h`): the count in the
/// "Big" face.
std::optional<deluge::ui::widgets::oled::ArtColumn> unison_count_art(const MenuContext& ctx, int32_t value);
/// `midi::Preset::renderInHorizontalMenu`: NONE at 128, else the number.
std::optional<deluge::ui::widgets::oled::ArtColumn> midi_preset_art(const MenuContext& ctx, int32_t value);
/// `midi::Preset::selectEncoderAction` (`origin/main:src/deluge/gui/menu_item/midi/preset.h`): a turn wraps round
/// `kMidiPresetRange`, 128 being none.
std::optional<int32_t> midi_preset_turn(const MenuContext& ctx, int32_t value, int32_t offset);
/// `midi::Preset::drawInteger`: NONE at 128 on the full screen.
ValueText midi_preset_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// @}

/// @name Column texts (`Binding::value_text`)
/// @{

/// `voice::VoiceCount` and `arpeggiator::SequenceLength`'s OFF at zero (`VoiceCount::getNotificationValue` in
/// `origin/main:src/deluge/gui/menu_item/voice/polyphony.h`, `SequenceLength::renderInHorizontalMenu` and
/// `getNotificationValue` in `origin/main:src/deluge/gui/menu_item/arpeggiator/sequence_length.h`): the column's word
/// and the notification's.
ValueText off_at_zero_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `cv::Volts::drawPixelsForOled`: 0 V/octave is Hz/V.
ValueText cv_volts_word(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `osc::RetriggerPhase` (`origin/main:src/deluge/gui/menu_item/osc/retrigger_phase.h`): OFF while free-running, and a
/// notification of the raw number.
ValueText retrigger_phase_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `reverb::sidechain::Volume` (`origin/main:src/deluge/gui/menu_item/reverb/sidechain/volume.h`): AUTO below zero.
ValueText ducking_auto_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `defaults::HoldTime::getDisplayValue`/`getUnit` (`origin/main:src/deluge/gui/menu_item/defaults/hold_time.h`):
/// twentieths of a second, 1..19 as 50..950 ms, 20 as 1 SEC.
ValueText hold_time_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `defaults::Magnitude::drawPixelsForOled`: the resolution, `96 << value`.
ValueText default_magnitude_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `arpeggiator::midi_cv::Rhythm::drawInteger`: the pattern's name.
ValueText arp_rhythm_name_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `arpeggiator::Rhythm::drawInteger`: "index: pattern", cut to its 12-byte buffer. A leaf text drawn in the digits'
/// place (`ValueStyle::label_in_digits`), not in the huge face.
ValueText arp_rhythm_screen_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `sample::Reverse` and `audio_clip::Reverse::getNotificationValue`
/// (`origin/main:src/deluge/gui/menu_item/sample/reverse.h`): ON or OFF.
ValueText on_off_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `reverb::Width::getColumnLabel` (`origin/main:src/deluge/gui/menu_item/reverb/width.h`): DIFFUSION for the plate
/// models, else the short width.
ValueText reverb_width_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `reverb::RoomSize::getName` (`origin/main:src/deluge/gui/menu_item/reverb/room_size.h`), which its column label
/// reads: TIME for the plate models.
ValueText reverb_room_size_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `sample::Repeat::getColumnLabel` (`origin/main:src/deluge/gui/menu_item/sample/repeat.h`): the mode's name.
ValueText repeat_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `sample::PitchSpeed::getColumnLabel` (`origin/main:src/deluge/gui/menu_item/sample/pitch_speed.h`): LINKED or
/// INDEPENDENT.
ValueText pitch_speed_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `osc::source::Volume::getColumnLabel` (`origin/main:src/deluge/gui/menu_item/osc/source/volume.h`): the first four
/// letters of the formatted name, for source 1 and 2.
ValueText source0_volume_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
ValueText source1_volume_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `source::patched_param::ModulatorLevel::getColumnLabel`
/// (`origin/main:src/deluge/gui/menu_item/source/patched_param/modulator_level.h`): the formatted name from its third
/// letter, for modulator 1 and 2.
ValueText modulator0_volume_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
ValueText modulator1_volume_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `mod_fx::Depth`, `Feedback` and `Offset::getColumnLabel`
/// (`origin/main:src/deluge/gui/menu_item/mod_fx/depth_patched.h`,
/// `origin/main:src/deluge/gui/menu_item/mod_fx/feedback.h`, `origin/main:src/deluge/gui/menu_item/mod_fx/offset.h`):
/// the mod FX type's short name for the knob.
ValueText mod_fx_depth_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
ValueText mod_fx_feedback_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
ValueText mod_fx_offset_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// @}

/// @name The filter pages (`gui/menu_item/filter/`)
/// @{ `filter::FilterContainer` (`origin/main:src/deluge/gui/menu_item/filter/filter_container.h`): the frequency
/// column draws the response across itself and the resonance column.
std::optional<ui::widgets::oled::DrawColumn> lpf_graph_draw(const MenuContext& ctx, int32_t value);
std::optional<ui::widgets::oled::DrawColumn> hpf_graph_draw(const MenuContext& ctx, int32_t value);
std::optional<ui::widgets::oled::DrawColumn> global_lpf_graph_draw(const MenuContext& ctx, int32_t value);
std::optional<ui::widgets::oled::DrawColumn> global_hpf_graph_draw(const MenuContext& ctx, int32_t value);
/// `filter::FilterParam::getColumnLabel` (`origin/main:src/deluge/gui/menu_item/filter/param.h`): the morph's short
/// name for the filter's family -- DRIVE (LP ladder), FM (HP ladder), MORPH (SVF).
ValueText lpf_morph_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
ValueText hpf_morph_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `filter::FilterParam::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/filter/param.h`): a slider on a
/// morphable (SVF) filter, HPF drawn as `50 - value`; the ordinary bar otherwise.
std::optional<ui::widgets::oled::DrawColumn> lpf_morph_draw(const MenuContext& ctx, int32_t value);
std::optional<ui::widgets::oled::DrawColumn> hpf_morph_draw(const MenuContext& ctx, int32_t value);
/// `filter::FilterParam::selectEncoderAction` (`origin/main:src/deluge/gui/menu_item/filter/param.h`): HPF SVF morph
/// turns the other way on a page.
bool hpf_morph_reversed(const MenuContext& ctx);
/// @}

/// @name Pictures (`Binding::column_draw`), each over its 0..50 value
/// @{ `voice::Portamento::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/voice/portamento.h`): the
/// glide between two notes.
std::optional<ui::widgets::oled::DrawColumn> glide_draw(const MenuContext& ctx, int32_t value);
/// `unison::Detune::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/unison/detune.h`): three splaying
/// lines.
std::optional<ui::widgets::oled::DrawColumn> detune_draw(const MenuContext& ctx, int32_t value);
/// `osc::PulseWidth::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/osc/pulse_width.h`): one cycle of
/// the pulse.
std::optional<ui::widgets::oled::DrawColumn> pulse_width_draw(const MenuContext& ctx, int32_t value);
/// `delay::Amount` and `Amount_Unpatched::renderInHorizontalMenu`
/// (`origin/main:src/deluge/gui/menu_item/delay/amount.h`,
/// `origin/main:src/deluge/gui/menu_item/delay/amount_unpatched.h`): the bar to 24, "!" past.
std::optional<ui::widgets::oled::DrawColumn> delay_amount_draw(const MenuContext& ctx, int32_t value);
/// @}

/// @name The sample and recorder columns (`FileSelector::renderInHorizontalMenu` and `getColumnLabel` in
///       `origin/main:src/deluge/gui/menu_item/file_selector.cpp`, `LoopPoint::renderInHorizontalMenu` and
///       `getColumnLabel` in `origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp`,
///       `SampleMarkerEditor::renderInHorizontalMenu` and `getColumnLabel` in
///       `origin/main:src/deluge/gui/menu_item/audio_clip/sample_marker_editor.cpp`,
///       `AudioRecorder::renderInHorizontalMenu` in `origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`)
/// @{
/// `FileSelector`'s big folder icon, a row above the region.
std::optional<deluge::ui::widgets::oled::ArtColumn> file_selector_art(const MenuContext& ctx, int32_t value);
/// `FileSelector::getColumnLabel` (`origin/main:src/deluge/gui/menu_item/file_selector.cpp`) for source 1 and 2 (or the
/// audio clip): the loaded sample's folder, "Mult", or the name.
ValueText file0_label(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
ValueText file1_label(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// The end markers' label: the end point's name cut to three characters.
ValueText end_point_label(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// The recorder's two-slot column for source 1 and 2.
std::optional<ui::widgets::oled::DrawColumn> recorder0_draw(const MenuContext& ctx, int32_t value);
std::optional<ui::widgets::oled::DrawColumn> recorder1_draw(const MenuContext& ctx, int32_t value);
/// The loop start and end markers and the audio clip's end marker.
std::optional<ui::widgets::oled::DrawColumn> loop_start_draw(const MenuContext& ctx, int32_t value);
std::optional<ui::widgets::oled::DrawColumn> loop_end_draw(const MenuContext& ctx, int32_t value);
std::optional<ui::widgets::oled::DrawColumn> clip_end_draw(const MenuContext& ctx, int32_t value);
/// @}

/// `FileSelector::getLastFolderFromPath` (`origin/main:src/deluge/gui/menu_item/file_selector.cpp`): the folder a file
/// sits in, empty for a file at the card's root.
std::string_view last_folder(std::string_view path);

/// @name Page titles (`Node::page_title`)
/// @{ `submenu::ActualSource::getTitle` (`origin/main:src/deluge/gui/menu_item/submenu/actual_source.h`): OSC* SAMPLE
/// past the first page of a sample oscillator.
ValueText source0_page_title(const MenuContext& ctx, uint8_t page, std::span<char> out);
ValueText source1_page_title(const MenuContext& ctx, uint8_t page, std::span<char> out);
/// `submenu::ModFxHorizontalMenu::getTitle` (`origin/main:src/deluge/gui/menu_item/submenu/mod_fx.h`): the effect's
/// name past the first page.
ValueText mod_fx_page_title(const MenuContext& ctx, uint8_t page, std::span<char> out);
/// @}

} // namespace deluge::ui::scenes::menu::nodes
