#pragma once

/// @file
/// @brief Legacy's horizontal-menu overrides that read the value or the context, as `Binding::column_art` and
///        `Binding::value_text` hooks: what a column draws (`renderInHorizontalMenu`), how it is labelled
///        (`getColumnLabel`) and what its notification says (`getNotificationValue`). The overrides that are a fixed
///        string are `Node::column_name`, and `showColumnLabel()`/`showNotification()` are `Node` flags.

#include "scenes/menu/node.h"
#include "ui/oled/columns.h"

#include <cstdint>
#include <optional>
#include <span>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @name Column drawings (`Binding::column_art`)
/// @{

/// `delay::PingPong::renderInHorizontalMenu` (`delay/ping_pong.h:66-70`): the switch, a row above the region.
std::optional<deluge::ui::oled::ArtColumn> switch_art(const MenuContext& ctx, int32_t value);
/// `randomizer::RandomizerLock::renderInHorizontalMenu` (`randomizer/randomizer_lock.h:67-71`): the big padlock.
std::optional<deluge::ui::oled::ArtColumn> lock_art(const MenuContext& ctx, int32_t value);
/// `sample::PitchSpeed::renderInHorizontalMenu` (`sample/pitch_speed.h:70-73`): the keyboard, crossed out when pitch
/// and speed are independent.
std::optional<deluge::ui::oled::ArtColumn> keyboard_art(const MenuContext& ctx, int32_t value);
/// `sample::Reverse` and `audio_clip::Reverse::renderInHorizontalMenu` (`sample/reverse.h:68-73`,
/// `audio_clip/reverse.h:71-75`): the direction arrow, mirrored when reversed.
std::optional<deluge::ui::oled::ArtColumn> direction_art(const MenuContext& ctx, int32_t value);
/// `lfo::Type::renderInHorizontalMenu` (`lfo/type.h:77-101`): the shape's icon.
std::optional<deluge::ui::oled::ArtColumn> lfo_type_art(const MenuContext& ctx, int32_t value);
/// `osc::Type::renderInHorizontalMenu` (`osc/type.h:112-155`) for source 1 and 2: the shape's icon with the analog
/// or supersaw letter, or DX7's name.
std::optional<deluge::ui::oled::ArtColumn> osc0_type_art(const MenuContext& ctx, int32_t value);
std::optional<deluge::ui::oled::ArtColumn> osc1_type_art(const MenuContext& ctx, int32_t value);
/// `sample::Repeat::renderInHorizontalMenu` (`sample/repeat.h:96-112`): the mode's icon, from the region's left.
std::optional<deluge::ui::oled::ArtColumn> repeat_art(const MenuContext& ctx, int32_t value);
/// `sequence::Direction::renderInHorizontalMenu` (`sequence/direction.h:123-141`): the arrow, mirrored for reverse, a
/// mirrored pair for ping-pong; obey-parent is the option's text.
std::optional<deluge::ui::oled::ArtColumn> sequence_direction_art(const MenuContext& ctx, int32_t value);
/// `arpeggiator::Rhythm` and `midi_cv::Rhythm::renderInHorizontalMenu` (`arpeggiator/rhythm.h:42-62`,
/// `midi_cv/rhythm.h:48-66`): the pattern spaced out, '0' as X, or the name of pattern 0.
std::optional<deluge::ui::oled::ArtColumn> arp_rhythm_art(const MenuContext& ctx, int32_t value);
std::optional<deluge::ui::oled::ArtColumn> arp_rhythm_midi_art(const MenuContext& ctx, int32_t value);
/// `unison::Count::renderInHorizontalMenu` (`unison/count.h:62-67`): the count in the "Big" face.
std::optional<deluge::ui::oled::ArtColumn> unison_count_art(const MenuContext& ctx, int32_t value);
/// `midi::Preset::renderInHorizontalMenu` (`midi/preset.h:62-79`): NONE at 128, else the number.
std::optional<deluge::ui::oled::ArtColumn> midi_preset_art(const MenuContext& ctx, int32_t value);
/// @}

/// @name Column texts (`Binding::value_text`)
/// @{

