#include "io/file.hpp"

#include "libdeluge/file_directory.h"

#include <cstdint>

namespace deluge::io {

namespace {

// The efatfs backend's `u32` handles are boxed into the opaque DelugeFile*/DelugeDir* pointers,
// the same convention the streaming handle uses -- these are never dereferenced, only cast back
// to `u32` before crossing into Rust.
//
// The Rust TaskFileTable/DirHandleTable `insert` hands out the lowest free
// slot starting at 0, so the offset-less cast used to box slot 0 into
// nullptr. File/Directory's RAII guards (`if (handle_) { close(); }`) treat
// null as "no handle", so a slot-0 handle would never get closed -- leaking
// the Rust slot and, for a slot-0 file opened for write, losing unflushed
// data. Reserve 0/null for "empty" by storing `handle + 1` and recovering
// `ptr - 1`, so every real slot (including 0) boxes to a non-null pointer.
DelugeFile* box_file_handle(uint32_t handle) {
	return reinterpret_cast<DelugeFile*>(static_cast<uintptr_t>(handle) + 1);
}
uint32_t unbox_file_handle(DelugeFile* handle) {
	return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(handle) - 1);
}
DelugeDir* box_dir_handle(uint32_t handle) {
	return reinterpret_cast<DelugeDir*>(static_cast<uintptr_t>(handle) + 1);
}
uint32_t unbox_dir_handle(DelugeDir* handle) {
	return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(handle) - 1);
}

// FAT attribute bits (embedded_fatfs::FileAttributes::bits() -- see
// efatfs_core.rs's DirEntryInfo doc).
constexpr uint8_t kFatAttrReadOnly = 0x01;
constexpr uint8_t kFatAttrHidden = 0x02;
constexpr uint8_t kFatAttrSystem = 0x04;
constexpr uint8_t kFatAttrArchive = 0x20;

// Unpacks deluge_efatfs_dir_read's `out_modified` -- the same
// `(dos_date << 16) | dos_time` convention deluge_efatfs_set_time packs and
// efatfs_core.rs's `pack_fat_datetime`/`set_time` document.
DelugeTimestamp unpack_fat_datetime(uint32_t packed) {
	auto dos_date = static_cast<uint16_t>(packed >> 16);
	auto dos_time = static_cast<uint16_t>(packed & 0xFFFFU);
	DelugeTimestamp ts{};
	ts.year = static_cast<uint16_t>(1980 + ((dos_date >> 9) & 0x7F));
	ts.month = static_cast<uint8_t>((dos_date >> 5) & 0x0F);
	ts.day = static_cast<uint8_t>(dos_date & 0x1F);
	ts.hour = static_cast<uint8_t>((dos_time >> 11) & 0x1F);
	ts.minute = static_cast<uint8_t>((dos_time >> 5) & 0x3F);
	ts.second = static_cast<uint8_t>((dos_time & 0x1F) * 2);
	return ts;
}

} // namespace

Status to_status(DelugeStatus status) {
	switch (status) {
	case DELUGE_OK:
		return Status::OK;
	case DELUGE_ERR:
		return Status::ERR;
	case DELUGE_ERR_PARAM:
		return Status::PARAM;
	case DELUGE_ERR_BUSY:
		return Status::BUSY;
	case DELUGE_ERR_TIMEOUT:
		return Status::TIMEOUT;
	case DELUGE_ERR_IO:
		return Status::IO;
	case DELUGE_ERR_NODEV:
		return Status::NODEV;
	case DELUGE_ERR_UNSUPPORTED:
		return Status::UNSUPPORTED;
	case DELUGE_ERR_NOT_FOUND:
		return Status::NOT_FOUND;
	case DELUGE_ERR_EXISTS:
		return Status::EXISTS;
	case DELUGE_ERR_NO_SPACE:
		return Status::NO_SPACE;
	case DELUGE_ERR_NO_FILESYSTEM:
		return Status::NO_FILESYSTEM;
	case DELUGE_ERR_WRITE_PROTECTED:
		return Status::WRITE_PROTECTED;
	case DELUGE_ERR_NO_MEMORY:
		return Status::NO_MEMORY;
	case DELUGE_ERR_NOT_EMPTY:
		return Status::NOT_EMPTY;
	}
	return Status::ERR; // unreachable while the switch above stays exhaustive
}

