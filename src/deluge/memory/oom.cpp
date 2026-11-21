#include "foundation/oom.h"

#include "foundation/panic.h"

[[noreturn]] void deluge::oom() {
	FREEZE_WITH_ERROR("OOM");
	__builtin_trap(); // freezeWithError is not [[noreturn]]
}
