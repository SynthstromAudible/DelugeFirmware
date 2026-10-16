#include "cppspec.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#ifndef DELUGE_REPO_ROOT
#error "DELUGE_REPO_ROOT must be defined by CMake"
#endif

// Exercises scripts/ui_snapshot_diff.py and scripts/pbm2png.py (spec G §3, §7) end to end via
// `std::system`, against fixture directories this spec writes into a temp dir it owns. These are
// Python tools with no C++ surface of their own; running them as subprocesses is the only way to
// spec their actual behaviour (exit codes, stdout, files written) rather than re-implementing
// them in C++ to test in-process.
namespace fs = std::filesystem;

namespace {

/// @brief True once, cached: is `python3` on PATH? If not, every `it` below short-circuits to a
/// trivially-true assertion and prints why -- cppspec has no "skip/pending" primitive, so this is
/// the standard workaround: still "pass", but say clearly that nothing was actually exercised.
bool python3_available() {
	static const bool available = (std::system("python3 --version > /dev/null 2>&1") == 0);
	static bool printed = false;
	if (!available && !printed) {
		printed = true;
		std::fprintf(stderr, "SKIP harness/snapshot_tools_spec: python3 not found on PATH\n");
	}
	return available;
}

/// @brief Run a shell command, returning its process exit code (or -1 if it could not be spawned
/// or did not exit normally).
int run(const std::string& cmd) {
	int status = std::system(cmd.c_str());
	if (status == -1) {
		return -1;
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::string repo_script(const char* name) {
	return std::string(DELUGE_REPO_ROOT) + "/scripts/" + name;
}

/// @brief A fresh temp directory under the system temp dir, removed by the caller when done.
fs::path make_tmp_dir() {
	std::string tmpl = (fs::temp_directory_path() / "snapshot_tools_spec_XXXXXX").string();
	std::vector<char> buf(tmpl.begin(), tmpl.end());
	buf.push_back('\0');
	char* dir = ::mkdtemp(buf.data());
	return dir != nullptr ? fs::path(dir) : fs::path();
}

void write_file(const fs::path& path, std::string_view contents) {
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary) << contents;
}

std::string read_file(const fs::path& path) {
	std::ifstream in(path, std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

} // namespace

// clang-format off
describe snapshot_tools("harness::snapshot_tools (ui_snapshot_diff.py, pbm2png.py)", $ {
	it("matches identical .state trees and exits 0", _ {
		if (!python3_available()) { expect(true).to_be_true(); return; }
		fs::path tmp = make_tmp_dir();
		expect(!tmp.empty()).to_be_true(); // M-5: mkdtemp failed -- everything below would run relative to CWD
		write_file(tmp / "golden" / "a.state", "scene X\n");
		write_file(tmp / "got" / "a.state", "scene X\n");
		int rc = run("python3 " + repo_script("ui_snapshot_diff.py") + " '" + (tmp / "got").string() + "' '" +
		             (tmp / "golden").string() + "' > /dev/null 2>&1");
		expect(rc).to_equal(0);
		fs::remove_all(tmp);
	});

	it("reports a mismatch and exits 1 when got diverges from golden", _ {
		if (!python3_available()) { expect(true).to_be_true(); return; }
		fs::path tmp = make_tmp_dir();
		expect(!tmp.empty()).to_be_true(); // M-5: mkdtemp failed -- everything below would run relative to CWD
		write_file(tmp / "golden" / "a.state", "scene X\n");
		write_file(tmp / "got" / "a.state", "scene Y\n");
		int rc = run("python3 " + repo_script("ui_snapshot_diff.py") + " '" + (tmp / "got").string() + "' '" +
		             (tmp / "golden").string() + "' > /dev/null 2>&1");
		expect(rc).to_equal(1);
		fs::remove_all(tmp);
	});

	it("re-records under GFX_GOLDEN_UPDATE=1, exits 0, and updates the golden file", _ {
		if (!python3_available()) { expect(true).to_be_true(); return; }
		fs::path tmp = make_tmp_dir();
		expect(!tmp.empty()).to_be_true(); // M-5: mkdtemp failed -- everything below would run relative to CWD
		write_file(tmp / "golden" / "a.state", "scene X\n");
		write_file(tmp / "got" / "a.state", "scene Y\n");
		int rc = run("GFX_GOLDEN_UPDATE=1 python3 " + repo_script("ui_snapshot_diff.py") + " '" +
		             (tmp / "got").string() + "' '" + (tmp / "golden").string() + "' > /dev/null 2>&1");
		expect(rc).to_equal(0);
		expect(read_file(tmp / "golden" / "a.state")).to_equal("scene Y\n");
		fs::remove_all(tmp);
	});

	it("refuses to re-record from an empty got/ tree, exits 2, and leaves the golden untouched (I-2)", _ {
		if (!python3_available()) { expect(true).to_be_true(); return; }
		fs::path tmp = make_tmp_dir();
		expect(!tmp.empty()).to_be_true();
		// `got/` exists but holds no .pbm/.state -- the "script forgot its snapshot steps" mistake
		// I-2 exists to catch. `golden/` starts with a real recorded file, which must survive.
		fs::create_directories(tmp / "got");
		write_file(tmp / "golden" / "a.state", "scene X\n");
		int rc = run("GFX_GOLDEN_UPDATE=1 python3 " + repo_script("ui_snapshot_diff.py") + " '" +
		             (tmp / "got").string() + "' '" + (tmp / "golden").string() + "' > /dev/null 2>&1");
		expect(rc).to_equal(2);
		expect(fs::exists(tmp / "golden" / "a.state")).to_be_true();
		expect(read_file(tmp / "golden" / "a.state")).to_equal("scene X\n");
		fs::remove_all(tmp);
	});

	it("flags an extra got file with no golden counterpart as a mismatch", _ {
		if (!python3_available()) { expect(true).to_be_true(); return; }
		fs::path tmp = make_tmp_dir();
		expect(!tmp.empty()).to_be_true(); // M-5: mkdtemp failed -- everything below would run relative to CWD
		write_file(tmp / "golden" / "a.state", "scene X\n");
		write_file(tmp / "got" / "a.state", "scene X\n");
		write_file(tmp / "got" / "b.state", "scene Z\n");
		int rc = run("python3 " + repo_script("ui_snapshot_diff.py") + " '" + (tmp / "got").string() + "' '" +
		             (tmp / "golden").string() + "' > /dev/null 2>&1");
		expect(rc).to_equal(1);
		fs::remove_all(tmp);
	});

	it("converts a 4x2 P1 PBM to a PNG whose signature bytes are present", _ {
		if (!python3_available()) { expect(true).to_be_true(); return; }
		fs::path tmp = make_tmp_dir();
		expect(!tmp.empty()).to_be_true(); // M-5: mkdtemp failed -- everything below would run relative to CWD
		fs::create_directories(tmp);
		write_file(tmp / "in.pbm", "P1\n4 2\n1 0 1 0\n0 1 0 1\n");
		int rc = run("python3 " + repo_script("pbm2png.py") + " '" + (tmp / "in.pbm").string() + "' -o '" +
		             (tmp / "out.png").string() + "' > /dev/null 2>&1");
		expect(rc).to_equal(0);
		std::string png = read_file(tmp / "out.png");
		std::string sig = std::string("\x89PNG\r\n\x1a\n", 8);
		expect(png.size() > sig.size()).to_be_true();
		expect(png.substr(0, sig.size())).to_equal(sig);
		fs::remove_all(tmp);
	});

	it("rejects a P4 (binary) PBM with a non-zero exit and no output file", _ {
		if (!python3_available()) { expect(true).to_be_true(); return; }
		fs::path tmp = make_tmp_dir();
		expect(!tmp.empty()).to_be_true(); // M-5: mkdtemp failed -- everything below would run relative to CWD
		fs::create_directories(tmp);
		write_file(tmp / "bad.pbm", std::string("P4\n4 2\n") + std::string(1, '\x00'));
		int rc = run("python3 " + repo_script("pbm2png.py") + " '" + (tmp / "bad.pbm").string() + "' -o '" +
		             (tmp / "out.png").string() + "' > /dev/null 2>&1");
		expect(rc).to_equal(1);
		expect(fs::exists(tmp / "out.png")).to_be_false();
		fs::remove_all(tmp);
	});
});
// clang-format on
int harness_snapshot_tools_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(snapshot_tools).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
