// The spec trees' deluge::oom() (src/foundation/oom.h): a std::bad_alloc throw, so a spec can observe the infallible
// path. Every spec driver links it (tests/support/cmake/specs.cmake); it declares the function itself rather than
// including foundation/oom.h, because not every driver has src/ on its include path.

#include <new>

namespace deluge {
[[noreturn]] void oom();
} // namespace deluge

[[noreturn]] void deluge::oom() {
	throw std::bad_alloc{};
}
