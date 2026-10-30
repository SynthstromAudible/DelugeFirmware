/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "deluge-project/param_automation.h"
#include "instruments/deluge/mod_matrix.h"

#include <cstddef>
#include <optional>

class ParamManager;
class PatchCableSet;
class Sound;

/// @brief A topology edit's undo record as the app keeps it: the instrument's delta, and the depth lane the edited
/// cable takes with it while it is out of the clip.
struct CableEdit {
	deluge::instrument::CableDelta delta;
	deluge::project::DocAutoParam lane;
};

/// @brief The app's side of `deluge.mod_matrix/1` for one clip's cables (a synth clip's, or a kit row's for one clip):
/// each write is validated by the instrument, applied to the clip's PatchCableSet and compiled, and returns the edit
/// that undoes it.
class ModMatrixHost {
public:
	ModMatrixHost(Sound& sound, ParamManager& paramManager);

	std::optional<CableEdit> addCable(deluge::synth::CableRecord cable);
	std::optional<CableEdit> removeCable(size_t index);
	std::optional<CableEdit> setPolarity(size_t index, deluge::synth::CablePolarity polarity);
	std::optional<CableEdit> reroute(size_t index, deluge::synth::CableTarget target);
	/// @brief Put the clip's cables as @p edit had them at @p time: BEFORE undoes, AFTER redoes.
	void apply(CableEdit& edit, TimeType time);
	/// @brief Create-on-edit: add the cable a depth write to (@p source, @p target) needs, with the source's default
	/// polarity, when it is missing.
	std::optional<CableEdit> ensureCable(PatchSource source, deluge::synth::CableTarget target);
	/// @brief Delete-at-zero: remove cable @p index when its depth is 0 with no automation and @p clipActive.
	std::optional<CableEdit> settleDepth(size_t index, bool clipActive);

private:
	/// @brief Finish @p delta from the cables as the compile left them.
	CableEdit finish(deluge::instrument::CableDelta delta, deluge::project::DocAutoParam lane = {});
	[[nodiscard]] std::optional<size_t> find(const deluge::synth::CableRecord& cable) const;

	Sound& sound_;
	ParamManager& paramManager_;
	PatchCableSet& cables_;
};
