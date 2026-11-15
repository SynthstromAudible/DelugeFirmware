/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "deluge-project/controller_mapping.h"
#include "deluge-project/mapping_table.h"
#include "dsp-next/core/stereo_sample.h"
#include "effects/delay.h"
#include "hid/button.h"
#include "model/fx/effects_binding.h"
#include "model/mod_controllable/filters/filter_config.h"
#include "model/mod_controllable/mod_controllable.h"
#include "model/snapshot/mod_controllable_snapshot.h"
#include "modulation/knob_target.h"
#include "modulation/params/param_set.h"
#include "modulation/sync.h"
#include "synth/modulation/cable.h"
#include "synth/modulation/lfo.h"
#include "util/functions.h"
#include <optional>
#include <vector>

#include "effects/chain_core.h"
#include "effects/state.h"
#include "plugin-api/plugin.h"

class ArpeggiatorSettings;
class Clip;
class GlobalEffectable;
class Sound;
class MIDICable;
class ModelStack;
class ModelStackWithTimelineCounter;
class ParamManager;
class Serializer;
class Serializer;
class Deserializer;

class ModControllableAudio : public ModControllable {
public:
	ModControllableAudio();
	virtual ~ModControllableAudio();

	/// @brief This owner's effect settings, as its binding holds them: a bound record's, else the binding's own.
	/// @return The effect settings.
	[[nodiscard]] const deluge::effects::EffectStates& effects() const { return binding_->effects(); }
	/// @brief Make @p next this owner's effect settings, and the render's copy of them, which is replaced with the
	/// render excluded.
	/// @param next The new effect settings.
	void setEffects(const deluge::effects::EffectStates& next);
	/// @brief Keep this owner's effect settings in @p record (a sound's SoundState), which takes the settings held now;
	/// nullptr keeps them here again.
	/// @param record The record that holds the settings, or nullptr to hold them in this owner.
	void bindEffects(deluge::effects::EffectStates* record);
	/// @brief The render's copy of the effect settings: it reads nothing else of them.
	/// @return The render's copy.
	[[nodiscard]] const deluge::snapshot::ModControllableSnapshot& modControllableSnapshot() const {
		return binding_->snapshot();
	}
	/// @brief Copy the settings into the render's copy, with the render excluded.
	void projectModControllableSnapshot();

	/// @brief The effect components every owner has, in its chain: a sound's, a clip chain's or the master's.
	/// @return The chain's core.
	[[nodiscard]] virtual deluge::effects::ChainCore& core() = 0;
	[[nodiscard]] virtual const deluge::effects::ChainCore& core() const = 0;

private:
	/// The binding that holds this owner's settings and the render's copy; set first in each owner's constructor.
	deluge::model::EffectsBinding* binding_{nullptr};

protected:
	/// @brief Makes @p binding the one this owner's settings and render copy live in. Each owner calls it first in its
	/// constructor, before anything reads its effects.
	/// @param binding The owner's binding, which outlives every use through this owner.
	void bindBinding(deluge::model::EffectsBinding& binding) { binding_ = &binding; }
};
