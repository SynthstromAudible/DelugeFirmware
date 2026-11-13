/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <array>
#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

/// @file
/// @brief Re-recording a baseline from C++: the switch, and the lines each change leaves in the update log.
///
/// `./dbt test --update <name>` runs a test with `DELUGE_GOLDEN_UPDATE=1` and `DELUGE_GOLDEN_UPDATE_LOG` naming a
/// file (scripts/tasks/golden_update.py documents the protocol, and writes the same lines from Python). A spec that
/// owns a baseline file writes it with write_if_changed() and remove_if_present(), which log
/// `GOLDEN-UPDATED <path> <old> -> <new>`; a spec or scenario that owns a record header's lines hands each to record(),
/// and the runner rewrites the header's region from them (scripts/tasks/golden_records.py). The log is how a change
/// reaches the runner: ctest keeps a passing test's output to itself.
namespace deluge::test::golden {

inline constexpr const char* kSwitch = "DELUGE_GOLDEN_UPDATE";
inline constexpr const char* kLog = "DELUGE_GOLDEN_UPDATE_LOG";

/// @brief Prints @p what and the current errno's text, and exits with status 2: a baseline that could not be written,
/// or a change that could not be logged, must fail the run so the runner puts every baseline back.
[[noreturn]] inline void fail(const std::string& what) {
	std::fprintf(stderr, "golden_update: %s: %s\n", what.c_str(), std::strerror(errno));
	std::exit(2);
}

/// @brief Says how a baseline is re-recorded and exits with status 2: what golden_update.py prints when the switch is
/// set without the log.
[[noreturn]] inline void refuse() {
	std::fputs("DELUGE_GOLDEN_UPDATE=1 is set by ./dbt test --update <name>, which builds first and collects what "
	           "changed: run that\n",
	           stderr);
	std::exit(2);
}

/// @brief Whether this run re-records: `DELUGE_GOLDEN_UPDATE` is exactly "1" (not merely set: "0" means no).
/// Exits with status 2 when it is set without `DELUGE_GOLDEN_UPDATE_LOG`: only the runner sets the log, after
/// building, so a baseline is never recorded from a stale binary or without its evidence.
[[nodiscard]] inline bool updating() {
	const char* on = std::getenv(kSwitch);
	if (on == nullptr || std::string_view{on} != "1") {
		return false;
	}
	const char* log = std::getenv(kLog);
	if (log == nullptr || *log == '\0') {
		refuse();
	}
	return true;
}

/// @brief Appends @p line and a newline to the update log in one write(2) to an O_APPEND descriptor, under the
/// exclusive flock golden_update.py's writers take: lines from specs running at once never interleave.
/// Exits with status 2 when the log is unset or cannot be written.
inline void append(std::string_view line) {
	const char* path = std::getenv(kLog);
	if (path == nullptr || *path == '\0') {
		refuse();
	}
	const int fd = ::open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
	if (fd < 0) {
		fail(std::string{"opening the update log "} + path);
	}
	::flock(fd, LOCK_EX);
	std::string text{line};
	text.push_back('\n');
	const ssize_t written = ::write(fd, text.data(), text.size());
	if (written != static_cast<ssize_t>(text.size())) {
		fail(std::string{"appending to the update log "} + path);
	}
	::close(fd);
}

namespace detail {

/// @brief The SHA-256 digest of @p bytes (FIPS 180-4).
[[nodiscard]] inline std::array<uint8_t, 32> sha256(std::string_view bytes) {
	static constexpr std::array<uint32_t, 64> k{
	    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
	};
	std::array<uint32_t, 8> h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
	                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

	// The message, a 1 bit, zeros, and its length in bits as a big-endian 64-bit word, to a multiple of 64 bytes.
	std::string padded{bytes};
	padded.push_back(static_cast<char>(0x80));
	while (padded.size() % 64 != 56) {
		padded.push_back('\0');
	}
	const uint64_t bits = static_cast<uint64_t>(bytes.size()) * 8;
	for (int shift = 56; shift >= 0; shift -= 8) {
		padded.push_back(static_cast<char>((bits >> shift) & 0xff));
	}

	for (size_t block = 0; block < padded.size(); block += 64) {
		std::array<uint32_t, 64> w{};
		for (size_t i = 0; i < 16; ++i) {
			const auto byte = [&](size_t n) { return static_cast<uint32_t>(static_cast<uint8_t>(padded[block + n])); };
			w[i] = (byte(4 * i) << 24) | (byte(4 * i + 1) << 16) | (byte(4 * i + 2) << 8) | byte(4 * i + 3);
		}
		for (size_t i = 16; i < 64; ++i) {
			const uint32_t s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			const uint32_t s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}
		auto [a, b, c, d, e, f, g, hh] = h;
		for (size_t i = 0; i < 64; ++i) {
			const uint32_t t1 =
			    hh + (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
			const uint32_t t2 = (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
			hh = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}
		const std::array<uint32_t, 8> round{a, b, c, d, e, f, g, hh};
		for (size_t i = 0; i < 8; ++i) {
			h[i] += round[i];
		}
	}

	std::array<uint8_t, 32> digest{};
	for (size_t i = 0; i < 32; ++i) {
		digest[i] = static_cast<uint8_t>(h[i / 4] >> (24 - 8 * (i % 4)));
	}
	return digest;
}

} // namespace detail

/// @brief One word for a file's content, as golden_update.py's describe_bytes() gives it: `(absent)`, or
/// `sha256=<the digest's first 16 hex digits>,bytes=<size>`.
[[nodiscard]] inline std::string describe(const std::optional<std::string>& bytes) {
	if (!bytes) {
		return "(absent)";
	}
	const std::array<uint8_t, 32> digest = detail::sha256(*bytes);
	std::string word{"sha256="};
	for (size_t i = 0; i < 8; ++i) {
		static constexpr char kHex[] = "0123456789abcdef";
		word.push_back(kHex[digest[i] >> 4]);
		word.push_back(kHex[digest[i] & 0xf]);
	}
	return word + ",bytes=" + std::to_string(bytes->size());
}

/// @brief What @p path holds, or nothing when it is not a regular file that can be read.
[[nodiscard]] inline std::optional<std::string> read(const std::filesystem::path& path) {
	std::error_code ec;
	if (!std::filesystem::is_regular_file(path, ec)) {
		return std::nullopt;
	}
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return std::nullopt;
	}
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// @brief The name a log line gives @p path: absolute, symlinks resolved, as golden_update.py's callers name theirs
/// (Path.resolve()), so the runner reads every item relative to the repository root.
[[nodiscard]] inline std::string item(const std::filesystem::path& path) {
	std::error_code ec;
	const std::filesystem::path resolved = std::filesystem::weakly_canonical(std::filesystem::absolute(path), ec);
	return (ec ? std::filesystem::absolute(path).lexically_normal() : resolved).string();
}

/// @brief Logs `GOLDEN-UPDATED <item> <old> -> <new>`; nothing when @p old and @p now describe alike.
inline void report(const std::string& name, const std::optional<std::string>& old,
                   const std::optional<std::string>& now) {
	const std::string from = describe(old);
	const std::string to = describe(now);
	if (from != to) {
		append("GOLDEN-UPDATED " + name + " " + from + " -> " + to);
	}
}

/// @brief Replaces @p path with @p bytes in one rename, keeping its mode (0644 for a new file): a reader, or a run cut
/// short, sees the old file or the new one, never a torn one. The temp file is written beside @p path, flushed to
/// disk, then renamed over it. Exits with status 2 when any step fails.
inline void atomic_write(const std::filesystem::path& path, std::string_view bytes) {
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path parent = path.parent_path().empty() ? fs::path{"."} : path.parent_path();
	fs::create_directories(parent, ec);
	struct stat st{};
	const mode_t mode = ::stat(path.c_str(), &st) == 0 ? (st.st_mode & 07777) : 0644;

	std::string tmpl = (parent / ("." + path.filename().string() + ".XXXXXX")).string();
	const int fd = ::mkstemp(tmpl.data());
	if (fd < 0) {
		fail("creating a temp file beside " + path.string());
	}
	size_t done = 0;
	while (done < bytes.size()) {
		const ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			::unlink(tmpl.c_str());
			fail("writing " + tmpl);
		}
		done += static_cast<size_t>(n);
	}
	if (::fsync(fd) != 0 || ::fchmod(fd, mode) != 0 || ::close(fd) != 0) {
		::unlink(tmpl.c_str());
		fail("flushing " + tmpl);
	}
	if (::rename(tmpl.c_str(), path.c_str()) != 0) {
		::unlink(tmpl.c_str());
		fail("renaming " + tmpl + " over " + path.string());
	}
}

/// @brief Makes @p path hold @p bytes, writing and logging only when it does not already.
/// @return Whether the file changed.
inline bool write_if_changed(const std::filesystem::path& path, std::string_view bytes) {
	const std::optional<std::string> old = read(path);
	if (old && *old == bytes) {
		return false;
	}
	const std::string name = item(path);
	atomic_write(path, bytes);
	report(name, old, std::string{bytes});
	return true;
}

/// @brief Deletes the regular file @p path and logs it, when there is one.
/// @return Whether a file was removed.
inline bool remove_if_present(const std::filesystem::path& path) {
	const std::optional<std::string> old = read(path);
	if (!old) {
		return false;
	}
	const std::string name = item(path);
	if (::unlink(path.c_str()) != 0) {
		fail("removing " + path.string());
	}
	report(name, old, std::nullopt);
	return true;
}

/// @brief Hands one line of a record header's region to the runner: `GOLDEN-RECORD <header> <region> <line>`.
/// @param header The header, relative to the repository root.
/// @param region The name on its `GOLDEN-RECORDS BEGIN`/`END` markers.
/// @param line   The line as the header holds it, indentation included. Every line of the region, in order.
inline void record(std::string_view header, std::string_view region, std::string_view line) {
	std::string text{"GOLDEN-RECORD "};
	text.append(header).append(" ").append(region).append(" ").append(line);
	append(text);
}

} // namespace deluge::test::golden
