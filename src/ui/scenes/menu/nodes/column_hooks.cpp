/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

/// @file
/// @brief The bodies of `column_hooks.h`'s hooks.

#include "ui/scenes/menu/nodes/column_hooks.h"

#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h" // node_text, for the formatted names the level columns cut
#include "ui/scenes/menu/nodes/nodes.h"

#include "effects/words.h"
#include "gui/l10n/effect_names.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "instruments/classic/state_access.h"
#include "instruments/classic/words.h"
#include "plugin/types/arp_rhythm.h"
#include "plugin/types/display_mapping.h"
#include "project/song.h"
#include "ui/gfx/oled/icons.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>

namespace deluge::ui::scenes::menu::nodes {

using deluge::l10n::String;
using deluge::ui::gfx::oled::Icon;
using deluge::ui::widgets::oled::ArtColumn;
namespace icons = deluge::ui::gfx::oled::icons;

namespace {

/// Legacy's icons drawn with `slot.start_y - 1` sit three rows above `kHorizontalSlotYOffset`.
constexpr int8_t kAboveRegion = -3;

ValueText put(std::string_view text, std::span<char> out) {
	const size_t n = std::min(text.size(), out.size());
	if (n > 0) {
		std::memcpy(out.data(), text.data(), n);
	}
	return ValueText{.length = n};
}

ValueText put_string(String id, std::span<char> out) {
	return put(deluge::l10n::getView(id), out);
}

ValueText put_int(int32_t value, std::span<char> out) {
	char buf[12];
	const int written = std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(value));
	return written <= 0 ? ValueText{} : put(std::string_view{buf, static_cast<size_t>(written)}, out);
}

/// The choice @p node's listed entry @p value stands for, or what the record stores when no choice does.
int32_t choice_of(const Node& node, const MenuContext& ctx, int32_t value) {
	if (const std::optional<int32_t> choice = bindings::listed_choice(node, ctx, value)) {
		return *choice;
	}
	return bindings::stored(node.bind.target, ctx).value_or(value);
}

std::optional<ArtColumn> osc_type_art(const Node& node, const MenuContext& ctx, int32_t value) {
	using instruments::classic::OscType;
	const auto type = static_cast<OscType>(choice_of(node, ctx, value));
	const Icon* icon = &icons::sine;
	switch (type) {
	case OscType::TRIANGLE:
		icon = &icons::triangle;
		break;
	case OscType::SQUARE:
	case OscType::ANALOG_SQUARE:
		icon = &icons::square;
		break;
	case OscType::SAW:
	case OscType::ANALOG_SAW_2:
	case OscType::SUPERSAW:
		icon = &icons::saw;
		break;
	case OscType::SAMPLE:
		icon = &icons::sample;
		break;
	case OscType::INPUT_STEREO:
	case OscType::INPUT_L:
	case OscType::INPUT_R:
		icon = ctx.ports.audio != nullptr && ctx.ports.audio->line_in_plugged() ? &icons::input : &icons::mic;
		break;
	case OscType::WAVETABLE:
		icon = &icons::wavetable;
		break;
	default:
		break;
	}
	// The analog and supersaw shapes carry a letter in the region's corner (`Type::renderInHorizontalMenu` in
	// `origin/main:src/deluge/gui/menu_item/osc/type.h`).
	char badge = 0;
	if (type == OscType::ANALOG_SQUARE || type == OscType::ANALOG_SAW_2) {
		badge = 'A';
	}
	else if (type == OscType::SUPERSAW) {
		badge = 'S';
	}
	return ArtColumn{.icon = icon, .dy = 2, .badge = badge};
}

/// `arpeggiator::Rhythm::renderInHorizontalMenu` and its MIDI/CV twin's
/// (`origin/main:src/deluge/gui/menu_item/arpeggiator/rhythm.h`,
/// `origin/main:src/deluge/gui/menu_item/arpeggiator/midi_cv/rhythm.h`), whose centring differs by
/// @p nudge.
std::optional<ArtColumn> rhythm_art(int32_t value, int8_t nudge) {
	if (value < 0 || static_cast<size_t>(value) >= plugin::kArpRhythms.size()) {
		return std::nullopt;
	}
	const std::string_view pattern = plugin::kArpRhythms[static_cast<size_t>(value)].name;
	if (value == 0) {
		return ArtColumn{.text = pattern};
	}
	return ArtColumn{.text = pattern, .dx = nudge, .char_gap = 2, .zero_as_x = true};
}

/// Is the song's reverb one of the two plate models, whose room size and width legacy names TIME and DIFFUSION?
bool plate_reverb(const MenuContext& ctx) {
	using deluge::effects::ReverbModel;
	const auto model = static_cast<ReverbModel>(choice_of(reverb_model, ctx, bindings::read(reverb_model, ctx)));
	return model == ReverbModel::DIGITAL || model == ReverbModel::MUTABLE;
}

/// The formatted name of @p node, as `node_text` fills its `*`.
std::string_view formatted_name(const Node& node, const MenuContext& ctx, NodeText& scratch) {
	return node_text(node.name, node, ctx, scratch);
}

ValueText mod_fx_knob_text(const MenuContext& ctx, deluge::effects::ModFxParam knob, ValueTextUse use,
                           std::span<char> out) {
	// The column label is the short name; the name and title the long one (`Depth_Patched::getColumnLabel` and
	// `getName` in `origin/main:src/deluge/gui/menu_item/mod_fx/depth_patched.h`).
	if (use != ValueTextUse::Label && use != ValueTextUse::Name && use != ValueTextUse::Title) {
		return kValueTextFallback;
	}
	const int32_t shown = bindings::read(mod_fx_type, ctx);
	const auto type = static_cast<deluge::effects::ModFXType>(choice_of(mod_fx_type, ctx, shown));
	return put(deluge::l10n::mod_fx_param_name(type, knob, use == ValueTextUse::Label), out);
}

} // namespace

