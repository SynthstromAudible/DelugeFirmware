// tests/unit/sync/storage_op_spec.cpp
#include "sync/storage_op.h"

#include "cppspec.hpp"

// The permit state is module-internal (storage_op.cpp); exercised exclusively through
// the public accessor and StorageOp's RAII behaviour. Test isolation invariant: each
// example must leave no live StorageOp (all are scoped locals), so the flag returns to
// false between examples — there is no before_each reset backstop.

// clang-format off
describe storage_op("deluge::sync::StorageOp", $ {
	it("permits user actions while alive", _ {
		deluge::sync::StorageOp op;
		expect(deluge::sync::user_actions_permitted()).to_be_truthy();
	});

	it("restores the prior state on destruction", _ {
		{ deluge::sync::StorageOp op; }
		expect(deluge::sync::user_actions_permitted()).to_be_falsy();
	});

	it("nests without clobbering (restores to the outer scope's value)", _ {
		deluge::sync::StorageOp outer;
		{ deluge::sync::StorageOp inner; }
		// Inner's destructor must restore to true (outer still active), not false.
		expect(deluge::sync::user_actions_permitted()).to_be_truthy();
	});

	it("user_actions_permitted() is false with no StorageOp active", _ {
		expect(deluge::sync::user_actions_permitted()).to_be_falsy();
	});

	it("user_actions_permitted() is true while a StorageOp is alive", _ {
		deluge::sync::StorageOp op;
		expect(deluge::sync::user_actions_permitted()).to_be_truthy();
	});

	it("user_actions_permitted() returns to false after the scope closes", _ {
		{ deluge::sync::StorageOp op; }
		expect(deluge::sync::user_actions_permitted()).to_be_falsy();
	});
});

CPPSPEC_SPEC(storage_op)
