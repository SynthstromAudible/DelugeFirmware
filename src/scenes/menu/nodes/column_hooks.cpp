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

#include "scenes/menu/nodes/column_hooks.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h" // node_text, for the formatted names the level columns cut
#include "scenes/menu/nodes/nodes.h"

#include "definitions_cxx.hpp"
#include "engines/deluge/words.h"
#include "engines/effects/words.h"
#include "gfx/oled/icons.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "model/mod_controllable/mod_fx_names.h"
#include "processing/engines/audio_engine.h" // lineInPluggedIn: an input oscillator's icon

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <utility>

namespace deluge::scenes::menu::nodes {

using deluge::gfx::oled::Icon;
using deluge::l10n::String;
using deluge::ui::oled::ArtColumn;
namespace icons = deluge::gfx::oled::icons;

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
	using instrument::OscType;
	const auto type = static_cast<OscType>(choice_of(node, ctx, value));
	if (type == OscType::DX7) {
		return ArtColumn{
		    .text = deluge::l10n::getView(String::STRING_FOR_DX7), .face = ArtColumn::Face::Title, .dy = 5};
	}
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
		icon = AudioEngine::lineInPluggedIn ? &icons::input : &icons::mic;
		break;
	case OscType::WAVETABLE:
		icon = &icons::wavetable;
		break;
	default:
		break;
	}
	return ArtColumn{.icon = icon, .dy = 2};
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

ValueText mod_fx_knob_text(const MenuContext& ctx, ModFXParam knob, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Label) {
		return kValueTextFallback;
	}
	const int32_t shown = bindings::read(mod_fx_type, ctx);
	const auto type = static_cast<deluge::effects::ModFXType>(choice_of(mod_fx_type, ctx, shown));
	return put(modfx::getParamName(type, knob, true), out);
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
	using instrument::LFOType;
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

std::optional<ArtColumn> repeat_art(const MenuContext&, int32_t value) {
	using instrument::SampleRepeatMode;
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

ValueText off_at_zero_text(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use == ValueTextUse::Label || value != 0) {
		return use == ValueTextUse::Notification ? put_int(value, out) : kValueTextFallback;
	}
	return put_string(String::STRING_FOR_OFF, out);
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

ValueText on_off_text(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Notification) {
		return kValueTextFallback;
	}
	return put_string(value != 0 ? String::STRING_FOR_ON : String::STRING_FOR_OFF, out);
}

ValueText reverb_width_text(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Label) {
		return kValueTextFallback;
	}
	return put_string(plate_reverb(ctx) ? String::STRING_FOR_DIFFUSION : String::STRING_FOR_WIDTH_SHORT, out);
}

ValueText reverb_room_size_text(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Label || !plate_reverb(ctx)) {
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
	return mod_fx_knob_text(ctx, ModFXParam::DEPTH, use, out);
}

ValueText mod_fx_feedback_text(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {
	return mod_fx_knob_text(ctx, ModFXParam::FEEDBACK, use, out);
}

ValueText mod_fx_offset_text(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {
	return mod_fx_knob_text(ctx, ModFXParam::OFFSET, use, out);
}

} // namespace deluge::scenes::menu::nodes