/// `voice::VoiceCount` and `arpeggiator::SequenceLength`'s OFF at zero (`voice/polyphony.h:89-96`,
/// `arpeggiator/sequence_length.h:27-44`): the column's word and the notification's.
ValueText off_at_zero_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `osc::RetriggerPhase` (`osc/retrigger_phase.h:106-123`): OFF while free-running, and a notification of the raw
/// number.
ValueText retrigger_phase_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `reverb::sidechain::Volume` (`reverb/sidechain/volume.h:50-73`): AUTO below zero.
ValueText ducking_auto_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `sample::Reverse` and `audio_clip::Reverse::getNotificationValue` (`sample/reverse.h:77-79`): ON or OFF.
ValueText on_off_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `reverb::Width::getColumnLabel` (`reverb/width.h:50-62`): DIFFUSION for the plate models, else the short width.
ValueText reverb_width_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `reverb::RoomSize::getName` (`reverb/room_size.h:40-49`), which its column label reads: TIME for the plate models.
ValueText reverb_room_size_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `sample::Repeat::getColumnLabel` (`sample/repeat.h:114-116`): the mode's name.
ValueText repeat_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `sample::PitchSpeed::getColumnLabel` (`sample/pitch_speed.h:75-78`): LINKED or INDEPENDENT.
ValueText pitch_speed_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `osc::source::Volume::getColumnLabel` (`osc/source/volume.h:39-42`): the first four letters of the formatted
/// name, for source 1 and 2.
ValueText source0_volume_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
ValueText source1_volume_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `source::patched_param::ModulatorLevel::getColumnLabel` (`source/patched_param/modulator_level.h:38`): the
/// formatted name from its third letter, for modulator 1 and 2.
ValueText modulator0_volume_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
ValueText modulator1_volume_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `mod_fx::Depth`, `Feedback` and `Offset::getColumnLabel` (`mod_fx/depth_patched.h:40-42`, `feedback.h:38-41`,
/// `offset.h:40-42`): the mod FX type's short name for the knob.
ValueText mod_fx_depth_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
ValueText mod_fx_feedback_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
ValueText mod_fx_offset_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// @}

/// @name The filter pages (`gui/menu_item/filter/`)
/// @{
/// `filter::FilterContainer` (`filter/filter_container.h:35-146`): the frequency column draws the response across
/// itself and the resonance column.
std::optional<ui::oled::DrawColumn> lpf_graph_draw(const MenuContext& ctx, int32_t value);
std::optional<ui::oled::DrawColumn> hpf_graph_draw(const MenuContext& ctx, int32_t value);
std::optional<ui::oled::DrawColumn> global_lpf_graph_draw(const MenuContext& ctx, int32_t value);
std::optional<ui::oled::DrawColumn> global_hpf_graph_draw(const MenuContext& ctx, int32_t value);
/// `filter::FilterParam::getColumnLabel` (`filter/param.h:47-49`): the morph's short name for the filter's family --
/// DRIVE (LP ladder), FM (HP ladder), MORPH (SVF).
ValueText lpf_morph_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
ValueText hpf_morph_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `filter::FilterParam::renderInHorizontalMenu` (`filter/param.h:51-63`): a slider on a morphable (SVF) filter, HPF
/// drawn as `50 - value`; the ordinary bar otherwise.
std::optional<ui::oled::DrawColumn> lpf_morph_draw(const MenuContext& ctx, int32_t value);
std::optional<ui::oled::DrawColumn> hpf_morph_draw(const MenuContext& ctx, int32_t value);
/// `filter::FilterParam::selectEncoderAction` (`filter/param.h:65-73`): HPF SVF morph turns the other way on a page.
bool hpf_morph_reversed(const MenuContext& ctx);
/// @}

/// @name Pictures (`Binding::column_draw`), each over its 0..50 value
/// @{
/// `voice::Portamento::renderInHorizontalMenu` (`voice/portamento.h:34-66`): the glide between two notes.
std::optional<ui::oled::DrawColumn> glide_draw(const MenuContext& ctx, int32_t value);
/// `unison::Detune::renderInHorizontalMenu` (`unison/detune.h:57-98`): three splaying lines.
std::optional<ui::oled::DrawColumn> detune_draw(const MenuContext& ctx, int32_t value);
/// `osc::PulseWidth::renderInHorizontalMenu` (`osc/pulse_width.h:54-77`): one cycle of the pulse.
std::optional<ui::oled::DrawColumn> pulse_width_draw(const MenuContext& ctx, int32_t value);
/// `delay::Amount` and `Amount_Unpatched::renderInHorizontalMenu` (`delay/amount.h:35-58`): the bar to 24, "!" past.
std::optional<ui::oled::DrawColumn> delay_amount_draw(const MenuContext& ctx, int32_t value);
/// @}

/// @name Page titles (`Node::page_title`)
/// @{
/// `submenu::ActualSource::getTitle` (`submenu/actual_source.h:29-40`): OSC* SAMPLE past the first page of a sample
/// oscillator.
ValueText source0_page_title(const MenuContext& ctx, uint8_t page, std::span<char> out);
ValueText source1_page_title(const MenuContext& ctx, uint8_t page, std::span<char> out);
/// `submenu::ModFxHorizontalMenu::getTitle` (`submenu/mod_fx.h:29-43`): the effect's name past the first page.
ValueText mod_fx_page_title(const MenuContext& ctx, uint8_t page, std::span<char> out);
/// @}

} // namespace deluge::scenes::menu::nodes
