// tests/unit/host_passthrough/mount_ops_spec.cpp
#include "passthrough_test_util.h"

#include "libdeluge/file_io.h"

#include "cppspec.hpp"
#include <cstdint>

// clang-format off
describe mount_ops("passthrough deluge_efatfs_mount/_remount/_cluster_size", $ {
	it("mount is a no-op success on the host", _ {
		fresh_root();
		expect(deluge_efatfs_mount()).to_equal(true);
	});

	it("is_mounted reports true once mount has run", _ {
		fresh_root();
		expect(deluge_efatfs_mount()).to_equal(true);
		expect(deluge_efatfs_is_mounted()).to_equal(true);
	});

	it("remount is a no-op success on the host", _ {
		fresh_root();
		expect(deluge_efatfs_remount()).to_equal(true);
	});

	it("reports a nonzero cluster size for a real filesystem", _ {
		fresh_root();
		uint32_t bytes = 0;
		expect(deluge_efatfs_cluster_size(&bytes)).to_equal(true);
		expect(bytes > 0).to_equal(true);
	});

	it("fails when the out-param is null", _ {
		fresh_root();
		expect(deluge_efatfs_cluster_size(nullptr)).to_equal(false);
	});
});

CPPSPEC_SPEC(mount_ops)