std::optional<ArtColumn> switch_art(const MenuContext&, int32_t value) {
	return ArtColumn{.icon = value != 0 ? &icons::switcherOn : &icons::switcherOff, .dy = kAboveRegion};
}

std::optional<ArtColumn> lock_art(const MenuContext&, int32_t value) {
	return ArtColumn{.icon = value != 0 ? &icons::lockedBig : &icons::unlockedBig, .dy = kAboveRegion};
}

std::optional<ArtColumn> keyboard_art(const MenuContext&, int32_t value) {
	return ArtColumn{.icon = value != 0 ? &icons::crossedOutKeyboard : &icons::keyboard, .dy = kAboveRegion};
}

std::optional<ArtColumn> direction_art(const MenuContext&, int32_t value) {
	return ArtColumn{.icon = &icons::direction, .reversed = value != 0};
}

std::optional<ArtColumn> lfo_type_art(const MenuContext&, int32_t value) {
	using instruments::classic::LFOType;
	const Icon* icon = &icons::sine;
	switch (static_cast<LFOType>(value)) {
	case LFOType::TRIANGLE:
		icon = &icons::triangle;
		break;
	case LFOType::SQUARE:
		icon = &icons::square;
		break;
	case LFOType::SAW:
		icon = &icons::saw;
		break;
	case LFOType::SAMPLE_AND_HOLD:
		icon = &icons::sampleHold;
		break;
	case LFOType::RANDOM_WALK:
		icon = &icons::randomWalk;
		break;
	case LFOType::WARBLER:
		icon = &icons::warbler;
		break;
	default:
		break;
	}
	return ArtColumn{.icon = icon, .dy = 2};
}

std::optional<ArtColumn> osc0_type_art(const MenuContext& ctx, int32_t value) {
	return osc_type_art(osc0_type, ctx, value);
}

