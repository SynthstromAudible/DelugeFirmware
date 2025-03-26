/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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
#include <etl/intrusive_links.h>

// Please see explanation of memory allocation and "stealing" at the top of GeneralMemoryAllocator.h

struct Stealable : etl::bidirectional_link<0> {
	using link_type = etl::bidirectional_link<0>;

	Stealable() = default;
	virtual ~Stealable() = default;

	virtual bool mayBeStolen(void* thingNotToStealFrom) = 0;
	virtual void steal(char const* errorCode) = 0; // You gotta also call the destructor after this.
	[[nodiscard]] virtual StealableQueue getAppropriateQueue() const = 0;

	uint32_t lastTraversalNo = 0xFFFFFFFF;

	/// Object equality is based on pointer equality
	[[nodiscard]]
	bool operator==(const Stealable& other) const {
		return this == &other;
	}
};
