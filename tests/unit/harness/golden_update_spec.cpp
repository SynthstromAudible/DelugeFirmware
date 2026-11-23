#include "cppspec.hpp"
#include "support/golden_update.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// The C++ side of `./dbt test --update`: the switch, and the lines it leaves in the update log. The lines and the
// descriptions are the ones scripts/tasks/golden_update.py writes, so the runner reads both sides alike.
namespace fs = std::filesystem;
namespace golden = deluge::test::golden;

namespace {

/// @brief Sets the switch and the log for one block and puts the caller's values back after it: `./dbt test --update
/// unit` runs this spec with both already set.
class Switch {
public:
	explicit Switch(const fs::path& log)
	    : old_on_(get("DELUGE_GOLDEN_UPDATE")), old_log_(get("DELUGE_GOLDEN_UPDATE_LOG")) {
		::setenv("DELUGE_GOLDEN_UPDATE", "1", 1);
		::setenv("DELUGE_GOLDEN_UPDATE_LOG", log.c_str(), 1);
	}
	~Switch() {
		put("DELUGE_GOLDEN_UPDATE", old_on_);
		put("DELUGE_GOLDEN_UPDATE_LOG", old_log_);
	}
	Switch(const Switch&) = delete;
	Switch& operator=(const Switch&) = delete;

private:
	static std::optional<std::string> get(const char* name) {
		const char* value = std::getenv(name);
		return value != nullptr ? std::optional<std::string>{value} : std::nullopt;
	}
	static void put(const char* name, const std::optional<std::string>& value) {
		if (value) {
			::setenv(name, value->c_str(), 1);
		}
		else {
			::unsetenv(name);
		}
	}
	std::optional<std::string> old_on_;
	std::optional<std::string> old_log_;
};

/// @brief A fresh directory under the system temp dir, symlinks resolved as the log's paths are; the caller removes it.
fs::path fresh_dir() {
	std::string tmpl = (fs::temp_directory_path() / "golden_update_spec_XXXXXX").string();
	std::vector<char> buf(tmpl.begin(), tmpl.end());
	buf.push_back('\0');
	return fs::weakly_canonical(fs::path(::mkdtemp(buf.data())));
}

std::string slurp(const fs::path& path) {
	std::ifstream in(path, std::ios::binary);
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}

/// @brief How a child that runs @p body ends: its exit status, or -1 when it did not exit.
/// Output buffered before the fork is flushed first, so the child's exit cannot print it a second time.
template <typename F>
int exit_status_of(F body) {
	std::cout.flush();
	std::fflush(nullptr);
	const pid_t pid = ::fork();
	if (pid == 0) {
		body();
		::_exit(0);
	}
	int status = 0;
	::waitpid(pid, &status, 0);
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

} // namespace

// clang-format off
describe golden_update("deluge::test::golden", $ {
	it("is off unless the switch is exactly 1", _ {
		const fs::path dir = fresh_dir();
		const Switch on{dir / "log"};
		::setenv("DELUGE_GOLDEN_UPDATE", "0", 1);
		expect(golden::updating()).to_be_false();
		::unsetenv("DELUGE_GOLDEN_UPDATE");
		expect(golden::updating()).to_be_false();
		fs::remove_all(dir);
	});

	it("refuses the switch without the log, exiting 2", _ {
		const fs::path dir = fresh_dir();
		const Switch on{dir / "log"};
		::unsetenv("DELUGE_GOLDEN_UPDATE_LOG");
		expect(exit_status_of([] { ::close(2); static_cast<void>(golden::updating()); })).to_equal(2);
		::setenv("DELUGE_GOLDEN_UPDATE_LOG", "", 1);
		expect(exit_status_of([] { ::close(2); static_cast<void>(golden::updating()); })).to_equal(2);
		fs::remove_all(dir);
	});

	it("writes a changed file and logs it from absent", _ {
		const fs::path dir = fresh_dir();
		const Switch on{dir / "log"};
		expect(golden::updating()).to_be_true();
		expect(golden::write_if_changed(dir / "a.pbm", "P1\n")).to_be_true();
		expect(slurp(dir / "a.pbm")).to_equal(std::string{"P1\n"});
		expect(slurp(dir / "log")).to_equal("GOLDEN-UPDATED " + (dir / "a.pbm").string() + " (absent) -> "
		                                    + golden::describe(std::string{"P1\n"}) + "\n");
		fs::remove_all(dir);
	});

	it("names a file by its absolute path, as the Python writers do", _ {
		const fs::path dir = fresh_dir();
		const Switch on{dir / "log"};
		const fs::path old_cwd = fs::current_path();
		fs::current_path(dir);
		expect(golden::write_if_changed("sub/../a.pbm", "P1\n")).to_be_true();
		fs::current_path(old_cwd);
		expect(slurp(dir / "log")).to_start_with("GOLDEN-UPDATED " + (dir / "a.pbm").string() + " (absent) -> ");
		fs::remove_all(dir);
	});

	it("rewrites a changed file, keeping its mode and leaving no temp file", _ {
		const fs::path dir = fresh_dir();
		const Switch on{dir / "log"};
		std::ofstream(dir / "a.pbm") << "P1\n";
		::chmod((dir / "a.pbm").c_str(), 0640);
		expect(golden::write_if_changed(dir / "a.pbm", "P2\n")).to_be_true();
		expect(slurp(dir / "a.pbm")).to_equal(std::string{"P2\n"});
		expect(static_cast<unsigned>(fs::status(dir / "a.pbm").permissions())).to_equal(0640u);
		std::vector<std::string> names;
		for (const fs::directory_entry& e : fs::directory_iterator(dir)) {
			names.push_back(e.path().filename().string());
		}
		std::ranges::sort(names);
		expect(names).to_equal(std::vector<std::string>{"a.pbm", "log"});
		expect(slurp(dir / "log")).to_equal("GOLDEN-UPDATED " + (dir / "a.pbm").string() + " "
		                                    + golden::describe(std::string{"P1\n"}) + " -> "
		                                    + golden::describe(std::string{"P2\n"}) + "\n");
		fs::remove_all(dir);
	});

	it("makes a new file's directories and gives it mode 0644", _ {
		const fs::path dir = fresh_dir();
		const Switch on{dir / "log"};
		expect(golden::write_if_changed(dir / "x" / "y" / "a.state", "scene X\n")).to_be_true();
		expect(slurp(dir / "x" / "y" / "a.state")).to_equal(std::string{"scene X\n"});
		expect(static_cast<unsigned>(fs::status(dir / "x" / "y" / "a.state").permissions())).to_equal(0644u);
		fs::remove_all(dir);
	});

	it("leaves an unchanged file alone and logs nothing", _ {
		const fs::path dir = fresh_dir();
		const Switch on{dir / "log"};
		std::ofstream(dir / "a.pbm") << "P1\n";
		expect(golden::write_if_changed(dir / "a.pbm", "P1\n")).to_be_false();
		expect(fs::exists(dir / "log")).to_be_false();
		fs::remove_all(dir);
	});

	it("logs a removed file to absent", _ {
		const fs::path dir = fresh_dir();
		const Switch on{dir / "log"};
		std::ofstream(dir / "a.pbm") << "P1\n";
		expect(golden::remove_if_present(dir / "a.pbm")).to_be_true();
		expect(fs::exists(dir / "a.pbm")).to_be_false();
		expect(slurp(dir / "log")).to_equal("GOLDEN-UPDATED " + (dir / "a.pbm").string() + " "
		                                    + golden::describe(std::string{"P1\n"}) + " -> (absent)\n");
		fs::remove_all(dir);
	});

	it("removes nothing and logs nothing when there is no file", _ {
		const fs::path dir = fresh_dir();
		const Switch on{dir / "log"};
		fs::create_directories(dir / "sub");
		expect(golden::remove_if_present(dir / "a.pbm")).to_be_false();
		expect(golden::remove_if_present(dir / "sub")).to_be_false();
		expect(fs::is_directory(dir / "sub")).to_be_true();
		expect(fs::exists(dir / "log")).to_be_false();
		fs::remove_all(dir);
	});

	it("appends each line whole, after what the log already holds", _ {
		const fs::path dir = fresh_dir();
		const Switch on{dir / "log"};
		std::ofstream(dir / "log") << "GOLDEN-UPDATED earlier (absent) -> (absent)\n";
		golden::append("one");
		golden::append("two");
		expect(slurp(dir / "log")).to_equal(std::string{"GOLDEN-UPDATED earlier (absent) -> (absent)\none\ntwo\n"});
		fs::remove_all(dir);
	});

	it("keeps every line whole when processes append at once", _ {
		const fs::path dir = fresh_dir();
		const Switch on{dir / "log"};
		constexpr int kChildren = 8;
		constexpr int kLinesEach = 200;
		std::cout.flush();
		std::fflush(nullptr);
		std::vector<pid_t> children;
		for (int c = 0; c < kChildren; ++c) {
			const pid_t pid = ::fork();
			if (pid == 0) {
				for (int i = 0; i < kLinesEach; ++i) {
					golden::append("GOLDEN-UPDATED child" + std::to_string(c) + "/line" + std::to_string(i)
					               + std::string(64, 'x'));
				}
				::_exit(0);
			}
			children.push_back(pid);
		}
		int clean = 0;
		for (const pid_t pid : children) {
			int status = 0;
			::waitpid(pid, &status, 0);
			clean += (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 1 : 0;
		}
		expect(clean).to_equal(kChildren);
		std::vector<std::string> want;
		for (int c = 0; c < kChildren; ++c) {
			for (int i = 0; i < kLinesEach; ++i) {
				want.push_back("GOLDEN-UPDATED child" + std::to_string(c) + "/line" + std::to_string(i)
				               + std::string(64, 'x'));
			}
		}
		std::vector<std::string> got;
		std::istringstream lines{slurp(dir / "log")};
		for (std::string line; std::getline(lines, line);) {
			got.push_back(line);
		}
		std::ranges::sort(want);
		std::ranges::sort(got);
		expect(got.size()).to_equal(want.size());
		expect(got).to_equal(want);
		fs::remove_all(dir);
	});

	it("hands a record line over with its indentation", _ {
		const fs::path dir = fresh_dir();
		const Switch on{dir / "log"};
		golden::record("tests/unit/dsp/x_records.h", "kRecords", "    0x1ULL, // ONE");
		expect(slurp(dir / "log")).to_equal(std::string{"GOLDEN-RECORD tests/unit/dsp/x_records.h kRecords     0x1ULL, // ONE\n"});
		fs::remove_all(dir);
	});

	it("describes content as golden_update.py does: a SHA-256 prefix and the size, or (absent)", _ {
		expect(golden::describe(std::nullopt)).to_equal(std::string{"(absent)"});
		expect(golden::describe(std::string{})).to_equal(std::string{"sha256=e3b0c44298fc1c14,bytes=0"});
		expect(golden::describe(std::string{"abc"})).to_equal(std::string{"sha256=ba7816bf8f01cfea,bytes=3"});
		expect(golden::describe(std::string{"P1\n"})).to_equal(std::string{"sha256=27ae4c8af1987faf,bytes=3"});
		// Two blocks of padding: 56 bytes leave no room for the length in the first.
		expect(golden::describe(std::string(56, 'a'))).to_equal(std::string{"sha256=b35439a4ac6f0948,bytes=56"});
		expect(golden::describe(std::string(1000, '\xff'))).to_equal(std::string{"sha256=b4f73dff046400b7,bytes=1000"});
	});
});
// clang-format on

int golden_update_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(golden_update).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
