#include "util/published.h"

#include "cppspec.hpp"
#include <vector>

using deluge::util::Published;
using deluge::util::SnapshotDomain;

namespace {

/// @brief Test double: records every freed pointer (order preserved) instead of
///        just calling `delete`, so specs can assert on exactly what got reclaimed
///        and when -- then still frees for real so the spec doesn't leak.
struct RecordingDeleter {
	std::vector<const int*>* freed;

	void operator()(const int* p) const {
		freed->push_back(p);
		delete p;
	}
};

using RecordingPublished = Published<int, RecordingDeleter>;

} // namespace

// clang-format off
describe published("Published<T>", $ {
	it("has no published pointer until the first publish()", _{
		SnapshotDomain domain;
		Published<int> pub(domain);
		expect(pub.load()).to_equal(static_cast<const int*>(nullptr));
	});

	it("load() returns the latest published snapshot", _{
		SnapshotDomain domain;
		Published<int> pub(domain);

		auto* first = new int(1);
		pub.publish(first);
		expect(pub.load()).to_equal(first);

		auto* second = new int(2);
		pub.publish(second);
		expect(pub.load()).to_equal(second);
	});

	it("retires (but does not free) the superseded snapshot on the next publish", _{
		std::vector<const int*> freed;
		SnapshotDomain domain;
		RecordingPublished pub(domain, nullptr, RecordingDeleter{&freed});

		auto* first = new int(1);
		pub.publish(first);
		expect(pub.retired_count()).to_equal(std::size_t{0});

		auto* second = new int(2);
		pub.publish(second);

		expect(pub.load()).to_equal(second);
		expect(pub.retired_count()).to_equal(std::size_t{1});
		expect(freed).not_().to_contain(first);
	});

	context("the two-grace-period reclaim rule", _{
		it("does not free a retired snapshot until the audio epoch has advanced two boundaries", _{
			std::vector<const int*> freed;
			SnapshotDomain domain;
			RecordingPublished pub(domain, nullptr, RecordingDeleter{&freed});

			auto* first = new int(1);
			pub.publish(first); // cur_ = first, nothing retired yet

			domain.advance_epoch(); // audio_epoch: 0 -> 1 ("block 1", still may hold first)

			auto* second = new int(2);
			pub.publish(second); // retires `first`, stamped at audio_epoch() == 1

			pub.reclaim();
			expect(freed).not_().to_contain(first);
			expect(pub.retired_count()).to_equal(std::size_t{1});

			domain.advance_epoch(); // audio_epoch: 1 -> 2 (one boundary past the stamp)
			pub.reclaim();
			expect(freed).not_().to_contain(first);
			expect(pub.retired_count()).to_equal(std::size_t{1});

			domain.advance_epoch(); // audio_epoch: 2 -> 3 (two boundaries past the stamp: 1 + 2 <= 3)
			pub.reclaim();
			expect(freed).to_contain(first);
			expect(pub.retired_count()).to_equal(std::size_t{0});
		});

		it("frees exactly at stamp_epoch + 2, not one block early", _{
			std::vector<const int*> freed;
			SnapshotDomain domain;
			RecordingPublished pub(domain, nullptr, RecordingDeleter{&freed});

			auto* first = new int(1);
			pub.publish(first);
			// Retire `second` at the current epoch (0), superseding `first`.
			auto* second = new int(2);
			pub.publish(second); // retires `first` @ epoch 0

			domain.advance_epoch(); // epoch 1: 0 + 2 <= 1 is false
			pub.reclaim();
			expect(freed).not_().to_contain(first);

			domain.advance_epoch(); // epoch 2: 0 + 2 <= 2 is true
			pub.reclaim();
			expect(freed).to_contain(first);
		});
	});

	it("forces an inline reclaim() once the retire list exceeds its threshold", _{
		std::vector<const int*> freed;
		SnapshotDomain domain;
		constexpr std::size_t kThreshold = 2;
		RecordingPublished pub(domain, nullptr, RecordingDeleter{&freed}, kThreshold);

		auto* a = new int(1);
		auto* b = new int(2);
		auto* c = new int(3);
		auto* d = new int(4);

		pub.publish(a); // cur_ = a, nothing retired yet

		pub.publish(b); // retires a, stamped at epoch 0 (1 retired, <= threshold: no auto-sweep)
		expect(pub.retired_count()).to_equal(std::size_t{1});

		domain.advance_epoch();
		domain.advance_epoch(); // epoch now 2, so `a`'s stamp (0) is old enough to free: 0 + 2 <= 2

		pub.publish(c); // retires b, stamped at epoch 2 (2 retired, == threshold: still no auto-sweep)
		expect(pub.retired_count()).to_equal(std::size_t{2});

		// Retiring d pushes the list to 3, past kThreshold == 2, which forces an
		// inline reclaim() as part of publish() itself -- with no explicit
		// reclaim() call from the test. Only `a` is old enough to be freed by
		// the grace-period rule; b and c were just stamped at the current epoch.
		pub.publish(d); // retires c, stamped at epoch 2 -- 3 retired triggers reclaim()
		expect(freed).to_contain(a);
		expect(freed).not_().to_contain(b);
		expect(pub.retired_count()).to_equal(std::size_t{2}); // b and c remain
	});
});

CPPSPEC_SPEC(published)
