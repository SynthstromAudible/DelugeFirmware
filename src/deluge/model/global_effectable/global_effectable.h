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
#include "deluge-project/fx_chain.h"
#include "effects/filter.h"
#include "model/fx/chain_view.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "modulation/arpeggiator.h"
#include "serialization/value.h"
using namespace deluge;
class Serializer;

class GlobalEffectable : public ModControllableAudio {
public:
	void cloneFrom(ModControllableAudio* other) override;

	/// @brief Sets a host chain's params to their defaults (model::initGlobalParams).
	/// @param paramManager The param manager to initialise.
	static void initParams(ParamManager* paramManager);
	/// @brief Sets an audio clip's params to their defaults (model::initAudioClipParams).
	/// @param paramManager The param manager to initialise.
	static void initParamsForAudioClip(ParamManagerForTimeline* paramManager);

	/// @brief Which values the chain's knobs edit: the view the chain's owner holds.
	/// @return The chain's view.
	[[nodiscard]] virtual deluge::model::ChainView& chainView() = 0;
	[[nodiscard]] virtual const deluge::model::ChainView& chainView() const = 0;
	[[nodiscard]] const ::GlobalEffectable* asGlobalEffectable() const override { return this; }
};
