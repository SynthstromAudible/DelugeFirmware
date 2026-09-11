#pragma once
#include <cstdint>

void delugeDealloc(void* address);

class GeneralMemoryAllocator {
public:
	static GeneralMemoryAllocator& get();
	void* allocMaxSpeed(uint32_t size);
};