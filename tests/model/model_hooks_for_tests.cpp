#include "model_hooks_for_tests.h"

#include "deluge-project/edit_guard.h"
#include "deluge-project/model_memory.h"

#include <cstddef>
#include <new>

namespace deluge::project {

namespace test {
HookCounts counts{};
} // namespace test

[[noreturn]] void model_out_of_memory() {
	throw std::bad_alloc{};
}

} // namespace deluge::project

using deluge::project::test::counts;

extern "C" void* deluge_model_alloc(std::size_t bytes, std::size_t alignment) noexcept {
	if (counts.fail_next_allocation) {
		counts.fail_next_allocation = false;
		return nullptr;
	}
	counts.allocations++;
	return ::operator new(bytes, std::align_val_t{alignment}, std::nothrow);
}

extern "C" void deluge_model_free(void* p, std::size_t alignment) noexcept {
	counts.frees++;
	::operator delete(p, std::align_val_t{alignment});
}

extern "C" bool deluge_model_edit_enter() noexcept {
	counts.enters++;
	counts.depth++;
	return true;
}

extern "C" void deluge_model_edit_exit(bool entered) noexcept {
	if (entered) {
		counts.exits++;
		counts.depth--;
	}
}

extern "C" void deluge_model_note_structural_edit(uint8_t kind) noexcept {
	counts.edits++;
	if (counts.depth == 0) {
		counts.unguarded_edits++;
	}
	switch (static_cast<deluge::project::ModelEdit>(kind)) {
	case deluge::project::ModelEdit::NODES:
		// The op's own guard is one level; a compound edit around it is a second.
		if (counts.depth < 2) {
			counts.lone_node_edits++;
		}
		break;
	case deluge::project::ModelEdit::PARAM_STORE:
		counts.store_edits++;
		break;
	case deluge::project::ModelEdit::NODE_KERNEL:
		counts.kernels++;
		break;
	}
}
