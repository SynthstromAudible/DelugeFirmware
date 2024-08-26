/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "util/d_string.h"
#include "definitions_cxx.hpp"
#include "memory/general_memory_allocator.h"
#include "util/cfunctions.h"
#include <bit>
#include <cstring>
#include <io/debug/log.h>

const char nothing = 0;

String::String(const String& other) {
	set(&other);
}
String& String::operator=(const String& other) {
	if (this != &other) {
		set(&other);
	}
	return *this;
}

String::~String() {
	clear(true);
}

int32_t String::getNumReasons() const {
	return *std::bit_cast<intptr_t*>(stringMemory - 4);
}

void String::setNumReasons(int32_t newNum) const {
	*std::bit_cast<intptr_t*>(stringMemory - 4) = newNum;
}

void String::clear(bool destructing) {
	if (stringMemory) {
		int32_t numReasons = getNumReasons();
		if (numReasons > 1) {
			setNumReasons(numReasons - 1);
		}
		else {
			delugeDealloc(stringMemory - 4);
		}

		stringMemory = nullptr;
	}
}

Error String::set(char const* newChars, int32_t newLength) {

	if (newLength == -1) {
		newLength = strlen(newChars);
	}

	if (!newLength) {
		clear();
		return Error::NONE;
	}

	// If we're here, new length is not 0

	// If already got some memory...
	if (stringMemory) {

		{
			// If it's shared with another object, can't use it
			int32_t numReasons = getNumReasons();
			if (numReasons > 1) {
				goto clearAndAllocateNew;
			}

			// If we're here, the memory is exclusively ours (1 reason)

			int32_t allocatedSize = GeneralMemoryAllocator::get().getAllocatedSize(stringMemory - 4);

			int32_t extraMemoryNeeded = newLength + 1 + 4 - allocatedSize;

			// Either it's enough for new string...
			if (extraMemoryNeeded <= 0) {
				goto doCopy;
			}

			else {
				// Try extending
				uint32_t amountExtendedLeft, amountExtendedRight;
				GeneralMemoryAllocator::get().extend(stringMemory - 4, extraMemoryNeeded, extraMemoryNeeded,
				                                     &amountExtendedLeft, &amountExtendedRight);

				stringMemory -= amountExtendedLeft;

				// If that gave us enough...
				if (amountExtendedLeft + amountExtendedRight >= extraMemoryNeeded) {
					goto doCopy;
				}
			}
		}

		// Or it's not and we have to clear
clearAndAllocateNew:
		clear();
	}

	{
		void* newMemory = GeneralMemoryAllocator::get().allocExternal(newLength + 1 + 4);
		if (!newMemory) {
			return Error::INSUFFICIENT_RAM;
		}
		stringMemory = (char*)newMemory + 4;
	}

doCopy:
	memcpy(stringMemory, newChars, newLength);
	stringMemory[newLength] = 0;
	setNumReasons(1);
	return Error::NONE;
}

// This one can't fail!
void String::set(String const* otherString) {
	char* sm = otherString->stringMemory;
	if (sm && sm == stringMemory) {
		D_PRINTLN("setting string to itself");
		return;
	}
#if ALPHA_OR_BETA_VERSION
	// if the other string has memory and it's not in the non audio region
	if (sm != nullptr) {
		if (!(EXTERNAL_MEMORY_END - RESERVED_EXTERNAL_ALLOCATOR < (uint32_t)sm && (uint32_t)sm < EXTERNAL_MEMORY_END)) {
			FREEZE_WITH_ERROR("S001");
			return;
		}
		// or if it doesn't have an allocation
		if (!GeneralMemoryAllocator::get().getAllocatedSize(sm)) {
			FREEZE_WITH_ERROR("S002");
			return;
		}
	}
#endif
	otherString->beenCloned();

	clear();
	stringMemory = sm;
}

void String::beenCloned() const {
	if (stringMemory != nullptr) {
		setNumReasons(getNumReasons() + 1);
	}
}

size_t String::getLength() {
	if (stringMemory == nullptr) {
		return 0;
	}
	return strlen(stringMemory);
}

Error String::shorten(int32_t newLength) {
	if (!newLength) {
		clear();
	}
	else {

		int32_t oldNumReasons = getNumReasons();

		// If reasons, we have to do a clone
		if (oldNumReasons > 1) {
			void* newMemory = GeneralMemoryAllocator::get().allocExternal(newLength + 1 + 4);
			if (!newMemory) {
				return Error::INSUFFICIENT_RAM;
			}

			setNumReasons(oldNumReasons - 1); // Reduce reasons on old memory

			char* newStringMemory = (char*)newMemory + 4;
			memcpy(newStringMemory, stringMemory, newLength); // The ending 0 will get set below
			stringMemory = newStringMemory;

			setNumReasons(1); // Set num reasons on new memory
		}

		stringMemory[newLength] = 0;
	}

	return Error::NONE;
}

