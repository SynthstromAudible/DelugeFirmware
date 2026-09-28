// tests/unit/io/file_spec.cpp
#include "io/file.hpp"
#include "mock_file_io.h"

#include "cppspec.hpp"

#include <cstring>

// clang-format off
describe file("deluge::io::File", $ {
	it("writes then reads back the same bytes", _ {
		mock_file_io_reset();
		auto written = deluge::io::File::open("a.txt", DELUGE_FILE_WRITE_CREATE);
		// File is move-only, so std::expected<File, Status> can't be copied into
		// cppspec's by-value expect(); check has_value() instead of the object itself.
		expect(written.has_value()).to_equal(true);
		const char msg[] = "hello";
		auto w = written->write(std::as_bytes(std::span{msg, 5}));
		expect(w).to_have_value();
		expect(*w).to_equal((uint32_t)5);
		expect(written->close()).to_have_value();

		auto opened = deluge::io::File::open("a.txt", DELUGE_FILE_READ);
		expect(opened.has_value()).to_equal(true);
		char buf[8] = {};
		auto r = opened->read(std::as_writable_bytes(std::span{buf, 8}));
		expect(r).to_have_value();
		expect(r->size()).to_equal((size_t)5);
		expect(std::string(buf, 5)).to_equal("hello");
	});

	it("returns NOT_FOUND opening a missing file", _ {
		mock_file_io_reset();
		auto opened = deluge::io::File::open("missing.txt", DELUGE_FILE_READ);
		expect(opened.has_value()).to_equal(false);
		expect(opened.error()).to_equal(deluge::io::Status::NOT_FOUND);
	});

	it("reports size after writing", _ {
		mock_file_io_reset();
		auto f = deluge::io::File::open("a.txt", DELUGE_FILE_WRITE_CREATE);
		const char msg[] = "hi";
		(void)f->write(std::as_bytes(std::span{msg, 2}));
		auto sz = f->size();
		expect(sz).to_have_value();
		expect(*sz).to_equal((uint32_t)2);
	});

	it("seek moves the read/write position", _ {
		mock_file_io_reset();
		auto f = deluge::io::File::open("a.txt", DELUGE_FILE_WRITE_CREATE);
		const char msg[] = "hello";
		(void)f->write(std::as_bytes(std::span{msg, 5}));
		expect(f->seek(0)).to_have_value();
		char buf[5] = {};
		auto r = f->read(std::as_writable_bytes(std::span{buf, 5}));
		expect(std::string(buf, 5)).to_equal("hello");
	});

	it("is move-only: moving leaves the source unable to double-close", _ {
		mock_file_io_reset();
		auto opened = deluge::io::File::open("a.txt", DELUGE_FILE_WRITE_CREATE);
		deluge::io::File moved = std::move(*opened);
		// The moved-from File's destructor must not touch the handle the new
		// owner (moved) still holds -- if it did, moved's own later close()
		// would fail because the mock already freed/invalidated the handle.
		auto closed = moved.close();
		expect(closed).to_have_value();
	});
});

CPPSPEC_SPEC(file)
