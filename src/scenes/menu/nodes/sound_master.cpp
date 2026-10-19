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
#include "engines/deluge/words.h"
#include "model/edits/setting_edit.h"
#include "processing/sound/sound.h"
#include "serialization/value.h"

#include "processing/source.h"
#include <string>

namespace deluge::scenes::menu::nodes {

int32_t get_synth_mode(const MenuContext& ctx) {
	return ctx.sound != nullptr ? static_cast<int32_t>(ctx.sound->getSynthMode()) : 0; // `synth_mode.h`
}

void set_synth_mode(const MenuContext& ctx, int32_t value) {
	// `SynthModeSelection::writeCurrentValue` (`synth_mode.h`). The affect-entire kit loop filters
	// *inside* itself -- only drums whose two sources are ring-moddable are switched, the same test
	// `relevance::ringmod_allowed` applies to the single-target case -- so the filter lives in the
	// hook, where legacy put it (`bindings.h`'s `SoundTarget::whole_kit`).
	bindings::apply_to_targets(
	    ctx, value, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		    if (t.sound == nullptr) {
			    return;
		    }
		    if (t.whole_kit
		        && !(t.sound->sources[0].oscType <= kLastRingmoddableOscType
		             && t.sound->sources[1].oscType <= kLastRingmoddableOscType)) {
			    return;
		    }
		    deluge::edits::settingFor(*t.sound, "mode",
		                              deluge::serialization::Value::makeScalar(
		                                  std::string(deluge::instrument::to_string(static_cast<::SynthMode>(v)))));
	    });
	// Legacy also calls `view.setKnobIndicatorLevels()`; `gui/views/` is out of reach and the redraw
	// it forces is what `bindings::write`'s `Dirty::Oled` already asks for.
}

int32_t get_master_transpose(const MenuContext& ctx) {
	// `MasterTranspose::readCurrentValue` (`master_transpose.h`): semitones and cents as one value
	// in cents.
	return ctx.sound != nullptr ? ctx.sound->getTranspose() * 100 + ctx.sound->snapshot().cents : 0;
}

void set_master_transpose(const MenuContext& ctx, int32_t value) {
	// `MasterTranspose::writeCurrentValue` (`master_transpose.h`): split the value into semitones
	// and cents (`computeFinalValuesForTranspose`), write both, then recalculate every voice's
	// phase increment against the sound's own model stack -- which is why `apply_to_targets` hands
	// one over (`bindings.h`'s `SoundTarget::stack`). A null stack means the context could not
	// resolve one, and the recalculation is skipped.
	bindings::apply_to_targets(
	    ctx, value, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		    if (t.sound == nullptr) {
			    return;
		    }
		    const int32_t shifted = v + 25600;
		    const int32_t semitones = (shifted + 50) / 100;
		    const int32_t cents = shifted - semitones * 100;
		    const int32_t transpose = semitones - 256;
		    deluge::edits::settingFor(*t.sound, "transpose", deluge::serialization::Value::makeNumber(transpose));
		    deluge::edits::settingFor(*t.sound, "cents", deluge::serialization::Value::makeNumber(cents));
	    });
}

namespace {
constexpr const Node* kRoots[] = {&sound_master};
} // namespace

const std::span<const Node* const> kSoundMasterChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
