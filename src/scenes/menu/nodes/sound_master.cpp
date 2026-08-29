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
/// @brief The master page's `get`/`set` hooks and its exported root span. The `Node` literals are
///        in `sound_master.h`.

#include "scenes/menu/nodes/sound_master.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "definitions_cxx.hpp"
#include "processing/sound/sound.h"
#include "processing/source.h"

namespace deluge::scenes::menu::nodes {

int32_t get_synth_mode(const MenuContext& ctx) {
	return ctx.sound != nullptr ? static_cast<int32_t>(ctx.sound->synthMode) : 0; // `synth_mode.h:31`
}

void set_synth_mode(const MenuContext& ctx, int32_t value) {
	// `SynthModeSelection::writeCurrentValue` (`synth_mode.h:33-58`). The affect-entire kit loop
	// filters *inside* itself -- only drums whose two sources are ring-moddable are switched
	// (`:46-49`), the same test `relevance::ringmod_allowed` applies to the single-target case --
	// so the filter lives in the hook, where legacy put it (`bindings.h`'s `SoundTarget::whole_kit`).
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext& ctx) {
		                           if (t.sound == nullptr) {
			                           return;
		                           }
		                           if (t.whole_kit
		                               && !(t.sound->sources[0].oscType <= kLastRingmoddableOscType
		                                    && t.sound->sources[1].oscType <= kLastRingmoddableOscType)) {
			                           return;
		                           }
		                           t.sound->setSynthMode(static_cast<::SynthMode>(v), ctx.song);
	                           });
	// Legacy also calls `view.setKnobIndicatorLevels()` (`:57`); `gui/views/` is out of reach and
	// the redraw it forces is what `bindings::write`'s `Dirty::Oled` already asks for.
}

int32_t get_master_transpose(const MenuContext& ctx) {
	return ctx.sound != nullptr ? ctx.sound->transpose : 0; // `master_transpose.h:35`
}

void set_master_transpose(const MenuContext& ctx, int32_t value) {
	// `MasterTranspose::writeCurrentValue` (`master_transpose.h:36-66`): store, then recalculate
	// every voice's phase increment against the sound's own model stack -- which is why
	// `apply_to_targets` hands one over (`bindings.h`'s `SoundTarget::stack`). Legacy skips the
	// recalculation for a drum with no stack by building one unconditionally; here a null stack
	// means the context could not resolve one at all, and skipping is strictly safer than the
	// null dereference legacy would have taken (`model/model_stack.cpp:116`).
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           if (t.sound == nullptr) {
			                           return;
		                           }
		                           t.sound->transpose = static_cast<int16_t>(v);
		                           if (t.stack != nullptr) {
			                           t.sound->recalculateAllVoicePhaseIncrements(t.stack);
		                           }
	                           });
}

namespace {
constexpr const Node* kRoots[] = {&sound_master};
} // namespace

const std::span<const Node* const> kSoundMasterChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
