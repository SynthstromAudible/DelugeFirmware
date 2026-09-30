// tests/unit/sync/sd_access_spec.cpp
#include "sync/sd_access.h"

#include "cppspec.hpp"

// Provide the seam the query reads (in firmware the BSP provides it).
namespace {
bool g_fs_busy = false;
}
extern "C" bool deluge_storage_fs_busy(void) noexcept {
	return g_fs_busy;
}

// clang-format off
describe sd_access("deluge::sync::sd_busy", $ {
	it("is false when the filesystem is not busy", _ {
		g_fs_busy = false;
		expect(deluge::sync::sd_busy()).to_be_falsy();
	});

	it("is true when the filesystem is busy", _ {
		g_fs_busy = true;
		expect(deluge::sync::sd_busy()).to_be_truthy();
	});

	it("tracks the seam rather than a hardcoded constant", _ {
		g_fs_busy = true;
		expect(deluge::sync::sd_busy()).to_be_truthy();
		g_fs_busy = false;
		expect(deluge::sync::sd_busy()).to_be_falsy();
	});
});

CPPSPEC_SPEC(sd_access)
