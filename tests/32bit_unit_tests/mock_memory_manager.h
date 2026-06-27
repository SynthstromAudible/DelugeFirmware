// Test-only hooks into the mock memory manager (mock_memory_manager.cpp).
//
// The host tests run as -m32 binaries, where AddressSanitizer's LeakSanitizer is unavailable. To still catch leaks in
// code that goes through the Deluge allocator, the mock keeps a net count of live allocations. A round-trip test snaps
// the counter, exercises some code, and asserts the counter came back to where it started.

#pragma once

// Net allocations currently outstanding through the mock allocator (alloc/allocExternal minus dealloc/deallocExternal).
long mockAllocatorLiveAllocations();

// Force the live-allocation counter back to zero. Useful at the very start of a test group when earlier setup may have
// left global fixtures allocated.
void mockAllocatorResetCounter();
