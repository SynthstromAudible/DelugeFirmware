// tests/unit/host_passthrough/file_ops_spec.cpp
#include "passthrough_test_util.h"

#include "libdeluge/file_io.h"

#include "cppspec.hpp"
#include <cstdint>
#include <cstring>

// clang-format off
describe file_ops("passthrough task-context files", $ {
	it("write-create then read round-trips", _ {
		fresh_root();
		uint32_t h = 0;
		expect(deluge_efatfs_file_open("A.TXT", DELUGE_FILE_WRITE_CREATE, &h)).to_equal(DELUGE_OK);
		uint32_t wrote = 0;
		expect(deluge_efatfs_file_write(h, "hello", 5, &wrote)).to_equal(DELUGE_OK);
		expect(wrote).to_equal(5u);
		deluge_efatfs_file_close(h);

		expect(deluge_efatfs_file_open("A.TXT", DELUGE_FILE_READ, &h)).to_equal(DELUGE_OK);
		char buf[5] = {0};
		uint32_t got = 0;
		expect(deluge_efatfs_file_read_exact(h, buf, 5, &got)).to_equal(DELUGE_OK);
		expect(got).to_equal(5u);
		expect(std::string(buf, 5)).to_equal(std::string("hello"));
		deluge_efatfs_file_close(h);
	});

	it("read FILLS (zero-pads) past EOF; read_exact SHORT-reads", _ {
		fresh_root();
		uint32_t h = 0;
		deluge_efatfs_file_open("B.TXT", DELUGE_FILE_WRITE_CREATE, &h);
		uint32_t wrote = 0;
		deluge_efatfs_file_write(h, "ab", 2, &wrote);
		deluge_efatfs_file_close(h);

		// read: 5 requested over a 2-byte file -> out_read==5, tail zero-padded.
		deluge_efatfs_file_open("B.TXT", DELUGE_FILE_READ, &h);
		unsigned char rf[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
		uint32_t nf = 0;
		expect(deluge_efatfs_file_read(h, rf, 5, &nf)).to_equal(DELUGE_OK);
		expect(nf).to_equal(5u);
		expect(rf[2] == 0 && rf[3] == 0 && rf[4] == 0).to_equal(true);
		deluge_efatfs_file_close(h);

		// read_exact: 5 requested over a 2-byte file -> out_read==2, no padding.
		deluge_efatfs_file_open("B.TXT", DELUGE_FILE_READ, &h);
		unsigned char re[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
		uint32_t ne = 0;
		expect(deluge_efatfs_file_read_exact(h, re, 5, &ne)).to_equal(DELUGE_OK);
		expect(ne).to_equal(2u);
		expect(re[2]).to_equal(static_cast<unsigned char>(0xFF));  // untouched past the real bytes
		deluge_efatfs_file_close(h);
	});

	it("seek is absolute; size leaves the cursor put", _ {
		fresh_root();
		uint32_t h = 0;
		deluge_efatfs_file_open("C.TXT", DELUGE_FILE_WRITE_CREATE, &h);
		uint32_t wrote = 0;
		deluge_efatfs_file_write(h, "0123456789", 10, &wrote);
		deluge_efatfs_file_close(h);

		deluge_efatfs_file_open("C.TXT", DELUGE_FILE_READ, &h);
		expect(deluge_efatfs_file_seek(h, 4)).to_equal(DELUGE_OK);
		uint32_t sz = 0;
		expect(deluge_efatfs_file_size(h, &sz)).to_equal(DELUGE_OK);
		expect(sz).to_equal(10u);
		char two[2] = {0};
		uint32_t n = 0;
		deluge_efatfs_file_read_exact(h, two, 2, &n);  // cursor still at 4 after size()
		expect(std::string(two, 2)).to_equal(std::string("45"));
		deluge_efatfs_file_close(h);
	});

	it("truncate is shrink-only (a grow request is a no-op)", _ {
		fresh_root();
		uint32_t h = 0;
		deluge_efatfs_file_open("D.TXT", DELUGE_FILE_WRITE_CREATE, &h);
		uint32_t wrote = 0;
		deluge_efatfs_file_write(h, "12345678", 8, &wrote);
		expect(deluge_efatfs_file_truncate(h, 3)).to_equal(DELUGE_OK);   // shrink to 3
		uint32_t sz = 0;
		deluge_efatfs_file_size(h, &sz);
		expect(sz).to_equal(3u);
		expect(deluge_efatfs_file_truncate(h, 100)).to_equal(DELUGE_OK); // grow request...
		deluge_efatfs_file_size(h, &sz);
		expect(sz).to_equal(3u);                                    // ...is a no-op
		deluge_efatfs_file_close(h);
	});

	it("write-create-new fails if the file exists; write-create auto-makes parents", _ {
		fresh_root();
		uint32_t h = 0;
		expect(deluge_efatfs_file_open("SUB/DEEP/E.TXT", DELUGE_FILE_WRITE_CREATE, &h)).to_equal(DELUGE_OK);
		deluge_efatfs_file_close(h);
		expect(deluge_efatfs_file_open("SUB/DEEP/E.TXT", DELUGE_FILE_WRITE_CREATE_NEW, &h)).to_equal(DELUGE_ERR_EXISTS);
	});

	it("write reports failure on a hard I/O error instead of swallowing it", _ {
		fresh_root();
		uint32_t h = 0;
		deluge_efatfs_file_open("F.TXT", DELUGE_FILE_WRITE_CREATE, &h);
		uint32_t wrote = 0;
		deluge_efatfs_file_write(h, "12345", 5, &wrote);
		deluge_efatfs_file_close(h);

		// Reopen READ-ONLY: the slot's fd is O_RDONLY, so pwrite() fails with EBADF
		// (a hard error, not EINTR) -- deluge_efatfs_file_write must report that as failure,
		// not swallow it and return true with a partial *out_written.
		expect(deluge_efatfs_file_open("F.TXT", DELUGE_FILE_READ, &h)).to_equal(DELUGE_OK);
		uint32_t wrote2 = 0;
		expect(deluge_efatfs_file_write(h, "ab", 2, &wrote2)).to_equal(DELUGE_ERR_IO);
		deluge_efatfs_file_close(h);
	});
});

CPPSPEC_SPEC(file_ops)
