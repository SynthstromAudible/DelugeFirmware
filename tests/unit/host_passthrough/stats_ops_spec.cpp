// tests/unit/host_passthrough/stats_ops_spec.cpp
#include "passthrough_test_util.h"

#include "libdeluge/file_io.h"

#include "cppspec.hpp"
#include <cstdint>

// clang-format off
describe stats_ops("passthrough deluge_efatfs_stats", $ {
	it("reports free/total clusters for a real filesystem", _ {
		fresh_root();
		uint32_t free_clusters = 0;
		uint32_t total_clusters = 0;
		expect(deluge_efatfs_stats(&free_clusters, &total_clusters)).to_equal(true);
		expect(total_clusters > 0).to_equal(true);
		expect(total_clusters >= free_clusters).to_equal(true);
	});

	it("fails when both out-params are null", _ {
		fresh_root();
		expect(deluge_efatfs_stats(nullptr, nullptr)).to_equal(false);
	});
});

CPPSPEC_SPEC(stats_ops)
