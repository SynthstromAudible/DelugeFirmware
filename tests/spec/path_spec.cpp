#include "util/path.h"

#include "cppspec.hpp"
#include <memory>

std::shared_ptr<PathComponent> Path::root_ = std::make_shared<PathComponent>();

// clang-format off
describe path("Path", $ {
	it("parses a filename", _ {
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
		std::weak_ptr<PathComponent> weak_path;
		{
			Path path = "/home/Kate/GitHub/DelugeFirmware/tests/build/spec/all_specs.exe";
			weak_path = path.data();
		}
		expect(weak_path.expired()).to_be_true();
	});

	it("appends", _ {
		Path path = "/home/Kate/GitHub/DelugeFirmware/tests/build";
		path /= "spec";
		path /= "all_specs.exe";
		expect(path.to_string()).to_equal("/home/Kate/GitHub/DelugeFirmware/tests/build/spec/all_specs.exe");
	});

	it("concats", _ {
		Path path = "/home/Kate/GitHub/DelugeFirmware/tests/build";
		path += "spec";
		path += "all_specs.exe";
		expect(path.to_string()).to_equal("/home/Kate/GitHub/DelugeFirmware/tests/buildspecall_specs.exe");
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
});

CPPSPEC_SPEC(path)
