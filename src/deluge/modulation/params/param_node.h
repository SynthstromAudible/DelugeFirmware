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
#include <cstdint>

#include "gui/positionable.h"

class ParamNode : public Positionable {
public:
	ParamNode();
	ParamNode(ParamNode const & other) = default;
	ParamNode(ParamNode && other) = default;

	ParamNode & operator=(ParamNode const & rhs) = default;
	ParamNode & operator=(ParamNode && rhs) = default;

	int32_t value;
	bool interpolated; // From the previous node
};

struct StolenParamNodes {
	int32_t num;
	ParamNode* nodes;
};
