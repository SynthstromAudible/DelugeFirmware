#include "CppUTest/TestHarness.h"
#include "memory/memory_region.h"
#include <cstdlib>
#include <cstring>

#include "util/container/hashtable/open_addressing_hash_table.h"

#define MEM_SIZE 10000000

TEST_GROUP(OpenHashTableTest){};

constexpr const uint32_t NUM_ELEMENTS_TO_ADD = 64;

template <typename HashTable>
void runTest(HashTable& table) {
	uint32_t elementsAdded[NUM_ELEMENTS_TO_ADD];
	int32_t numElementsAdded = 0;

	// Add a bunch of elements
	while (numElementsAdded < NUM_ELEMENTS_TO_ADD) {
		// Don't allow 0 - we'll use that for special test. Or 0xFF, cos that means empty
		elementsAdded[numElementsAdded] = numElementsAdded + 1;

		CHECK(table.insert(elementsAdded[numElementsAdded]));
		numElementsAdded++;
	}

	CHECK_EQUAL(NUM_ELEMENTS_TO_ADD, table.numElements);

	// See if it'll let us remove an element that doesn't exist
	CHECK_FALSE(table.remove(0));

	// Clean up elements
	for (unsigned int& i : elementsAdded) {
		CHECK(table.remove(i));
	}

	CHECK_EQUAL(0, table.numElements);

	// See if it'll let us remove an element that doesn't exist
	CHECK_FALSE(table.remove(0));
}

TEST(OpenHashTableTest, test8bit) {
	OpenAddressingHashTableWith8bitKey table;
	runTest(table);
}

TEST(OpenHashTableTest, test16bit) {
	OpenAddressingHashTableWith16bitKey table;
	runTest(table);
}

TEST(OpenHashTableTest, test32bit) {
	OpenAddressingHashTableWith32bitKey table;
	runTest(table);
}
