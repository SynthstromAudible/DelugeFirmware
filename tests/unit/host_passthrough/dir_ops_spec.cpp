// tests/unit/host_passthrough/dir_ops_spec.cpp
#include "passthrough_test_util.h"

#include "libdeluge/file_io.h"

#include "cppspec.hpp"
#include <cstdint>
#include <string>

// clang-format off
describe dir_ops("passthrough directories", $ {
	it("enumerates children, skips . and .., ends with has_entry=false", _ {
		fresh_root();
		uint32_t fh = 0;
		deluge_efatfs_file_open("D/a.xml", DELUGE_FILE_WRITE_CREATE, &fh);       // auto-creates D/
		uint32_t w = 0; deluge_efatfs_file_write(fh, "xyz", 3, &w);
		deluge_efatfs_file_close(fh);
		deluge_efatfs_file_open("D/b.xml", DELUGE_FILE_WRITE_CREATE, &fh);
		deluge_efatfs_file_close(fh);

		uint32_t dh = 0;
		expect(deluge_efatfs_dir_open("D", &dh)).to_equal(DELUGE_OK);
		int count = 0;
		bool saw_a = false;
		while (true) {
			char name[64] = {0};
			bool is_dir = true;
			uint32_t size = 999, mtime = 0;
			uint8_t attrs = 0;
			bool has = false;
			expect(deluge_efatfs_dir_read(dh, name, sizeof name, &is_dir, &size, &mtime, &attrs, &has))
			    .to_equal(DELUGE_OK);
			if (!has) break;
			count++;
			std::string n(name);
			expect(n != "." && n != "..").to_equal(true);
			if (n == "a.xml") { saw_a = true; expect(is_dir).to_equal(false); expect(size).to_equal(3u); }
		}
		expect(count).to_equal(2);
		expect(saw_a).to_equal(true);
		deluge_efatfs_dir_close(dh);
	});

	it("reports size 0 and is_dir for a subdirectory", _ {
		fresh_root();
		uint32_t fh = 0;
		deluge_efatfs_file_open("P/sub/keep.txt", DELUGE_FILE_WRITE_CREATE, &fh); // makes P/sub/
		deluge_efatfs_file_close(fh);

		uint32_t dh = 0;
		deluge_efatfs_dir_open("P", &dh);
		char name[64] = {0}; bool is_dir = false; uint32_t size = 7, mtime = 0; uint8_t attrs = 0; bool has = false;
		deluge_efatfs_dir_read(dh, name, sizeof name, &is_dir, &size, &mtime, &attrs, &has);
		expect(has).to_equal(true);
		expect(std::string(name)).to_equal(std::string("sub"));
		expect(is_dir).to_equal(true);
		expect(size).to_equal(0u);
		deluge_efatfs_dir_close(dh);
	});
});

CPPSPEC_SPEC(dir_ops)
