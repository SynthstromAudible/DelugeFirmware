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
/// @brief The four envelope pages' exported root span, their segments' per-mode names and their graph.

#include "ui/scenes/menu/nodes/sound_env.h"

#include "gui/l10n/l10n.h"
#include "instruments/classic/state_access.h"
#include "ui/scenes/bind/ports/sound_facts.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/widgets/oled/column_drawings.h"

#include <algorithm>
#include <array>
#include <utility>

namespace deluge::ui::scenes::menu::nodes {

namespace {
constexpr const Node* kRoots[] = {&env1, &env2, &env3, &env4};
} // namespace

const std::span<const Node* const> kSoundEnvChildren{kRoots};

namespace {

using deluge::l10n::String;
using deluge::synth::EnvelopeMode;

/// @brief The settings of envelope @p e (0..3) of the sound @p ctx edits; a classic ADSR's where it edits none.
deluge::synth::EnvelopeState envelope_state(const MenuContext& ctx, size_t e) {
	const deluge::instruments::classic::SoundState* state =
	    ctx.ports.sound != nullptr && ctx.addresses.slot ? ctx.ports.sound->sound_state(*ctx.addresses.slot) : nullptr;
	return state != nullptr ? deluge::instruments::classic::envelope(*state, e) : deluge::synth::EnvelopeState{};
}

/// @brief Envelope @p e's name or title from @p name or @p title, its `*` the envelope's number.
ValueText segment_text(size_t e, String name, String title, ValueTextUse use, std::span<char> out) {
	const std::string_view text = deluge::l10n::getView(use == ValueTextUse::Title ? title : name);
	const size_t n = std::min(text.size(), out.size());
	std::ranges::copy_n(text.begin(), static_cast<std::ptrdiff_t>(n), out.begin());
	std::ranges::replace(out.first(n), '*', static_cast<char>('1' + e));
	return {.length = n};
}

/// @brief `Segment::strings` (`origin/main:src/deluge/gui/menu_item/envelope/segment.h`) for the decay param.
ValueText decay_text(const MenuContext& ctx, size_t e, ValueTextUse use, std::span<char> out) {
	return envelope_state(ctx, e).mode == EnvelopeMode::AHD
	           ? segment_text(e, String::STRING_FOR_HOLD, String::STRING_FOR_ENV_HOLD_MENU_TITLE, use, out)
	           : kValueTextFallback;
}
/// @brief As @ref decay_text for the sustain param.
ValueText sustain_text(const MenuContext& ctx, size_t e, ValueTextUse use, std::span<char> out) {
	const EnvelopeMode mode = envelope_state(ctx, e).mode;
	return mode == EnvelopeMode::AR || mode == EnvelopeMode::MPE
	           ? segment_text(e, String::STRING_FOR_ENV_LEVEL, String::STRING_FOR_ENV_LEVEL_MENU_TITLE, use, out)
	           : kValueTextFallback;
}
/// @brief As @ref decay_text for the release param: an AHD decays at it.
ValueText release_text(const MenuContext& ctx, size_t e, ValueTextUse use, std::span<char> out) {
	return envelope_state(ctx, e).mode == EnvelopeMode::AHD
	           ? segment_text(e, String::STRING_FOR_DECAY, String::STRING_FOR_ENV_DECAY_MENU_TITLE, use, out)
	           : kValueTextFallback;
}

/// @brief `EnvelopeMenu::renderMenuItems` (`origin/main:src/deluge/gui/menu_item/envelope/envelope_menu.h`) over
/// envelope @p e's four segments.
void draw_envelope(ui::widgets::oled::OledFrame& panel, const MenuContext& ctx, int32_t focused, size_t e,
                   const Node& attack, const Node& decay, const Node& sustain, const Node& release) {
	const deluge::synth::EnvelopeState state = envelope_state(ctx, e);
	const deluge::synth::EnvelopeEighths shape = deluge::synth::eighths_of(state.curve);
	using Mode = ui::widgets::oled::EnvelopeGraph::Mode;
	static constexpr std::array<Mode, 4> kModes{Mode::ADSR, Mode::AHD, Mode::AR, Mode::MPE}; // in EnvelopeMode's order
	ui::widgets::oled::envelope_graph(panel,
	                                  {.attack = bindings::read(attack, ctx),
	                                   .decay = bindings::read(decay, ctx),
	                                   .sustain = bindings::read(sustain, ctx),
	                                   .release = bindings::read(release, ctx),
	                                   .mode = kModes[std::to_underlying(state.mode)],
	                                   .attack_k = static_cast<float>(shape.attack) / 8.f,
	                                   .decay_k = static_cast<float>(shape.decay) / 8.f,
	                                   .release_k = static_cast<float>(shape.release) / 8.f},
	                                  focused);
}

} // namespace

#define DELUGE_ENVELOPE_HOOKS(n)                                                                                       \
	ValueText env##n##_decay_text(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {            \
		return decay_text(ctx, (n) - 1, use, out);                                                                     \
	}                                                                                                                  \
	ValueText env##n##_sustain_text(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {          \
		return sustain_text(ctx, (n) - 1, use, out);                                                                   \
	}                                                                                                                  \
	ValueText env##n##_release_text(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {          \
		return release_text(ctx, (n) - 1, use, out);                                                                   \
	}                                                                                                                  \
	void env##n##_page_draw(ui::widgets::oled::OledFrame& panel, const MenuContext& ctx, int32_t focused) {            \
		draw_envelope(panel, ctx, focused, (n) - 1, env##n##_attack, env##n##_decay, env##n##_sustain,                 \
		              env##n##_release);                                                                               \
	}

DELUGE_ENVELOPE_HOOKS(1)
DELUGE_ENVELOPE_HOOKS(2)
DELUGE_ENVELOPE_HOOKS(3)
DELUGE_ENVELOPE_HOOKS(4)

#undef DELUGE_ENVELOPE_HOOKS

} // namespace deluge::ui::scenes::menu::nodes