DelugeStatus to_deluge_status(Status status) {
	switch (status) {
	case Status::OK:
		return DELUGE_OK;
	case Status::ERR:
		return DELUGE_ERR;
	case Status::PARAM:
		return DELUGE_ERR_PARAM;
	case Status::BUSY:
		return DELUGE_ERR_BUSY;
	case Status::TIMEOUT:
		return DELUGE_ERR_TIMEOUT;
	case Status::IO:
		return DELUGE_ERR_IO;
	case Status::NODEV:
		return DELUGE_ERR_NODEV;
	case Status::UNSUPPORTED:
		return DELUGE_ERR_UNSUPPORTED;
	case Status::NOT_FOUND:
		return DELUGE_ERR_NOT_FOUND;
	case Status::EXISTS:
		return DELUGE_ERR_EXISTS;
	case Status::NO_SPACE:
		return DELUGE_ERR_NO_SPACE;
	case Status::NO_FILESYSTEM:
		return DELUGE_ERR_NO_FILESYSTEM;
	case Status::WRITE_PROTECTED:
		return DELUGE_ERR_WRITE_PROTECTED;
	case Status::NO_MEMORY:
		return DELUGE_ERR_NO_MEMORY;
	case Status::NOT_EMPTY:
		return DELUGE_ERR_NOT_EMPTY;
	}
	return DELUGE_ERR; // unreachable while the switch above stays exhaustive
}

std::expected<bool, Status> presence_from_open(const std::expected<File, Status>& opened) {
	if (opened.has_value()) {
		return true;
	}
	if (opened.error() == Status::NOT_FOUND) {
		return false;
	}
	return std::unexpected{opened.error()};
}

std::expected<File, Status> File::open(std::string_view path, DelugeFileOpenMode mode) {
	uint32_t handle = 0;
	DelugeStatus st = deluge_efatfs_file_open(path.data(), static_cast<uint8_t>(mode), &handle);
	if (st != DELUGE_OK) {
		return std::unexpected(to_status(st));
	}
	return File(box_file_handle(handle));
}

std::expected<std::span<std::byte>, Status> File::read(std::span<std::byte> buffer) {
	uint32_t out_read = 0;
	// EOF-honest read (the true short count, never zero-padded) -- the
	// efatfs backend for this port method.
	DelugeStatus st = deluge_efatfs_file_read_exact(unbox_file_handle(handle_), buffer.data(),
	                                                static_cast<uint32_t>(buffer.size()), &out_read);
	if (st != DELUGE_OK) {
		return std::unexpected(to_status(st));
	}
	return buffer.subspan(0, out_read);
}

std::expected<uint32_t, Status> File::write(std::span<const std::byte> buffer) {
	uint32_t out_written = 0;
	DelugeStatus st = deluge_efatfs_file_write(unbox_file_handle(handle_), buffer.data(),
	                                           static_cast<uint32_t>(buffer.size()), &out_written);
	if (st != DELUGE_OK) {
		return std::unexpected(to_status(st));
	}
	return out_written;
}

std::expected<void, Status> File::seek(uint32_t offset) {
	DelugeStatus st = deluge_efatfs_file_seek(unbox_file_handle(handle_), offset);
	if (st != DELUGE_OK) {
		return std::unexpected(to_status(st));
	}
	return {};
}

std::expected<uint32_t, Status> File::size() {
	uint32_t out_size = 0;
	DelugeStatus st = deluge_efatfs_file_size(unbox_file_handle(handle_), &out_size);
	if (st != DELUGE_OK) {
		return std::unexpected(to_status(st));
	}
	return out_size;
}

std::expected<void, Status> File::close() {
	deluge_efatfs_file_close(unbox_file_handle(handle_));
	handle_ = nullptr; // matters even on error: don't let the destructor double-close
	return {};
}

std::expected<Directory, Status> Directory::open(std::string_view path) {
	uint32_t handle = 0;
	DelugeStatus st = deluge_efatfs_dir_open(path.data(), &handle);
	if (st != DELUGE_OK) {
		return std::unexpected(to_status(st));
	}
	return Directory(box_dir_handle(handle));
}

