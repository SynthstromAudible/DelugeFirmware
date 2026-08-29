#pragma once

#include "libdeluge/file_io.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace deluge::io {

/// A real enum class over DelugeStatus's C enum, wrapping the C error code
/// as a type-safe C++ one.
enum class Status {
	OK,
	ERR,
	PARAM,
	BUSY,
	TIMEOUT,
	IO,
	NODEV,
	UNSUPPORTED,
	NOT_FOUND,
	EXISTS,
	NO_SPACE,
	NO_FILESYSTEM,
	WRITE_PROTECTED,
	NO_MEMORY,
	NOT_EMPTY,
};

Status to_status(DelugeStatus status);
DelugeStatus to_deluge_status(Status status);

class File {
public:
	File(File&) = delete;
	File(File&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
	File& operator=(File&) = delete;
	File& operator=(File&& other) noexcept {
		if (this != &other) {
			if (handle_) {
				// Route through close() (not deluge_efatfs_file_close directly) so
				// the unbox + "handle_ = nullptr" bookkeeping lives in exactly one
				// place -- see file.cpp's close().
				(void)close();
			}
			handle_ = other.handle_;
			other.handle_ = nullptr;
		}
		return *this;
	}
	~File() {
		if (handle_) {
			(void)close();
		}
	}

	[[nodiscard]] static std::expected<File, Status> open(std::string_view path, DelugeFileOpenMode mode);
	std::expected<std::span<std::byte>, Status> read(std::span<std::byte> buffer);
	std::expected<uint32_t, Status> write(std::span<const std::byte> buffer);
	std::expected<void, Status> seek(uint32_t offset);
	std::expected<uint32_t, Status> size();
	std::expected<void, Status> close();

private:
	explicit File(DelugeFile* handle) : handle_(handle) {}
	DelugeFile* handle_ = nullptr;
};

/// @brief Collapse an open attempt into an existence answer.
/// @param opened The result of `File::open()` (only its success/error is read; the File is untouched).
/// @return `true` present · `false` **known absent** (`Status::NOT_FOUND`) · an error when existence
///         could not be determined (e.g. `Status::BUSY` when the filesystem refuses off-owner callers).
/// @note Callers must never treat the error case as absence.
std::expected<bool, Status> presence_from_open(const std::expected<File, Status>& opened);

class Directory {
public:
	Directory(Directory&) = delete;
	Directory(Directory&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
	Directory& operator=(Directory&) = delete;
	Directory& operator=(Directory&& other) noexcept {
		if (this != &other) {
			if (handle_) {
				// Route through close() -- see File's move-assignment for why.
				(void)close();
			}
			handle_ = other.handle_;
			other.handle_ = nullptr;
		}
		return *this;
	}
	~Directory() {
		if (handle_) {
			(void)close();
		}
	}

	[[nodiscard]] static std::expected<Directory, Status> open(std::string_view path);

	/// @brief Adopt a directory handle opened elsewhere — the async directory channel's collect path.
	///
	/// `deluge_file_directory_collect` hands back a raw handle on READY, and there are only eight of
	/// them; this is what puts one straight back under RAII so the ordinary destructor closes it.
	/// The returned Directory owns the handle exactly as one from open() does.
	/// @param handle An open efatfs directory handle, as written to `deluge_file_directory_collect`'s
	///        `out_handle`. Must not be closed by the caller as well.
	/// @return A Directory owning `handle`.
	[[nodiscard]] static Directory adopt(uint32_t handle);

	std::expected<std::optional<DelugeDirEntry>, Status> read();
	std::expected<void, Status> close();

private:
	explicit Directory(DelugeDir* handle) : handle_(handle) {}
	DelugeDir* handle_ = nullptr;
};

/// @brief Collapse a directory-open attempt into an existence answer.
/// @param opened The result of `Directory::open()` (only its success/error is read; the Directory is
///        untouched).
/// @return `true` present · `false` **known absent** (`Status::NOT_FOUND`) · an error when existence
///         could not be determined (e.g. `Status::BUSY` when the filesystem refuses off-owner callers).
/// @note Callers must never treat the error case as absence.
std::expected<bool, Status> presence_from_open(const std::expected<Directory, Status>& opened);

std::expected<void, Status> set_time(std::string_view path, DelugeTimestamp timestamp);

std::expected<void, Status> mkdir(std::string_view path);
std::expected<void, Status> unlink(std::string_view path);
std::expected<void, Status> rename(std::string_view old_path, std::string_view new_path);

} // namespace deluge::io
