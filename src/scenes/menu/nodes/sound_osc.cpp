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
/// @brief The oscillator subtree's `get`/`set` hooks, the `kPending` placeholder's two members, and
///        the exported root span. The `Node` literals are in `sound_osc.h`.

#include "scenes/menu/nodes/sound_osc.h"

#include "scenes/l10n_bridge.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h" // MenuState, for the placeholder `CustomItem`
#include "scenes/profile.h"

#include "shell/scene.h"
#include "ui/oled/frame.h"
#include "ui/oled/text.h"

#include "definitions_cxx.hpp"
#include "instruments/deluge/sound_state.h"
#include "instruments/deluge/words.h"
#include "scenes/bind/runtime_facts.h"
#include "scenes/bind/write_sink.h"

#include <string>
#include <vector>

namespace deluge::scenes::menu::nodes {

using deluge::l10n::String;

// ---------------------------------------------------------------------------------------------
// The placeholder for the four bespoke items. See `sound_osc.h`'s note.

void PendingCustom::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	// Deliberately the same "honest placeholder" `MenuScene`'s own unimplemented-kind arm draws
	// (`menu_scene.cpp:872-881`): the user sees which item they are on rather than the previous
	// screen. Replaced, per item, when E2 lands the browser, the recorder and the marker editor.
	const Node* node = state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
	                                           : std::string_view{});
}

deluge::ui::shell::Result PendingCustom::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

namespace {

// ---------------------------------------------------------------------------------------------
// `osc::Type` (`gui/menu_item/osc/type.h:34-160`).

// ---------------------------------------------------------------------------------------------
// `sample::Transpose` (`gui/menu_item/sample/transpose.h:31-175`) -- "this is actually the
// Oscillator transpose" (`sample/transpose.h:31`).

/// @brief `computeCurrentValueForTranspose` (`gui/menu_item/value_scaling.cpp:64-66`).
constexpr int32_t transpose_to_menu(int32_t transpose, int32_t cents) {
	return transpose * 100 + cents;
}

/// @brief Does source @p src of a sound in @p state take its transpose in its range? A sample source with ranges does,
///        unless the sound is FM (`sample/transpose.h:38-56`).
bool transposes_in_range(const deluge::instrument::SoundState& state, const deluge::instrument::SourceState& src) {
	return !src.ranges.empty() && state.synth_mode != deluge::instrument::SynthMode::FM
	       && src.osc_type == deluge::instrument::OscType::SAMPLE;
}

int32_t get_osc_transpose(const MenuContext& ctx, uint8_t index) {
	const deluge::instrument::SoundState* state =
	    ctx.facts != nullptr && ctx.addresses.slot ? ctx.facts->sound_state(*ctx.addresses.slot) : nullptr;
	if (state == nullptr) {
		return 0;
	}
	const deluge::instrument::SourceState& src = state->sources[index];
	if (transposes_in_range(*state, src)) {
		// `:44-47`: the selected range when the menu is on this very source, else range 0.
		const size_t r = ctx.source_index == index && ctx.range_index >= 0
		                         && static_cast<size_t>(ctx.range_index) < src.ranges.size()
		                     ? static_cast<size_t>(ctx.range_index)
		                     : 0;
		return transpose_to_menu(src.ranges[r].transpose, src.ranges[r].cents);
	}
	return transpose_to_menu(src.transpose, src.cents);
}

void set_osc_transpose(const MenuContext& ctx, int32_t value, uint8_t index) {
	// `sample::Transpose::writeCurrentValue` (`sample/transpose.h`): a sample source's transpose is its range's -- the
	// selected one, or range 0 in the kit sweep -- and any other source's is its own setting; the sink decides per
	// sound. Legacy's affect-entire test here is `soundEditor.editingKit()`, not the `editingKitRow()` every other
	// item uses; `bindings::writes_whole_kit` is the latter, so on a kit-global-FX context this writes one target
	// where legacy swept the kit. Unreachable in practice (the oscillator menus are not reachable from the
	// kit-global-FX root).
	if (ctx.sink == nullptr) {
		return;
	}
	bind::Addresses where = ctx.addresses;
	where.affect_entire = bindings::writes_whole_kit(ctx);
	const std::optional<uint16_t> range = ctx.source_index == index && ctx.range_index >= 0
	                                          ? std::optional{static_cast<uint16_t>(ctx.range_index)}
	                                          : std::nullopt;
	(void)ctx.sink->write_source_transpose(where, index, range, value);
}

// ---------------------------------------------------------------------------------------------
// `osc::RetriggerPhase` (`gui/menu_item/osc/retrigger_phase.h:25-135`).

// ---------------------------------------------------------------------------------------------
// The FM modulator items.

constexpr const Node* kRoots[] = {&recorder, &source0, &source1, &modulator0, &modulator1, &osc_mixer};

} // namespace

// ---------------------------------------------------------------------------------------------
// The hooks `sound_osc.h`'s macros declared, each forwarding its baked-in source index.

#define DELUGE_OSC_SOURCE_HOOKS(i)                                                                                     \
	int32_t get_source##i##_transpose(const MenuContext& ctx) {                                                        \
		return get_osc_transpose(ctx, (i));                                                                            \
	}                                                                                                                  \
	void set_source##i##_transpose(const MenuContext& ctx, int32_t v) {                                                \
		set_osc_transpose(ctx, v, (i));                                                                                \
	}

DELUGE_OSC_SOURCE_HOOKS(0)
DELUGE_OSC_SOURCE_HOOKS(1)

#undef DELUGE_OSC_SOURCE_HOOKS

const std::span<const Node* const> kSoundOscChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
