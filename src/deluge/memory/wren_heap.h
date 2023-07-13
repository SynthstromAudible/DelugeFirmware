#pragma once
#include "RZA1/cpu_specific.h"
#include <cstddef>

constexpr size_t kWrenHeapSize = 4 * 1024 * 1024;

constexpr size_t kWrenHeapStartAddr = EXTERNAL_MEMORY_END - kWrenHeapSize;
constexpr size_t kWrenHeapEndAddr = EXTERNAL_MEMORY_END;

void wren_heap_init();
extern "C" void* wren_heap_realloc(void* ptr, size_t newSize, void* userData);
