/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

class OpenAddressingHashTable {
public:
	OpenAddressingHashTable();
	virtual ~OpenAddressingHashTable();
	virtual uint32_t getKeyFromAddress(void* address) = 0;
	virtual void setKeyAtAddress(uint32_t key, void* address) = 0;
	virtual bool doesKeyIndicateEmptyBucket(uint32_t key) = 0;

	int32_t getBucketIndex(uint32_t key);
	void* getBucketAddress(int32_t b);
	void* secondaryMemoryGetBucketAddress(int32_t b);
	void* insert(uint32_t key, bool* onlyIfNotAlreadyPresent = nullptr);
	void* lookup(uint32_t key);
	bool remove(uint32_t key);
	void empty(bool destructing = false);

	void* memory;
	int32_t numBuckets;
	int32_t numElements;

	void* secondaryMemory;
	int32_t secondaryMemoryNumBuckets;
	uint32_t secondaryMemoryFunctionCurrentIteration;
	uint8_t secondaryMemoryCurrentFunction;

	int8_t elementSize;
	int8_t initialNumBuckets;
};

class OpenAddressingHashTableWith32bitKey final : public OpenAddressingHashTable {
public:
	OpenAddressingHashTableWith32bitKey();
	uint32_t getKeyFromAddress(void* address);
	void setKeyAtAddress(uint32_t key, void* address);
	bool doesKeyIndicateEmptyBucket(uint32_t key);
};

class OpenAddressingHashTableWith16bitKey final : public OpenAddressingHashTable {
public:
	OpenAddressingHashTableWith16bitKey();
	uint32_t getKeyFromAddress(void* address);
	void setKeyAtAddress(uint32_t key, void* address);
	bool doesKeyIndicateEmptyBucket(uint32_t key);
};

class OpenAddressingHashTableWith8bitKey final : public OpenAddressingHashTable {
public:
	OpenAddressingHashTableWith8bitKey();
	uint32_t getKeyFromAddress(void* address);
	void setKeyAtAddress(uint32_t key, void* address);
	bool doesKeyIndicateEmptyBucket(uint32_t key);
};
