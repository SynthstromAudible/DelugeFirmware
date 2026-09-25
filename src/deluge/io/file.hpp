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

/// @brief How an attempt to open a directory through the async channel ended.
enum class DirOpen : uint8_t {
	Pending, ///< Asked for; ask again on a later tick.
	Ready,   ///< The directory is adopted and ready to drain.
	Failed,  ///< TERMINAL -- the open did not happen, and must NOT be asked for again.
};

/// @brief Collect the directory opened for @p path, requesting it if nothing is known yet.
///
/// The interaction tier's way to open a folder: the open crosses to the storage tier, and the walk
/// afterwards costs no card I/O at all -- `file_directory.h` takes a snapshot and `read()` walks it
/// in memory -- so a caller collects on one tick and drains to completion in that same tick.
///
/// @param path      The folder to open. Matched EXACTLY against the request, so it must be the same
///                  string each tick.
/// @param out       Receives the adopted Directory on @ref DirOpen::Ready, untouched otherwise.
///                  `std::optional` because Directory is move-only with no default constructor.
/// @param outStatus Written only on @ref DirOpen::Failed.
///
/// @warning `UNKNOWN` is folded into @ref DirOpen::Pending AND re-requests, which is that status's
///          documented meaning. `FAILED` is NOT re-requested: a folder that cannot be opened would
///          otherwise be re-opened on every tick for as long as the caller lives. A caller that is
///          trying to STOP rather than to open must therefore not use this -- it would keep asking.
/// @warning On Ready the handle is ADOPTED, so the ordinary destructor closes it. There are only
///          eight directory handles; collecting one and dropping the number burns it permanently.
[[nodiscard]] DirOpen collectOrRequestDir(const char* path, std::optional<Directory>* out, Status* outStatus);

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
