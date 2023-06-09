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

#ifndef NAMEDTHINGVECTOR_H_
#define NAMEDTHINGVECTOR_H_

#include "ResizeableArray.h"

#include "DString.h"

class NamedThingVectorElement {
public:
	NamedThingVectorElement(void* newNamedThing, String* newName);
	~NamedThingVectorElement() {} // Empty, but must call this so that the String's destructor is also called
	void* namedThing;
	String name; // Store this here so we don't have to go follow the Sample's pointer first to find this out
};

// Note: these are currently non-destructible. If you do destruct, well currently it doesn't destruct all the Strings in the Elements!
class NamedThingVector : public ResizeableArray {
public:
	NamedThingVector(int newStringOffset);
	int search(char const* searchString, int comparison, bool* foundExact = NULL);
	void* getElement(int index);
	void removeElement(int i);
	int insertElement(void* namedThing);
	int insertElement(void* namedThing, int i);
	void renameMember(int i, String* newName);

	const int stringOffset;

private:
	NamedThingVectorElement* getMemory(int index);
	String* getName(void* namedThing);
};

#endif /* NAMEDTHINGVECTOR_H_ */
