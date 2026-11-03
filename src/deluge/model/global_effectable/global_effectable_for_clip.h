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
#include "model/fx/track_channel.h"
#include "model/global_effectable/global_effectable.h"
#include "util/functions.h"

class ParamManagerForTimeline;
class Output;
class Clip;
class ModelStackWithThreeMainThings;

class GlobalEffectableForClip : public GlobalEffectable {
public:
	/// @brief The track's channel strip: its chain, the chain's settings and its view.
	/// @return The channel the kit or audio track holds.
	[[nodiscard]] virtual deluge::model::TrackChannel& channel() = 0;
	[[nodiscard]] virtual const deluge::model::TrackChannel& channel() const = 0;

	[[nodiscard]] deluge::effects::TrackChain& chain() { return channel().chain(); }
	[[nodiscard]] deluge::effects::ChainCore& core() final { return channel().core(); }
	[[nodiscard]] const deluge::effects::ChainCore& core() const final { return channel().core(); }
	[[nodiscard]] deluge::effects::Filter& filter() final { return channel().filter(); }
	[[nodiscard]] deluge::model::ChainView& chainView() final { return channel().view(); }
	[[nodiscard]] const deluge::model::ChainView& chainView() const final { return channel().view(); }

	void modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager,
	                     const deluge::project::CompView& comp = {}) final;
	bool modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack,
	                            deluge::project::CompView* comp = nullptr) final;
	virtual Output* toOutput() = 0;

	/// @brief The two mod knobs a clip's global effectable lays out differently from the song's: the sidechain volume
	/// (mode 4, upper) and the pitch adjust (mode 6, lower); nullopt for the others.
	[[nodiscard]] static std::optional<deluge::plugin::ParamId> clipKnobParam(int32_t modKnobMode,
	                                                                          int32_t whichModEncoder);

protected:
	int32_t getParameterFromKnob(int32_t whichModEncoder, const deluge::project::CompView& comp) final;
};
