// tests/spec_io/startup_policy_spec.cpp
#include "io/file.hpp"
#include "storage/existence_policy.h"

#include "cppspec.hpp"

#include <expected>

using deluge::storage::startup_action;
using deluge::storage::StartupPresence;

namespace {
std::expected<bool, deluge::io::Status> there() {
	return true;
}
std::expected<bool, deluge::io::Status> notThere() {
	return false;
}
std::expected<bool, deluge::io::Status> cannotTell(deluge::io::Status s) {
	return std::unexpected(s);
}
} // namespace

// clang-format off
describe startup_policy("the startup fail-safe never reads 'could not tell' as 'not there'", $ {

	it("reports a path that is there as Confirmed", _ {
		expect(startup_action(there()) == StartupPresence::Confirmed).to_equal(true);
	});

	it("reports a path known absent as Missing", _ {
		expect(startup_action(notThere()) == StartupPresence::Missing).to_equal(true);
	});

	// The whole reason the verb exists. BUSY is what an off-owner refusal looks like, which is the
	// condition this rung removes -- but the policy must hold even so, because a card can fail for
	// its own reasons at any time.
	it("reports a refused check as Unusable, NEVER Missing", _ {
		expect(startup_action(cannotTell(deluge::io::Status::BUSY)) == StartupPresence::Unusable).to_equal(true);
		expect(startup_action(cannotTell(deluge::io::Status::BUSY)) == StartupPresence::Missing).to_equal(false);
	});

	// Every failure status, not just the one we expect. A verb that special-cased BUSY would let a
	// card reporting IO errors write a template over songs the user still has.
	it("reports every failure status as Unusable", _ {
		for (auto s : {deluge::io::Status::ERR, deluge::io::Status::IO, deluge::io::Status::BUSY,
		               deluge::io::Status::TIMEOUT, deluge::io::Status::NODEV,
		               deluge::io::Status::NO_FILESYSTEM, deluge::io::Status::PARAM}) {
			expect(startup_action(cannotTell(s)) == StartupPresence::Unusable).to_equal(true);
		}
	});
});
// clang-format on

CPPSPEC_SPEC(startup_policy)
