// The panic contract (foundation/panic.h) for spec binaries linking a standalone library: a freeze is recorded,
// not terminal, so a spec can check which code an impossible state froze with.
#include "mock_panic.h"

#include "foundation/panic.h"

namespace {
std::string last;
} // namespace

namespace deluge::test {
const std::string& last_freeze() {
	return last;
}
void clear_freeze() {
	last.clear();
}
} // namespace deluge::test

extern "C" void freezeWithError(char const* errmsg) {
	last = errmsg;
}

extern "C" void fault_handler_print_freeze_pointers(uint32_t, uint32_t) {
}