std::optional<ArtColumn> osc1_type_art(const MenuContext& ctx, int32_t value) {
	return osc_type_art(osc1_type, ctx, value);
}

std::optional<ArtColumn> sequence_direction_art(const MenuContext&, int32_t value) {
	switch (value) {
	case 0: // forward
		return ArtColumn{.icon = &icons::direction};
	case 1: // reverse
		return ArtColumn{.icon = &icons::direction, .reversed = true};
	case 2: // ping-pong
		return ArtColumn{.icon = &icons::direction, .twin_dx = 2};
	default: // obey parent: `Selection`'s own text
		return std::nullopt;
	}
}

std::optional<ArtColumn> arp_rhythm_art(const MenuContext&, int32_t value) {
	return rhythm_art(value, 2);
}

std::optional<ArtColumn> arp_rhythm_midi_art(const MenuContext&, int32_t value) {
	return rhythm_art(value, 1);
}

std::optional<ArtColumn> repeat_art(const MenuContext&, int32_t value) {
	using instruments::classic::SampleRepeatMode;
	const Icon* icon = &icons::sampleModeCut;
	switch (static_cast<SampleRepeatMode>(value)) {
	case SampleRepeatMode::ONCE:
		icon = &icons::sampleModeOnce;
		break;
	case SampleRepeatMode::LOOP:
		icon = &icons::sampleModeLoop;
		break;
	case SampleRepeatMode::STRETCH:
		icon = &icons::sampleModeStretch;
		break;
	default:
		break;
	}
	return ArtColumn{.icon = icon, .centred = false, .dx = 4, .dy = -4};
}

std::optional<ArtColumn> unison_count_art(const MenuContext&, int32_t value) {
	static constexpr std::array<std::string_view, 10> kDigits{"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
	if (value < 0 || value >= static_cast<int32_t>(kDigits.size())) {
		return std::nullopt;
	}
	return ArtColumn{.text = kDigits[static_cast<size_t>(value)], .face = ArtColumn::Face::Big, .dx = 1, .dy = 3};
}

std::optional<ArtColumn> midi_preset_art(const MenuContext&, int32_t value) {
	if (value != 128) {
		return std::nullopt;
	}
	return ArtColumn{.text = deluge::l10n::getView(String::STRING_FOR_NONE)};
}

std::optional<int32_t> midi_preset_turn(const MenuContext&, int32_t value, int32_t offset) {
	const int32_t span = kMidiPresetRange.max - kMidiPresetRange.min + 1;
	return kMidiPresetRange.min + (((value + offset - kMidiPresetRange.min) % span) + span) % span;
}

ValueText midi_preset_text(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Column || value != kMidiPresetRange.max) {
		return kValueTextFallback;
	}
	return put_string(String::STRING_FOR_NONE, out);
}

ValueText off_at_zero_text(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use == ValueTextUse::Label || value != 0) {
		return use == ValueTextUse::Notification ? put_int(value, out) : kValueTextFallback;
	}
	return put_string(String::STRING_FOR_OFF, out);
}

ValueText cv_volts_word(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Column || value != 0) {
		return kValueTextFallback;
	}
	return put("Hz/V", out);
}

ValueText retrigger_phase_text(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use == ValueTextUse::Label) {
		return kValueTextFallback;
	}
	if (value < 0) {
		return put_string(String::STRING_FOR_OFF, out);
	}
	return use == ValueTextUse::Notification ? put_int(value, out) : kValueTextFallback;
}

ValueText ducking_auto_text(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use == ValueTextUse::Label) {
		return kValueTextFallback;
	}
	if (value < 0) {
		return put_string(String::STRING_FOR_AUTO, out);
	}
	return use == ValueTextUse::Notification ? put_int(value, out) : kValueTextFallback;
}

ValueText hold_time_text(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Column) {
		return kValueTextFallback;
	}
	if (value == 20) {
		return put("1 SEC", out);
	}
	char buf[12];
	const int written = std::snprintf(buf, sizeof(buf), "%d MS", static_cast<int>(value * 50));
	return written <= 0 ? ValueText{} : put(std::string_view{buf, static_cast<size_t>(written)}, out);
}

