// The spec trees' deluge::oom(): a std::bad_alloc throw, so a spec can observe the infallible path.

#include "foundation/oom.h"

#include <new>

[[noreturn]] void deluge::oom() {
	throw std::bad_alloc{};
}
