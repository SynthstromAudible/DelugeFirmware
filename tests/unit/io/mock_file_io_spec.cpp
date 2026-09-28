// tests/unit/io/mock_file_io_spec.cpp
#include "mock_file_io.h"

extern "C" {
#include "libdeluge/file_io.h"
}

#include "cppspec.hpp"

#include <cstring>

// clang-format off
describe mock_file_io("the file_io.h mock backing", $ {
	it("round-trips a write then read", _ {
		mock_file_io_reset();
		uint32_t h = 0;
		expect(deluge_efatfs_file_open("a.txt", DELUGE_FILE_WRITE_CREATE, &h)).to_equal(DELUGE_OK);
		const char* msg = "hello";
		uint32_t written = 0;
		expect(deluge_efatfs_file_write(h, msg, 5, &written)).to_equal(DELUGE_OK);
		expect(written).to_equal((uint32_t)5);
		deluge_efatfs_file_close(h);

		h = 0;
		expect(deluge_efatfs_file_open("a.txt", DELUGE_FILE_READ, &h)).to_equal(DELUGE_OK);
		char buf[8] = {};
		uint32_t read = 0;
		expect(deluge_efatfs_file_read_exact(h, buf, 8, &read)).to_equal(DELUGE_OK);
		expect(read).to_equal((uint32_t)5);
		expect(std::string(buf, 5)).to_equal("hello");
		deluge_efatfs_file_close(h);
	});

	it("read fills the buffer, zero-padding a short tail", _ {
		mock_file_io_reset();
		uint32_t h = 0;
		deluge_efatfs_file_open("a.txt", DELUGE_FILE_WRITE_CREATE, &h);
		const char* msg = "hi";
		uint32_t written = 0;
		deluge_efatfs_file_write(h, msg, 2, &written);
		deluge_efatfs_file_close(h);

		h = 0;
		expect(deluge_efatfs_file_open("a.txt", DELUGE_FILE_READ, &h)).to_equal(DELUGE_OK);
		char buf[4] = {'x', 'x', 'x', 'x'};
		uint32_t read = 0;
		expect(deluge_efatfs_file_read(h, buf, 4, &read)).to_equal(DELUGE_OK);
		expect(read).to_equal((uint32_t)4);
		expect(buf[0]).to_equal('h');
		expect(buf[1]).to_equal('i');
		expect(buf[2]).to_equal('\0');
		expect(buf[3]).to_equal('\0');
		deluge_efatfs_file_close(h);
	});

	it("reports NOT_FOUND opening a missing file for read", _ {
		mock_file_io_reset();
		uint32_t h = 0;
		expect(deluge_efatfs_file_open("missing.txt", DELUGE_FILE_READ, &h)).to_equal(DELUGE_ERR_NOT_FOUND);
	});

	it("mkdir is idempotent -- a repeat succeeds", _ {
		// embedded-fatfs's create_dir is idempotent (unlike C-FatFS's f_mkdir,
		// which reports EXIST on a repeat).
		mock_file_io_reset();
		expect(deluge_efatfs_mkdir("SONGS")).to_equal(DELUGE_OK);
		expect(deluge_efatfs_mkdir("SONGS")).to_equal(DELUGE_OK);
	});

	it("mkdir on a path already occupied by a file reports EXISTS", _ {
		mock_file_io_reset();
		uint32_t h = 0;
		deluge_efatfs_file_open("a.txt", DELUGE_FILE_WRITE_CREATE, &h);
		deluge_efatfs_file_close(h);
		expect(deluge_efatfs_mkdir("a.txt")).to_equal(DELUGE_ERR_EXISTS);
	});

	it("unlink removes an entry", _ {
		mock_file_io_reset();
		uint32_t h = 0;
		deluge_efatfs_file_open("a.txt", DELUGE_FILE_WRITE_CREATE, &h);
		deluge_efatfs_file_close(h);
		expect(deluge_efatfs_unlink("a.txt")).to_equal(DELUGE_OK);
		h = 0;
		expect(deluge_efatfs_file_open("a.txt", DELUGE_FILE_READ, &h)).to_equal(DELUGE_ERR_NOT_FOUND);
	});

	it("rename moves an entry to a new path", _ {
		mock_file_io_reset();
		uint32_t h = 0;
		deluge_efatfs_file_open("old.txt", DELUGE_FILE_WRITE_CREATE, &h);
		deluge_efatfs_file_close(h);
		expect(deluge_efatfs_rename("old.txt", "new.txt")).to_equal(DELUGE_OK);
		h = 0;
		expect(deluge_efatfs_file_open("old.txt", DELUGE_FILE_READ, &h)).to_equal(DELUGE_ERR_NOT_FOUND);
		h = 0;
		expect(deluge_efatfs_file_open("new.txt", DELUGE_FILE_READ, &h)).to_equal(DELUGE_OK);
		deluge_efatfs_file_close(h);
	});

	it("lists direct children of a directory, distinguishing files from subdirectories", _ {
		mock_file_io_reset();
		deluge_efatfs_mkdir("SONGS");
		uint32_t h = 0;
		deluge_efatfs_file_open("SONGS/a.xml", DELUGE_FILE_WRITE_CREATE, &h);
		deluge_efatfs_file_close(h);
		deluge_efatfs_mkdir("SONGS/sub");

		uint32_t d = 0;
		expect(deluge_efatfs_dir_open("SONGS", &d)).to_equal(DELUGE_OK);
		bool saw_file = false, saw_dir = false;
		bool has_entry = true;
		while (has_entry) {
			char name[DELUGE_MAX_FILENAME] = {};
			bool is_dir = false;
			uint32_t size = 0;
			uint32_t modified = 0;
			uint8_t attrs = 0;
			expect(deluge_efatfs_dir_read(d, name, DELUGE_MAX_FILENAME, &is_dir, &size, &modified, &attrs,
			                              &has_entry))
			    .to_equal(DELUGE_OK);
			if (!has_entry) break;
			if (std::string(name) == "a.xml") { saw_file = true; expect(is_dir).to_equal(false); }
			if (std::string(name) == "sub") { saw_dir = true; expect(is_dir).to_equal(true); }
		}
		expect(saw_file).to_equal(true);
		expect(saw_dir).to_equal(true);
		deluge_efatfs_dir_close(d);
	});

	// Defensive specs for the generation-guarded handle table (mock_file_io.cpp's
	// checkout/release_slot): a bad, freed, or stale handle must fail cleanly
	// (return DELUGE_ERR_PARAM) rather than abort the process. Before the handle
	// table was generation-guarded, these calls used to `.at().value()` a freed
	// slot and crash the whole test binary -- a passing run here, with the new
	// cases' names actually appearing in the output, is the proof.

	it("read after close fails cleanly, does not abort", _ {
		mock_file_io_reset();
		uint32_t h = 0;
		expect(deluge_efatfs_file_open("a.txt", DELUGE_FILE_WRITE_CREATE, &h)).to_equal(DELUGE_OK);
		deluge_efatfs_file_close(h);

		char buf[4] = {};
		uint32_t read = 0;
		expect(deluge_efatfs_file_read(h, buf, 4, &read)).to_equal(DELUGE_ERR_PARAM);
	});

	it("write/seek/size after close fail cleanly, do not abort", _ {
		mock_file_io_reset();
		uint32_t h = 0;
		expect(deluge_efatfs_file_open("a.txt", DELUGE_FILE_WRITE_CREATE, &h)).to_equal(DELUGE_OK);
		deluge_efatfs_file_close(h);

		const char* msg = "x";
		uint32_t written = 0;
		expect(deluge_efatfs_file_write(h, msg, 1, &written)).to_equal(DELUGE_ERR_PARAM);
		expect(deluge_efatfs_file_seek(h, 0)).to_equal(DELUGE_ERR_PARAM);
		uint32_t size = 0;
		expect(deluge_efatfs_file_size(h, &size)).to_equal(DELUGE_ERR_PARAM);
	});

	it("a never-allocated handle fails cleanly, does not abort", _ {
		mock_file_io_reset();
		char buf[4] = {};
		uint32_t read = 0;
		expect(deluge_efatfs_file_read(0xDEADBEEF, buf, 4, &read)).to_equal(DELUGE_ERR_PARAM);
		uint32_t size = 0;
		expect(deluge_efatfs_file_size(0xDEADBEEF, &size)).to_equal(DELUGE_ERR_PARAM);

		// A small, plausible-looking handle is equally bad: the table is empty,
		// so any index is out of range regardless of how "reasonable" it looks.
		expect(deluge_efatfs_file_read(0, buf, 4, &read)).to_equal(DELUGE_ERR_PARAM);
		expect(deluge_efatfs_file_size(1, &size)).to_equal(DELUGE_ERR_PARAM);
	});

	it("a stale handle after slot reuse does not alias the new occupant", _ {
		mock_file_io_reset();
		uint32_t hA = 0;
		expect(deluge_efatfs_file_open("a.txt", DELUGE_FILE_WRITE_CREATE, &hA)).to_equal(DELUGE_OK);
		const char* msgA = "AAAA";
		uint32_t written = 0;
		expect(deluge_efatfs_file_write(hA, msgA, 4, &written)).to_equal(DELUGE_OK);
		deluge_efatfs_file_close(hA);

		// b.txt's open reuses a.txt's freed slot, with a bumped generation.
		uint32_t hB = 0;
		expect(deluge_efatfs_file_open("b.txt", DELUGE_FILE_WRITE_CREATE, &hB)).to_equal(DELUGE_OK);
		expect(hB).not_().to_equal(hA);
		const char* msgB = "BBBB";
		expect(deluge_efatfs_file_write(hB, msgB, 4, &written)).to_equal(DELUGE_OK);

		// The stale hA must fail, not silently read b.txt's bytes through the
		// reused slot.
		char buf[4] = {};
		uint32_t read = 0;
		expect(deluge_efatfs_file_read(hA, buf, 4, &read)).to_equal(DELUGE_ERR_PARAM);

		// hB itself is unaffected: reopened for read, it still sees its own bytes.
		deluge_efatfs_file_close(hB);
		uint32_t hB2 = 0;
		expect(deluge_efatfs_file_open("b.txt", DELUGE_FILE_READ, &hB2)).to_equal(DELUGE_OK);
		char buf2[4] = {};
		uint32_t read2 = 0;
		expect(deluge_efatfs_file_read_exact(hB2, buf2, 4, &read2)).to_equal(DELUGE_OK);
		expect(std::string(buf2, 4)).to_equal("BBBB");
		deluge_efatfs_file_close(hB2);
	});

	it("dir handle after close fails cleanly, does not abort", _ {
		mock_file_io_reset();
		deluge_efatfs_mkdir("SONGS");
		uint32_t d = 0;
		expect(deluge_efatfs_dir_open("SONGS", &d)).to_equal(DELUGE_OK);
		deluge_efatfs_dir_close(d);

		char name[DELUGE_MAX_FILENAME] = {};
		bool is_dir = false;
		uint32_t size = 0;
		uint32_t modified = 0;
		uint8_t attrs = 0;
		bool has_entry = true;
		expect(deluge_efatfs_dir_read(d, name, DELUGE_MAX_FILENAME, &is_dir, &size, &modified, &attrs, &has_entry))
		    .to_equal(DELUGE_ERR_PARAM);
	});
});

CPPSPEC_SPEC(mock_file_io)
