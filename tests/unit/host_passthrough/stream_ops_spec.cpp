// tests/unit/host_passthrough/stream_ops_spec.cpp
#include "passthrough_test_util.h"

#include "libdeluge/stream_io.h"

#include "cppspec.hpp"
#include <cstdint>
#include <string>

// clang-format off
describe stream_ops("passthrough stream-write", $ {
	it("write_at extends the in-memory extent; read_at_via reads it back pre-flush", _ {
		fresh_root();
		uint32_t h = 0;
		expect(deluge_efatfs_stream_open("REC.WAV", DELUGE_STREAM_WRITE_CREATE, &h)).to_equal(true);
		uint32_t wrote = 0;
		expect(deluge_efatfs_stream_write_at(h, 0, "ABCDEFGH", 8, &wrote)).to_equal(true);
		expect(wrote).to_equal(8u);
		uint32_t sz = 0;
		expect(deluge_efatfs_stream_size(h, &sz)).to_equal(true);
		expect(sz).to_equal(8u);                       // in-memory extent, pre-flush

		char back[4] = {0};
		uint32_t got = 0;
		expect(deluge_efatfs_stream_read_at_via(h, 4, back, 4, &got)).to_equal(true);
		expect(got).to_equal(4u);
		expect(std::string(back, 4)).to_equal(std::string("EFGH"));

		// read_at_via is EOF-honest: reading past the in-memory size short-reads.
		char over[4] = {0};
		uint32_t g2 = 0;
		expect(deluge_efatfs_stream_read_at_via(h, 6, over, 4, &g2)).to_equal(true);
		expect(g2).to_equal(2u);                       // only 2 bytes (offset 6..8) exist
		expect(deluge_efatfs_stream_close(h)).to_equal(true);
	});

	it("truncate is shrink-only and updates the reported size", _ {
		fresh_root();
		uint32_t h = 0;
		deluge_efatfs_stream_open("T.BIN", DELUGE_STREAM_WRITE_CREATE, &h);
		uint32_t w = 0;
		deluge_efatfs_stream_write_at(h, 0, "0123456789", 10, &w);
		expect(deluge_efatfs_stream_truncate(h, 4)).to_equal(true);
		uint32_t sz = 0; deluge_efatfs_stream_size(h, &sz);
		expect(sz).to_equal(4u);
		expect(deluge_efatfs_stream_truncate(h, 99)).to_equal(true); // grow request = no-op
		deluge_efatfs_stream_size(h, &sz);
		expect(sz).to_equal(4u);
		deluge_efatfs_stream_close(h);
	});

	it("write_at reports failure on a hard I/O error instead of swallowing it", _ {
		fresh_root();
		uint32_t h = 0;
		deluge_efatfs_stream_open("G.BIN", DELUGE_STREAM_WRITE_CREATE, &h);
		uint32_t w = 0;
		deluge_efatfs_stream_write_at(h, 0, "12345", 5, &w);
		deluge_efatfs_stream_close(h);

		// Reopen READ-ONLY: the slot's fd is O_RDONLY, so pwrite() fails with EBADF
		// (a hard error, not EINTR) -- deluge_efatfs_stream_write_at must report that as failure,
		// not swallow it and return true with a partial *out_written.
		expect(deluge_efatfs_stream_open("G.BIN", DELUGE_STREAM_READ, &h)).to_equal(true);
		uint32_t w2 = 0;
		expect(deluge_efatfs_stream_write_at(h, 0, "ab", 2, &w2)).to_equal(false);
		deluge_efatfs_stream_close(h);
	});
});

CPPSPEC_SPEC(stream_ops)