Error String::concatenate(String* otherString) {
	if (!stringMemory) {
		set(otherString);
		if (getNumReasons() < 2 && otherString->stringMemory != nullptr) {
			FREEZE_WITH_ERROR("S003");
		}
		return Error::NONE;
	}

	return concatenate(otherString->get());
}

Error String::concatenate(char const* newChars) {
	return concatenateAtPos(newChars, getLength());
}

Error String::concatenateAtPos(char const* newChars, int32_t pos, int32_t newCharsLength) {
	if (pos == 0) {
		return set(newChars, newCharsLength);
	}
	if (pos > getLength()) {
		// this hopefully never happens? If it does it's super broken
		FREEZE_WITH_ERROR("S004");
		return Error::POS_PAST_STRING;
	}
	if (newCharsLength == -1) {
		newCharsLength = strlen(newChars);
	}

	if (newCharsLength == 0) {
		return shorten(pos);
	}

	int32_t oldNumReasons = getNumReasons();

	int32_t requiredSize = pos + newCharsLength + 4 + 1;
	int32_t extraBytesNeeded;
	bool deallocateAfter = true;

	// If additional reasons, we definitely have to allocate afresh
	if (oldNumReasons > 1) {
		deallocateAfter = false;
		setNumReasons(oldNumReasons - 1);
		goto allocateNewMemory;
	}

	extraBytesNeeded = requiredSize - GeneralMemoryAllocator::get().getAllocatedSize(stringMemory - 4);

	// If not enough memory allocated...
	if (extraBytesNeeded > 0) {

		// See if we can extend
		uint32_t amountExtendedLeft, amountExtendedRight;
		GeneralMemoryAllocator::get().extend(stringMemory - 4, extraBytesNeeded, extraBytesNeeded, &amountExtendedLeft,
		                                     &amountExtendedRight);

		// If that worked...
		if (amountExtendedLeft || amountExtendedRight) {

			// If extended left, gotta move stuff
			if (amountExtendedLeft) {
				memmove(stringMemory - amountExtendedLeft - 4, stringMemory - 4,
				        pos + 4); // Moves the stored num reasons, too
				stringMemory -= amountExtendedLeft;
			}
		}

		// Otherwise, gotta allocate brand new memory
		else {
allocateNewMemory:
			void* newMemory = GeneralMemoryAllocator::get().allocExternal(requiredSize);
			if (!newMemory) {
				return Error::INSUFFICIENT_RAM;
			}

			char* newStringMemory = (char*)newMemory + 4;

			// Copy the bit we want to keep of the old memory
			memcpy(newStringMemory, stringMemory, pos);

			if (deallocateAfter) {
				delugeDealloc(stringMemory - 4);
			}
			stringMemory = newStringMemory;
			setNumReasons(1);
		}
	}
	memcpy(&stringMemory[pos], newChars, newCharsLength);
	stringMemory[pos + newCharsLength] = 0;

	return Error::NONE;
}

Error String::concatenateInt(int32_t number, int32_t minNumDigits) {
	char buffer[12];
	intToString(number, buffer, minNumDigits);
	return concatenate(buffer);
}

Error String::setInt(int32_t number, int32_t minNumDigits) {
	char buffer[12];
	intToString(number, buffer, minNumDigits);
	return set(buffer);
}

Error String::setChar(char newChar, int32_t pos) {

	// If any additional reasons, we gotta clone the memory first
	int32_t oldNumReasons = getNumReasons();
	if (oldNumReasons > 1) {

		int32_t length = getLength();

		int32_t requiredSize = length + 4 + 1;
		void* newMemory = GeneralMemoryAllocator::get().allocExternal(requiredSize);
		if (!newMemory) {
			return Error::INSUFFICIENT_RAM;
		}

		char* newStringMemory = (char*)newMemory + 4;

		// Copy the old memory
		memcpy(newStringMemory, stringMemory, length + 1); // Copies 0 at end, too
		setNumReasons(oldNumReasons - 1);                  // Reduce reasons on old memory
		stringMemory = newStringMemory;
		setNumReasons(1); // Set reasons on new memory
	}

	stringMemory[pos] = newChar;
	return Error::NONE;
}

bool String::equals(char const* otherChars) {
	return !strcmp(get(), otherChars);
}

bool String::equalsCaseIrrespective(char const* otherChars) {
	return !strcasecmp(get(), otherChars);
}
