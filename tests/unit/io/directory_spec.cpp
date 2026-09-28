// tests/unit/io/directory_spec.cpp
#include "io/file.hpp"
#include "mock_file_io.h"

#include "cppspec.hpp"

// clang-format off
describe directory("deluge::io::Directory", $ {
	it("iterates direct children, ending with nullopt", _ {
		mock_file_io_reset();
		deluge_efatfs_mkdir("SONGS");
		auto f = deluge::io::File::open("SONGS/a.xml", DELUGE_FILE_WRITE_CREATE);
		(void)f->close();
		deluge_efatfs_mkdir("SONGS/sub");

		auto dir = deluge::io::Directory::open("SONGS");
		// Directory is move-only, so std::expected<Directory, Status> can't be
		// copied into cppspec's by-value expect() (same issue/fix as File in
		// file_spec.cpp); check has_value() instead of the object itself.
		expect(dir.has_value()).to_equal(true);

		int count = 0;
		bool saw_file = false, saw_dir = false;
		while (true) {
			auto entry = dir->read();
			expect(entry).to_have_value();
			if (!entry->has_value()) break;
			count++;
			if (std::string((*entry)->name) == "a.xml") { saw_file = true; expect((*entry)->is_directory).to_equal(false); }
			if (std::string((*entry)->name) == "sub") { saw_dir = true; expect((*entry)->is_directory).to_equal(true); }
		}
		expect(count).to_equal(2);
		expect(saw_file).to_equal(true);
		expect(saw_dir).to_equal(true);
	});

	it("an empty directory reads nullopt immediately", _ {
		mock_file_io_reset();
		deluge_efatfs_mkdir("EMPTY");
		auto dir = deluge::io::Directory::open("EMPTY");
		expect(dir.has_value()).to_equal(true);
		auto entry = dir->read();
		expect(entry).to_have_value();
		expect(entry->has_value()).to_equal(false);
	});

	it("is move-only: moving leaves the source unable to double-close", _ {
		mock_file_io_reset();
		deluge_efatfs_mkdir("SONGS");
		auto opened = deluge::io::Directory::open("SONGS");
		deluge::io::Directory moved = std::move(*opened);
		auto closed = moved.close();
		expect(closed).to_have_value();
	});

	it("propagates size through Directory::read", _ {
		mock_file_io_reset();
		deluge_efatfs_mkdir("SONGS");
		auto f = deluge::io::File::open("SONGS/big.wav", DELUGE_FILE_WRITE_CREATE);
		std::byte buf[10]{};
		(void)f->write(buf);
		(void)f->close();

		auto dir = deluge::io::Directory::open("SONGS");
		expect(dir.has_value()).to_equal(true);
		auto entry = dir->read();
		expect(entry).to_have_value();
		expect(entry->has_value()).to_equal(true);
		expect((*entry)->size).to_equal(10u);
	});
});

CPPSPEC_SPEC(directory)
