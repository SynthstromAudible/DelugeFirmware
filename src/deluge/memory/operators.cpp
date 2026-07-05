#include "memory/heaps.h"
#include "util/exceptions.h"
#include <cstdlib>
#include <cstring>
#include <new>

#if !IN_UNIT_TESTS
#if defined(__APPLE__)
// macOS host only. Unlike ELF (where the app's strong operator new/delete preempt the C++ runtime's
// process-wide, and no system library hands our pointers to a mismatched free), macOS/dyld coalesces
// the *replaceable* global operators so this executable's definitions preempt libc++abi's everywhere
// — INCLUDING inside system C++ frameworks (AudioToolbox, CoreFoundation, …). Those internally pair
// `operator new` with a raw libsystem_malloc free()/malloc(), so routing operator new to
// GeneralMemoryAllocator hands them a host_sdram (GMA) pointer that free() then rejects with
// "pointer being freed was not allocated" — the SIGABRT seen the first time the audio engine opens a
// CoreAudio AudioQueue. Route the operators to the system allocator on macOS so every pointer foreign
// code receives is system-free-able. GMA is still used directly (via its own API) for sample/cluster
// memory; operator delete below still returns a stray GMA pointer to GMA. The macOS host is not the
// golden reference (that's the Linux -m32 build, which keeps the GMA path unchanged), so routing
// object `new` through the system heap here costs some GMA fidelity but nothing that's compared.
void* operator new(std::size_t n) noexcept(false) {
	void* address = std::malloc(n != 0 ? n : 1);
	if (address == nullptr) [[unlikely]] {
		throw deluge::exception::BAD_ALLOC;
	}
#ifdef DELUGE_DETERMINISTIC_ALLOC
	std::memset(address, 0, n);
#endif
	return address;
}

// operator new above is system-malloc'd, so the common case is std::free. But a pointer allocated
// directly through the GMA API can still reach operator delete (e.g. a placement-new'd object over
// GMA memory); owning_heap() is non-null only for those, so route them back to GMA.
static inline void host_operator_delete(void* p) noexcept {
	if (deluge::memory::owning_heap(p) != nullptr) {
		deluge::memory::dealloc(p);
	}
	else {
		std::free(p);
	}
}
void operator delete(void* p) noexcept {
	host_operator_delete(p);
}
void operator delete(void* p, std::size_t) noexcept {
	host_operator_delete(p);
}
#else
void* operator new(std::size_t n) noexcept(false) {
	// allocate on external RAM (the unified SDRAM heap)
	void* address = deluge::memory::alloc_external(n, 16);
	if (address == nullptr) [[unlikely]] {
		// The throwing operator new must never return null: std::string / std::vector and friends assume a non-null
		// buffer, so returning null silently corrupts them. Throw the same lightweight exception the custom STL
		// allocators (sdram_allocator / external_allocator) do, so existing `catch (deluge::exception)` out-of-memory
		// handlers can deal with it. Code that wants graceful failure uses the heap API directly.
		throw deluge::exception::BAD_ALLOC;
	}
#ifdef DELUGE_DETERMINISTIC_ALLOC
	// The GeneralMemoryAllocator::alloc path zeroes at its return sites; mirror that here so std-container /
	// `new` allocations are deterministic too — `new` goes straight to the heap, bypassing alloc(). Sim/golden
	// builds only (see sim/CMakeLists). Firmware leaves it off; the guard inlines away.
	memset(address, 0, n);
#endif
	return address;
}

void operator delete(void* p) {
	deluge::memory::dealloc(p);
}

// C++14 sized deallocation. libstdc++ containers (std::string, std::vector, ...) call the sized form directly. On the
// device the default sized delete defers to the unsized one above, but a sanitizer substitutes its own sized-delete
// that does NOT defer - so without this override, std::string's buffer (allocated via our operator new) is freed
// through the sanitizer and reported as a bad free. Route it to the custom allocator like the unsized form.
void operator delete(void* p, std::size_t) noexcept {
	deluge::memory::dealloc(p);
}
#endif // defined(__APPLE__)
#endif // !IN_UNIT_TESTS
