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

#pragma once

#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "storage/flash_storage.h"
#include "synth/voice/param_view.h"

#include <limits>

/// @brief A sound's render reads its clip's params through this: `synth::param_view` over a `ParamManagerForTimeline`.
/// Each read is forced inline so the view costs the render nothing over reading the param manager itself.
class ParamManagerView {
public:
	explicit ParamManagerView(ParamManagerForTimeline& params) : params_{&params} {}

	[[nodiscard, gnu::always_inline]] int32_t unpatched(int32_t param) const {
		return params_->getUnpatchedParamSet()->getValue(param);
	}
	[[nodiscard, gnu::always_inline]] deluge::dsp::Q<31> knob(int32_t param) const {
		return params_->getUnpatchedParamSet()->getKnob(param);
	}
	[[nodiscard, gnu::always_inline]] bool modulated(int32_t param) const {
		return params_->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(param);
	}
	[[nodiscard, gnu::always_inline]] bool wired(PatchSource source) const {
		return params_->getPatchCableSet()->isSourcePatchedToSomething(source);
	}
	[[nodiscard, gnu::always_inline]] uint32_t sources_wired(Globality scope) const {
		return params_->getPatchCableSet()->sourcesPatchedToAnything[scope];
	}
	[[nodiscard, gnu::always_inline]] bool can_sound(int32_t param) const {
		PatchedParamSet& patched = *params_->getPatchedParamSet();
		// the neutral is the param's minimum, as an unsigned word
		return patched.params[param].containsSomething(patched.playedLane(param),
		                                               static_cast<uint32_t>(std::numeric_limits<int32_t>::min()));
	}
	[[nodiscard, gnu::always_inline]] std::array<uint8_t, 2> bend_ranges() const {
		const ExpressionParamSet* expression = params_->getExpressionParamSet();
		const uint8_t* ranges = expression != nullptr ? expression->bendRanges : FlashStorage::defaultBendRange;
		return {ranges[BEND_RANGE_MAIN], ranges[BEND_RANGE_FINGER_LEVEL]};
	}

private:
	ParamManagerForTimeline* params_;
};

static_assert(deluge::synth::param_view<ParamManagerView>);
