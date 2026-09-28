// tests/unit/host_passthrough/path_ops_spec.cpp
#include "passthrough_test_util.h"

#include "libdeluge/file_io.h"

#include "cppspec.hpp"
#include <cstdint>

// clang-format off
describe path_ops("passthrough path ops", $ {
	it("mkdir is recursive and idempotent", _ {
		fresh_root();
		expect(deluge_efatfs_mkdir("X/Y/Z")).to_equal(DELUGE_OK);
		expect(deluge_efatfs_mkdir("X/Y/Z")).to_equal(DELUGE_OK); // already exists -> success
		uint32_t dh = 0;
		expect(deluge_efatfs_dir_open("X/Y", &dh)).to_equal(DELUGE_OK);
		deluge_efatfs_dir_close(dh);
	});

	it("rename fails when the destination already exists", _ {
		fresh_root();
		uint32_t h = 0;
		deluge_efatfs_file_open("SRC.TXT", DELUGE_FILE_WRITE_CREATE, &h); deluge_efatfs_file_close(h);
		deluge_efatfs_file_open("DST.TXT", DELUGE_FILE_WRITE_CREATE, &h); deluge_efatfs_file_close(h);
		expect(deluge_efatfs_rename("SRC.TXT", "DST.TXT")).to_equal(DELUGE_ERR_EXISTS); // dest exists
		expect(deluge_efatfs_rename("SRC.TXT", "NEW.TXT")).to_equal(DELUGE_OK);
	});

	it("unlink removes a file; set_time fails on a missing file", _ {
		fresh_root();
		uint32_t h = 0;
		deluge_efatfs_file_open("K.TXT", DELUGE_FILE_WRITE_CREATE, &h); deluge_efatfs_file_close(h);
		expect(deluge_efatfs_unlink("K.TXT")).to_equal(DELUGE_OK);
		expect(deluge_efatfs_unlink("K.TXT")).to_equal(DELUGE_ERR_NOT_FOUND);                 // gone now
		expect(deluge_efatfs_set_time("K.TXT", 2026, 8, 4, 12, 0, 0)).to_equal(DELUGE_ERR_NOT_FOUND); // missing -> fail
		expect(deluge_efatfs_set_time("K.TXT", 2026, 13, 4, 12, 0, 0)).to_equal(DELUGE_ERR_PARAM); // bad month -> fail
	});
});

CPPSPEC_SPEC(path_ops)
