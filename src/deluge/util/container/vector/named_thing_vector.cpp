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

#include "util/container/vector/named_thing_vector.h"
#include <new>
#include <string.h>

NamedThingVectorElement::NamedThingVectorElement(void* newNamedThing, String* newName) {
	namedThing = newNamedThing;
	name.set(newName);
}

NamedThingVector::NamedThingVector(int32_t newStringOffset)
    : ResizeableArray(sizeof(NamedThingVectorElement)), stringOffset(newStringOffset) {
}

int32_t NamedThingVector::search(char const* searchString, int32_t comparison, bool* foundExact) {

	int32_t rangeBegin = 0;
	int32_t rangeEnd = numElements;
	int32_t proposedIndex;

	while (rangeBegin != rangeEnd) {
		int32_t rangeSize = rangeEnd - rangeBegin;
		proposedIndex = rangeBegin + (rangeSize >> 1);

		NamedThingVectorElement* element = getMemory(proposedIndex);
		int32_t result = strcasecmp(element->name.get(), searchString);

		if (!result) {
			if (foundExact) {
				*foundExact = true;
			}
			return proposedIndex + comparison;
		}
		else if (result < 0) {
			rangeBegin = proposedIndex + 1;
		}
		else {
			rangeEnd = proposedIndex;
		}
	}

	if (foundExact) {
		*foundExact = false;
	}
	return rangeBegin + comparison;
}

NamedThingVectorElement* NamedThingVector::getMemory(int32_t index) {
	return (NamedThingVectorElement*)getElementAddress(index);
}

void* NamedThingVector::getElement(int32_t index) {
	return getMemory(index)->namedThing;
}

String* NamedThingVector::getName(void* namedThing) {
	return (String*)((uint32_t)namedThing + stringOffset);
}

// Returns error code
Error NamedThingVector::insertElement(void* namedThing) {

	String* name = getName(namedThing);

	int32_t i = search(name->get(), GREATER_OR_EQUAL);

	return insertElement(namedThing, i);
}

Error NamedThingVector::insertElement(void* namedThing, int32_t i) {
	// While inserting, the stealing of any AudioFiles would cause a simultaneous
	// delete. They all know not to allow theft when passed this AudioFileVector.
	D_TRY(insertAtIndex(i, 1, this));

	String* name = getName(namedThing);
	new (getMemory(i)) NamedThingVectorElement(namedThing, name);

	return Error::NONE;
}

void NamedThingVector::removeElement(int32_t i) {
	getMemory(i)->~NamedThingVectorElement(); // Have to call this so String gets destructed!
	deleteAtIndex(i);
}

// Check the new name is in fact different before calling this, if you want.
void NamedThingVector::renameMember(int32_t i, String* newName) {

	int32_t newI = search(newName->get(), GREATER_OR_EQUAL);

	NamedThingVectorElement* memory = getMemory(i);
	memory->name.set(newName);                 // Can't fail
	getName(memory->namedThing)->set(newName); // Can't fail

	// Probably need to move element now we've changed its name.
	if (newI > i + 1) {
		newI--;
		goto doRepositionElement;
	}
	else if (newI < i) {
doRepositionElement:
		repositionElement(i, newI);
	}
}