ValueText default_magnitude_text(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Column || value < 0 || value > 6) {
		return kValueTextFallback;
	}
	return put_int(96 << value, out);
}

ValueText arp_rhythm_name_text(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Column || value < 0 || value > plugin::kMaxMenuSteps) {
		return kValueTextFallback;
	}
	return put(plugin::kArpRhythms[static_cast<size_t>(value)].name, out);
}

ValueText arp_rhythm_screen_text(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Column || value < 0 || value > plugin::kMaxMenuSteps) {
		return kValueTextFallback;
	}
	char name[12];
	const std::string_view pattern = plugin::kArpRhythms[static_cast<size_t>(value)].name;
	const int written = std::snprintf(name, sizeof(name), "%d: %.*s", static_cast<int>(value),
	                                  static_cast<int>(pattern.size()), pattern.data());
	// "%d: " alone is three bytes, so `written` is positive.
	return put(std::string_view{name, std::min(static_cast<size_t>(written), sizeof(name) - 1)}, out);
}

ValueText on_off_text(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Notification) {
		return kValueTextFallback;
	}
	return put_string(value != 0 ? String::STRING_FOR_ON : String::STRING_FOR_OFF, out);
}

ValueText reverb_width_text(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {
	if (use == ValueTextUse::Name || use == ValueTextUse::Title) {
		// `reverb::Width::getName`/`getTitle` (`origin/main:src/deluge/gui/menu_item/reverb/width.h`).
		return plate_reverb(ctx) ? put_string(String::STRING_FOR_DIFFUSION, out) : kValueTextFallback;
	}
	if (use != ValueTextUse::Label) {
		return kValueTextFallback;
	}
	return put_string(plate_reverb(ctx) ? String::STRING_FOR_DIFFUSION : String::STRING_FOR_WIDTH_SHORT, out);
}

ValueText reverb_room_size_text(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {
	// The column label, name and title alike (`RoomSize::getName` in
	// `origin/main:src/deluge/gui/menu_item/reverb/room_size.h`).
	if ((use != ValueTextUse::Label && use != ValueTextUse::Name && use != ValueTextUse::Title) || !plate_reverb(ctx)) {
		return kValueTextFallback;
	}
	return put_string(String::STRING_FOR_TIME, out);
}

ValueText repeat_text(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	static constexpr std::array kModes{String::STRING_FOR_CUT, String::STRING_FOR_ONCE, String::STRING_FOR_LOOP,
	                                   String::STRING_FOR_STRETCH};
	if (use != ValueTextUse::Label || value < 0 || value >= static_cast<int32_t>(kModes.size())) {
		return kValueTextFallback;
	}
	return put_string(kModes[static_cast<size_t>(value)], out);
}

ValueText pitch_speed_text(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Label) {
		return kValueTextFallback;
	}
	return put_string(value != 0 ? String::STRING_FOR_INDEPENDENT : String::STRING_FOR_LINKED, out);
}

ValueText source0_volume_text(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Label) {
		return kValueTextFallback;
	}
	NodeText scratch;
	return put(formatted_name(source0_volume, ctx, scratch).substr(0, 4), out);
}

ValueText source1_volume_text(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Label) {
		return kValueTextFallback;
	}
	NodeText scratch;
	return put(formatted_name(source1_volume, ctx, scratch).substr(0, 4), out);
}

ValueText modulator0_volume_text(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Label) {
		return kValueTextFallback;
	}
	NodeText scratch;
	const std::string_view name = formatted_name(modulator0_volume, ctx, scratch);
	return put(name.substr(std::min<size_t>(2, name.size())), out);
}

ValueText modulator1_volume_text(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Label) {
		return kValueTextFallback;
	}
	NodeText scratch;
	const std::string_view name = formatted_name(modulator1_volume, ctx, scratch);
	return put(name.substr(std::min<size_t>(2, name.size())), out);
}