Directory Directory::adopt(uint32_t handle) {
	// Deliberately the same boxing open() applies to the handle it just received, rather than a
	// second path into the private constructor: a handle from the async channel and a handle from a
	// synchronous open are the same efatfs slot index, and boxing them differently would make one of
	// the two unclose-able.
	return Directory(box_dir_handle(handle));
}

std::expected<std::optional<DelugeDirEntry>, Status> Directory::read() {
	DelugeDirEntry entry{};
	bool has_entry = false;
	uint32_t modified = 0;
	uint8_t attrs = 0;
	DelugeStatus st = deluge_efatfs_dir_read(unbox_dir_handle(handle_), entry.name, DELUGE_MAX_FILENAME,
	                                         &entry.is_directory, &entry.size, &modified, &attrs, &has_entry);
	if (st != DELUGE_OK) {
		return std::unexpected(to_status(st));
	}
	if (!has_entry) {
		return std::nullopt;
	}
	entry.modified_time = unpack_fat_datetime(modified);
	entry.is_read_only = (attrs & kFatAttrReadOnly) != 0;
	entry.is_hidden = (attrs & kFatAttrHidden) != 0;
	entry.is_system = (attrs & kFatAttrSystem) != 0;
	entry.is_archive = (attrs & kFatAttrArchive) != 0;
	return entry;
}

std::expected<void, Status> Directory::close() {
	deluge_efatfs_dir_close(unbox_dir_handle(handle_));
	handle_ = nullptr; // matters even on error: don't let the destructor double-close
	return {};
}

DirOpen collectOrRequestDir(const char* path, std::optional<Directory>* out, Status* outStatus) {
	uint32_t handle = 0;
	DelugeStatus st = DELUGE_OK;
	switch (deluge_file_directory_collect(path, &handle, &st)) {
	case DELUGE_FILE_DIRECTORY_READY:
		// emplace, not assign: Directory is move-only with no default constructor, so there is no
		// empty slot to assign into. Adopting here is what puts the handle back under RAII.
		out->emplace(Directory::adopt(handle));
		return DirOpen::Ready;

	case DELUGE_FILE_DIRECTORY_UNKNOWN:
		// "Request it (again)" -- the documented meaning of UNKNOWN, and where the FIRST request is
		// issued, which is why no separate "have I asked yet" flag is needed.
		deluge_file_directory_request(path, /*create_if_missing=*/false);
		return DirOpen::Pending;

	case DELUGE_FILE_DIRECTORY_PENDING:
		return DirOpen::Pending;

	case DELUGE_FILE_DIRECTORY_FAILED:
	default:
		// TERMINAL, and deliberately NOT re-requested: a folder that cannot be opened would be
		// re-opened on every tick, forever, for as long as the caller sat on it.
		*outStatus = to_status(st);
		return DirOpen::Failed;
	}
}

std::expected<bool, Status> presence_from_open(const std::expected<Directory, Status>& opened) {
	if (opened.has_value()) {
		return true;
	}
	if (opened.error() == Status::NOT_FOUND) {
		return false;
	}
	return std::unexpected{opened.error()};
}

std::expected<void, Status> mkdir(std::string_view path) {
	DelugeStatus st = deluge_efatfs_mkdir(path.data());
	if (st != DELUGE_OK) {
		return std::unexpected(to_status(st));
	}
	return {};
}

std::expected<void, Status> unlink(std::string_view path) {
	DelugeStatus st = deluge_efatfs_unlink(path.data());
	if (st != DELUGE_OK) {
		return std::unexpected(to_status(st));
	}
	return {};
}

std::expected<void, Status> rename(std::string_view old_path, std::string_view new_path) {
	DelugeStatus st = deluge_efatfs_rename(old_path.data(), new_path.data());
	if (st != DELUGE_OK) {
		return std::unexpected(to_status(st));
	}
	return {};
}

std::expected<void, Status> set_time(std::string_view path, DelugeTimestamp timestamp) {
	DelugeStatus st = deluge_efatfs_set_time(path.data(), timestamp.year, timestamp.month, timestamp.day,
	                                         timestamp.hour, timestamp.minute, timestamp.second);
	if (st != DELUGE_OK) {
		return std::unexpected(to_status(st));
	}
	return {};
}

} // namespace deluge::io
