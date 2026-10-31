#pragma once

// The model's host hooks for spec binaries: model memory from the ordinary heap, and a recording edit guard, so a spec
// can check that every structural edit was bracketed and every allocation returned.

namespace deluge::project::test {

struct HookCounts {
	int allocations{0};
	int frees{0};
	int enters{0};
	int exits{0};
	int depth{0};
	int edits{0};
	int unguarded_edits{0};
	int lone_node_edits{0};           ///< node-vector ops made with no compound edit's guard around their own
	int store_edits{0};               ///< ModelEdit::PARAM_STORE edits
	int kernels{0};                   ///< ModelEdit::NODE_KERNEL edits
	bool fail_next_allocation{false}; ///< the next deluge_model_alloc reports exhausted memory
};

extern HookCounts counts;

} // namespace deluge::project::test
