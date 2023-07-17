#pragma once
#include <cstddef>

constexpr size_t kWrenHeapSize = 4 * 1024 * 1024;

void wren_heap_init();
void wren_heap_deinit();
extern "C" void* wren_heap_realloc(void* ptr, size_t newSize, void* userData);
