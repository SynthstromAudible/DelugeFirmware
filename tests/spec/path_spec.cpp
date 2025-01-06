#include "util/filesystem.h"

#include "cppspec.hpp"
#include <memory>

using namespace deluge::filesystem;

// clang-format off
describe path("Path", $ {
	it("parses a root path", _ {
		Path path = "/";
		expect(path.data()).to_equal(Path::root());
		expect(path.to_string()).to_equal("/");
	});

	it("parses a simple filename", _ {
		Path path = "/path_spec.cpp";
		expect(path.basename()).to_equal("path_spec.cpp");
	});

	it("parses a simple path", _ {
		Path path = "/spec/path_spec.cpp";
		expect(path.basename()).to_equal("path_spec.cpp");
		expect(path.to_string()).to_equal("/spec/path_spec.cpp");
	});

	it("iterates properly forwards", _ {
		Path path = "/home/Kate/GitHub/DelugeFirmware/tests/build/spec/all_specs.exe";
		expect(path.basename()).to_equal("all_specs.exe");
		std::string s;
		for (const char c : path) {
			s.push_back(c);
		}
		expect(s.size()).to_equal(path.to_string().size());
		expect(s).to_equal(path.to_string());
	});


	it("cleans up after release", _ {
		std::weak_ptr<typename Path::component_type> weak_path;
		{
			Path path = "/home/Kate/GitHub/DelugeFirmware/tests/build/spec/all_specs.exe";
			weak_path = path.data();
		}
		expect(weak_path.expired()).to_be_true();
	});

	it("appends", _ {
		Path path = "build";
		path /= "spec";
		path /= "all_specs.exe";
		expect(path.to_string()).to_equal("/build/spec/all_specs.exe");
	});

	it("concats", _ {
		Path path = "build";
		path += "spec";
		path += "all_specs.exe";
		expect(path.to_string()).to_equal("/buildspecall_specs.exe");
	});

	it("merges strings", _ {
		Path path = "/home/Kate/GitHub/DelugeFirmware/tests/build/spec/all_specs.cpp";
		Path path2 = "/home/Kate/GitHub/DelugeFirmware/tests/build/spec/all_specs.exe";
		expect(path.data()->parent()).to_equal(path2.data()->parent());
	});

	context("ends_with", _{
		it("matches within the basename", _{
			Path path = "/home/Kate/GitHub/DelugeFirmware/tests/build/spec/all_specs.cpp";
			expect(path.ends_with(".cpp")).to_be_true();
		});

		it("matches beyond the basename", _{
			Path path = "/home/Kate/GitHub/DelugeFirmware/tests/build/spec/all_specs.cpp";
			expect(path.ends_with("spec/all_specs.cpp")).to_be_true();
		});

		it("does not match", _{
			Path path = "/home/Kate/GitHub/DelugeFirmware/tests/build/spec/all_specs.cpp";
			expect(path.ends_with("spec/all_specs.exe")).to_be_false();
		});
	});

	it("returns the extension", _{
		Path path = "all_specs.cpp";
		expect(path.extension()).to_equal(".cpp");
	});

	it("returns the stem of the filename", _{
		Path path = "all_specs.cpp";
		expect(path.stem()).to_equal("all_specs");
	});
});

CPPSPEC_SPEC(path)