ValueText mod_fx_depth_text(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {
	return mod_fx_knob_text(ctx, deluge::effects::ModFxParam::DEPTH, use, out);
}

ValueText mod_fx_feedback_text(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {
	return mod_fx_knob_text(ctx, deluge::effects::ModFxParam::FEEDBACK, use, out);
}

ValueText mod_fx_offset_text(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {
	return mod_fx_knob_text(ctx, deluge::effects::ModFxParam::OFFSET, use, out);
}

namespace {

/// @brief The filter mode the LPF or HPF mode node reads; OFF for a low-pass index past the last low-pass mode.
deluge::effects::FilterMode filter_mode_of(const MenuContext& ctx, bool hpf) {
	using deluge::effects::FilterMode;
	const int32_t index = bindings::read(hpf ? hpf_mode : lpf_mode, ctx);
	if (hpf) {
		return static_cast<FilterMode>(index + deluge::effects::kFirstHPFMode);
	}
	return index < deluge::effects::kNumLPFModes ? static_cast<FilterMode>(index) : FilterMode::OFF;
}

/// @brief The family of the LPF or HPF whose mode node @p ctx reads (`FilterInfo::getMode`,
/// `origin/main:src/deluge/gui/menu_item/filter/info.h`).
deluge::effects::FilterFamily filter_family_of(const MenuContext& ctx, bool hpf) {
	return deluge::effects::filter_family(filter_mode_of(ctx, hpf));
}

std::optional<ui::widgets::oled::DrawColumn> graph(const MenuContext& ctx, int32_t freq, const Node& reso,
                                                   const Node& morph, bool hpf) {
	return ui::widgets::oled::DrawColumn{
	    .shape = ui::widgets::oled::DrawColumn::Shape::FilterGraph,
	    .filter = {.freq_raw = freq,
	               .reso_raw = bindings::read(reso, ctx),
	               .morph_raw = bindings::read(morph, ctx),
	               .morphable = filter_family_of(ctx, hpf) == deluge::effects::FilterFamily::SVF,
	               .hpf = hpf},
	    .span_next = 1};
}

ValueText morph_text(const MenuContext& ctx, ValueTextUse use, std::span<char> out, bool hpf) {
	// `FilterParam::getName`/`getTitle` are the long morph name, `getColumnLabel` the short
	// (`origin/main:src/deluge/gui/menu_item/filter/param.h`).
	if (use != ValueTextUse::Label && use != ValueTextUse::Name && use != ValueTextUse::Title) {
		return kValueTextFallback;
	}
	return put(deluge::l10n::filter_morph_name(filter_mode_of(ctx, hpf), use == ValueTextUse::Label), out);
}

std::optional<ui::widgets::oled::DrawColumn> morph_draw(const MenuContext& ctx, int32_t value, bool hpf) {
	if (filter_family_of(ctx, hpf) != deluge::effects::FilterFamily::SVF) {
		return std::nullopt;
	}
	const int32_t shown = hpf ? 50 - value : value;
	return ui::widgets::oled::DrawColumn{.shape = ui::widgets::oled::DrawColumn::Shape::Slider,
	                                     .norm = static_cast<float>(shown) / 50.f};
}

} // namespace

std::string_view last_folder(std::string_view path) {
	const size_t last_slash = path.rfind('/');
	if (last_slash == std::string_view::npos || last_slash == 0) {
		return {};
	}
	const size_t prev_slash = path.rfind('/', last_slash - 1);
	const size_t start = prev_slash == std::string_view::npos ? 0 : prev_slash + 1;
	return path.substr(start, last_slash - start);
}

namespace {

/// `FileSelector::getColumnLabel` (`origin/main:src/deluge/gui/menu_item/file_selector.cpp`) for source @p source: the
/// loaded sample's folder, "Mult" over several ranges, the node's name with nothing loaded.
ValueText file_label(const MenuContext& ctx, int32_t source, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Label) {
		return kValueTextFallback;
	}
	if (ctx.is_audio_clip) {
		const project::DocAudioClip* clip = ctx.model != nullptr && ctx.addresses.clip
		                                        ? ctx.model->audio_clip_structure.find(ctx.addresses.clip->clip_id)
		                                        : nullptr;
		if (clip == nullptr || clip->file_path.empty()) {
			return kValueTextFallback;
		}
		return put(last_folder(clip->file_path), out);
	}
	const instruments::classic::SoundState* sound =
	    ctx.ports.sound != nullptr && ctx.addresses.slot ? ctx.ports.sound->sound_state(*ctx.addresses.slot) : nullptr;
	if (sound == nullptr) {
		return ValueText{};
	}
	const std::vector<synth::SourceRange> ranges =
	    instruments::classic::source_ranges(*sound, static_cast<instruments::classic::SourceId>(source));
	if (std::ranges::none_of(ranges, [](const synth::SourceRange& r) { return !r.file_path.empty(); })) {
		return kValueTextFallback;
	}
	if (ranges.size() > 1) {
		return put("Mult", out);
	}
	return put(last_folder(ranges.front().file_path), out);
}

} // namespace

ValueText file0_label(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {
	return file_label(ctx, 0, use, out);
}

ValueText file1_label(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {
	return file_label(ctx, 1, use, out);
}

ValueText end_point_label(const MenuContext&, int32_t, ValueTextUse use, std::span<char> out) {
	// `LoopPoint::getColumnLabel`'s end arm and `SampleMarkerEditor::getColumnLabel`
	// (`origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp`,
	// `origin/main:src/deluge/gui/menu_item/audio_clip/sample_marker_editor.cpp`): the name cut to three characters.
	if (use != ValueTextUse::Label) {
		return kValueTextFallback;
	}
	return put(deluge::l10n::getView(String::STRING_FOR_END_POINT).substr(0, 3), out);
}

std::optional<ArtColumn> file_selector_art(const MenuContext&, int32_t) {
	return ArtColumn{.icon = &icons::folderBig, .dy = kAboveRegion};
}

std::optional<ui::widgets::oled::DrawColumn> recorder0_draw(const MenuContext&, int32_t) {
	return ui::widgets::oled::DrawColumn{.shape = ui::widgets::oled::DrawColumn::Shape::Recorder, .raw = 0};
}

std::optional<ui::widgets::oled::DrawColumn> recorder1_draw(const MenuContext&, int32_t) {
	return ui::widgets::oled::DrawColumn{.shape = ui::widgets::oled::DrawColumn::Shape::Recorder, .raw = 1};
}

std::optional<ui::widgets::oled::DrawColumn> loop_start_draw(const MenuContext&, int32_t) {
	return ui::widgets::oled::DrawColumn{.shape = ui::widgets::oled::DrawColumn::Shape::LoopStart};
}

std::optional<ui::widgets::oled::DrawColumn> loop_end_draw(const MenuContext&, int32_t) {
	return ui::widgets::oled::DrawColumn{.shape = ui::widgets::oled::DrawColumn::Shape::LoopEnd};
}

std::optional<ui::widgets::oled::DrawColumn> clip_end_draw(const MenuContext&, int32_t) {
	return ui::widgets::oled::DrawColumn{.shape = ui::widgets::oled::DrawColumn::Shape::ClipEnd};
}

std::optional<ui::widgets::oled::DrawColumn> glide_draw(const MenuContext&, int32_t value) {
	return ui::widgets::oled::DrawColumn{.shape = ui::widgets::oled::DrawColumn::Shape::Glide,
	                                     .norm = static_cast<float>(value) / 50.f};
}

std::optional<ui::widgets::oled::DrawColumn> detune_draw(const MenuContext&, int32_t value) {
	return ui::widgets::oled::DrawColumn{.shape = ui::widgets::oled::DrawColumn::Shape::Detune,
	                                     .norm = static_cast<float>(value) / 50.f};
}

std::optional<ui::widgets::oled::DrawColumn> pulse_width_draw(const MenuContext&, int32_t value) {
	return ui::widgets::oled::DrawColumn{.shape = ui::widgets::oled::DrawColumn::Shape::PulseWidth, .raw = value};
}

std::optional<ui::widgets::oled::DrawColumn> delay_amount_draw(const MenuContext&, int32_t value) {
	return ui::widgets::oled::DrawColumn{.shape = ui::widgets::oled::DrawColumn::Shape::DelayAmount, .raw = value};
}

std::optional<ui::widgets::oled::DrawColumn> lpf_graph_draw(const MenuContext& ctx, int32_t value) {
	return graph(ctx, value, lpf_res, lpf_morph, false);
}
std::optional<ui::widgets::oled::DrawColumn> hpf_graph_draw(const MenuContext& ctx, int32_t value) {
	return graph(ctx, value, hpf_res, hpf_morph, true);
}
std::optional<ui::widgets::oled::DrawColumn> global_lpf_graph_draw(const MenuContext& ctx, int32_t value) {
	return graph(ctx, value, global_lpf_res, global_lpf_morph, false);
}
std::optional<ui::widgets::oled::DrawColumn> global_hpf_graph_draw(const MenuContext& ctx, int32_t value) {
	return graph(ctx, value, global_hpf_res, global_hpf_morph, true);
}
ValueText lpf_morph_text(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {
	return morph_text(ctx, use, out, false);
}
ValueText hpf_morph_text(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {
	return morph_text(ctx, use, out, true);
}
std::optional<ui::widgets::oled::DrawColumn> lpf_morph_draw(const MenuContext& ctx, int32_t value) {
	return morph_draw(ctx, value, false);
}
std::optional<ui::widgets::oled::DrawColumn> hpf_morph_draw(const MenuContext& ctx, int32_t value) {
	return morph_draw(ctx, value, true);
}
bool hpf_morph_reversed(const MenuContext& ctx) {
	return filter_family_of(ctx, true) == deluge::effects::FilterFamily::SVF;
}

namespace {

ValueText source_page_title(const MenuContext& ctx, uint8_t page, std::span<char> out, const Node& type_node,
                            char number) {
	if (page == 0) {
		return kValueTextFallback;
	}
	const auto type = static_cast<OscType>(choice_of(type_node, ctx, bindings::read(type_node, ctx)));
	if (type != OscType::SAMPLE) {
		return kValueTextFallback;
	}
	const std::string_view title = deluge::l10n::getView(String::STRING_FOR_OSC_SAMPLE_MENU_TITLE);
	const size_t n = std::min(title.size(), out.size());
	for (size_t k = 0; k < n; k++) {
		out[k] = title[k] == '*' ? number : title[k];
	}
	return ValueText{.length = n};
}

} // namespace

ValueText source0_page_title(const MenuContext& ctx, uint8_t page, std::span<char> out) {
	return source_page_title(ctx, page, out, osc0_type, '1');
}
ValueText source1_page_title(const MenuContext& ctx, uint8_t page, std::span<char> out) {
	return source_page_title(ctx, page, out, osc1_type, '2');
}
ValueText mod_fx_page_title(const MenuContext& ctx, uint8_t page, std::span<char> out) {
	if (page == 0) {
		return kValueTextFallback;
	}
	const auto type = static_cast<size_t>(choice_of(mod_fx_type, ctx, bindings::read(mod_fx_type, ctx)));
	return type < static_cast<size_t>(deluge::effects::kNumModFXTypes)
	           ? put(deluge::l10n::mod_fx_name(static_cast<deluge::effects::ModFXType>(type)), out)
	           : kValueTextFallback;
}

} // namespace deluge::ui::scenes::menu::nodes
