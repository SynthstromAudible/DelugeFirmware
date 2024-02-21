/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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
#include "util/container/array/resizeable_array.h"

#include "util/d_string.h"

class NamedThingVectorElement {
public:
	NamedThingVectorElement(void* newNamedThing, String* newName);
	~NamedThingVectorElement() {} // Empty, but must call this so that the String's destructor is also called
	void* namedThing;
	String name; // Store this here so we don't have to go follow the Sample's pointer first to find this out
};

// Note: these are currently non-destructible. If you do destruct, well currently it doesn't destruct all the Strings in
// the Elements!
class NamedThingVector : public ResizeableArray {
public:
	NamedThingVector(int32_t newStringOffset);
	int32_t search(char const* searchString, int32_t comparison, bool* foundExact = NULL);
	void* getElement(int32_t index);
	void removeElement(int32_t i);
	Error insertElement(void* namedThing);
	Error insertElement(void* namedThing, int32_t i);
	void renameMember(int32_t i, String* newName);

	const int32_t stringOffset;

private:
	NamedThingVectorElement* getMemory(int32_t index);
	String* getName(void* namedThing);
};
