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

#include "model/edits/edit.h"
#include "processing/sound/mod_matrix_host.h"

#include <cstddef>

class ModelStackWithAutoParam;
class ModelStackWithThreeMainThings;

namespace deluge::edits {

/// @brief Record @p edit into the open parameter-change Action, so a depth change and the topology change it caused
/// undo as one step.
void recordCableEdit(ModelStackWithThreeMainThings const* modelStack, CableEdit edit);

/// @brief The depth of the cable from @p source to @p target, for a write: the create-on-edit policy adds the cable
/// first when it is missing, recording the add into the open Action.
/// @return The depth's stack, or nullptr when the cable could not be added.
ModelStackWithAutoParam* cableDepthForEdit(ModelStackWithThreeMainThings* modelStack, PatchSource source,
                                           deluge::synth::CableTarget target);

/// @brief One cable topology op on one clip, as an Edit.
class EditCable final : public Edit {
public:
	static EditCable add(ModelStackWithThreeMainThings* modelStack, deluge::synth::CableRecord cable);
	static EditCable remove(ModelStackWithThreeMainThings* modelStack, size_t index);
	static EditCable setPolarity(ModelStackWithThreeMainThings* modelStack, size_t index,
	                             deluge::synth::CablePolarity polarity);
	static EditCable reroute(ModelStackWithThreeMainThings* modelStack, size_t index,
	                         deluge::synth::CableTarget target);

	Error apply(Context& ctx) override;

private:
	EditCable(ModelStackWithThreeMainThings* modelStack, deluge::instrument::CableDelta::Op op,
	          deluge::synth::CableRecord cable, size_t index)
	    : op_(op), cable_(cable), index_(index), modelStack_(modelStack) {}

	deluge::instrument::CableDelta::Op op_;
	deluge::synth::CableRecord cable_;
	size_t index_;
	/// Valid only for the duration of apply(); never stored beyond it.
	ModelStackWithThreeMainThings* modelStack_;
};

} // namespace deluge::edits
