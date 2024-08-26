#include "definitions.h"
#include <exception>

[[noreturn]] void Terminate() noexcept {
	FREEZE_WITH_ERROR("TERM");
	__builtin_unreachable();
}

namespace __cxxabiv1 {
std::terminate_handler __terminate_handler = Terminate;
}
