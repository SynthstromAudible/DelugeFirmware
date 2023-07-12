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

NamedThingVector::NamedThingVector(int newStringOffset)
    : ResizeableArray(sizeof(NamedThingVectorElement)), stringOffset(newStringOffset) {
}

int NamedThingVector::search(char const* searchString, int comparison, bool* foundExact) {

	int rangeBegin = 0;
	int rangeEnd = numElements;
	int proposedIndex;

	while (rangeBegin != rangeEnd) {
		int rangeSize = rangeEnd - rangeBegin;
		proposedIndex = rangeBegin + (rangeSize >> 1);

		NamedThingVectorElement* element = getMemory(proposedIndex);
		int result = strcasecmp(element->name.get(), searchString);

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

NamedThingVectorElement* NamedThingVector::getMemory(int index) {
	return (NamedThingVectorElement*)getElementAddress(index);
}

void* NamedThingVector::getElement(int index) {
	return getMemory(index)->namedThing;
}

String* NamedThingVector::getName(void* namedThing) {
	return (String*)((uint32_t)namedThing + stringOffset);
}

// Returns error code
int NamedThingVector::insertElement(void* namedThing) {

	String* name = getName(namedThing);

	int i = search(name->get(), GREATER_OR_EQUAL);

	return insertElement(namedThing, i);
}

int NamedThingVector::insertElement(void* namedThing, int i) {
	int error = insertAtIndex(
	    i, 1,
	    this); // While inserting, the stealing of any AudioFiles would cause a simultaneous delete. They all know not to allow theft when passed this AudioFileVector.
	if (error) {
		return error;
	}

	String* name = getName(namedThing);
	new (getMemory(i)) NamedThingVectorElement(namedThing, name);

	return NO_ERROR;
}

void NamedThingVector::removeElement(int i) {
	getMemory(i)->~NamedThingVectorElement(); // Have to call this so String gets destructed!
	deleteAtIndex(i);
}

// Check the new name is in fact different before calling this, if you want.
void NamedThingVector::renameMember(int i, String* newName) {

	int newI = search(newName->get(), GREATER_OR_EQUAL);

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
