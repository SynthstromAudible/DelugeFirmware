#include "io/stream.hpp"

#include "io/c_path.h"
#include "libdeluge/file_stream.h"

#include <cstdint>

namespace deluge::io {

std::expected<Stream, Status> Stream::open(std::string_view path, DelugeStreamMode mode) {
	// One persistent write context for the whole recording -- every subsequent
	// write_at/read_at_via/close on this Stream reuses it; no per-call reopen.
	const auto cpath = CPath::from(path);
	if (!cpath) {
		return std::unexpected(Status::PARAM);
	}
	DelugeHandle handle = DELUGE_HANDLE_NONE;
	const DelugeStatus status = deluge_efatfs_stream_open(cpath->c_str(), static_cast<uint8_t>(mode), &handle);
	if (status != DELUGE_OK) {
		return std::unexpected(to_status(status));
	}
	return Stream(handle);
}

std::expected<uint32_t, Status> Stream::write_at(uint32_t byte_offset, std::span<const std::byte> buffer) {
	uint32_t out_written = 0;
	// No-flush write -- see stream_io.h's `deluge_efatfs_stream_write_at` doc. The persisted
	// size/mtime edit is flushed to disk on `close()` (or an explicit mid-recording flush, not
	// exercised by this port).
	const DelugeStatus status = deluge_efatfs_stream_write_at(handle_, byte_offset, buffer.data(),
	                                                          static_cast<uint32_t>(buffer.size()), &out_written);
	if (status != DELUGE_OK) {
		return std::unexpected(to_status(status));
	}
	return out_written;
}

bool Stream::request_write_at(uint32_t byte_offset, std::span<const std::byte> buffer) {
	if (handle_ == DELUGE_HANDLE_NONE) {
		return false;
	}
	return deluge_file_stream_request_write_at(handle_, byte_offset, reinterpret_cast<const uint8_t*>(buffer.data()),
	                                           static_cast<uint32_t>(buffer.size()));
}

DelugeChannelStatus Stream::collect_write_at(uint32_t* out_written) {
	if (handle_ == DELUGE_HANDLE_NONE) {
		// UNKNOWN, not PENDING: nothing is in flight and nothing ever will be on this stream.
		return DELUGE_CHANNEL_UNKNOWN;
	}
	return deluge_file_stream_collect_write_at(handle_, out_written);
}

bool Stream::request_open(std::string_view path, DelugeStreamMode mode) {
	const auto cpath = CPath::from(path);
	if (!cpath) {
		return false;
	}
	return deluge_file_stream_request_open(cpath->c_str(), static_cast<uint8_t>(mode));
}

DelugeChannelStatus Stream::collect_open(std::string_view path, std::optional<Stream>* out, DelugeStatus* error) {
	const auto cpath = CPath::from(path);
	if (!cpath) {
		// A path this long was never accepted by request_open, so nothing is in flight for it:
		// the same answer collect_open gives for any path with no matching request.
		return DELUGE_CHANNEL_UNKNOWN;
	}
	DelugeHandle handle = DELUGE_HANDLE_NONE;
	const DelugeChannelStatus status = deluge_file_stream_collect_open(cpath->c_str(), &handle, error);
	if (status == DELUGE_CHANNEL_READY && out != nullptr) {
		out->emplace(Stream(handle));
	}
	return status;
}

bool Stream::request_read_at(uint32_t byte_offset, std::span<std::byte> dst) {
	if (handle_ == DELUGE_HANDLE_NONE) {
		return false;
	}
	return deluge_file_stream_request_read_at(handle_, byte_offset, reinterpret_cast<uint8_t*>(dst.data()),
	                                          static_cast<uint32_t>(dst.size()));
}

DelugeChannelStatus Stream::collect_read_at(uint32_t* out_read) {
	if (handle_ == DELUGE_HANDLE_NONE) {
		return DELUGE_CHANNEL_UNKNOWN;
	}
	return deluge_file_stream_collect_read_at(handle_, out_read);
}

bool Stream::request_truncate(uint32_t new_size) {
	if (handle_ == DELUGE_HANDLE_NONE) {
		return false;
	}
	return deluge_file_stream_request_truncate(handle_, new_size);
}

DelugeChannelStatus Stream::collect_truncate() {
	if (handle_ == DELUGE_HANDLE_NONE) {
		return DELUGE_CHANNEL_UNKNOWN;
	}
	return deluge_file_stream_collect_truncate(handle_);
}

bool Stream::request_close() {
	if (handle_ == DELUGE_HANDLE_NONE) {
		return false;
	}
	return deluge_file_stream_request_close(handle_);
}

DelugeChannelStatus Stream::collect_close() {
	if (handle_ == DELUGE_HANDLE_NONE) {
		return DELUGE_CHANNEL_UNKNOWN;
	}
	const DelugeChannelStatus status = deluge_file_stream_collect_close(handle_);
	if (status == DELUGE_CHANNEL_READY || status == DELUGE_CHANNEL_FAILED) {
		// Disengage on ANY terminal status, exactly as the synchronous close() does on error: the
		// handle is gone either way, and keeping it would let the destructor close it twice.
		handle_ = DELUGE_HANDLE_NONE;
	}
	return status;
}

std::expected<uint32_t, Status> Stream::read_at_via(uint32_t byte_offset, std::span<std::byte> dst) {
	uint32_t out_read = 0;
	const DelugeStatus status = deluge_efatfs_stream_read_at_via(handle_, byte_offset, dst.data(),
	                                                             static_cast<uint32_t>(dst.size()), &out_read);
	if (status != DELUGE_OK) {
		return std::unexpected(to_status(status));
	}
	return out_read;
}

std::expected<void, Status> Stream::truncate(uint32_t new_size) {
	const DelugeStatus status = deluge_efatfs_stream_truncate(handle_, new_size);
	if (status != DELUGE_OK) {
		return std::unexpected(to_status(status));
	}
	return {};
}

std::expected<uint32_t, Status> Stream::size() {
	uint32_t out_size = 0;
	const DelugeStatus status = deluge_efatfs_stream_size(handle_, &out_size);
	if (status != DELUGE_OK) {
		return std::unexpected(to_status(status));
	}
	return out_size;
}

std::expected<void, Status> Stream::close() {
	// NOTE: an in-flight async write must already be finished by the time anyone closes this stream.
	// The channel holds a raw pointer into the caller's buffer, and every caller frees that buffer
	// after close() returns. The obligation sits with the state machine that owns the buffer: see
	// `SampleRecorder::cardRoutine`, which never reaches a terminal status with a write outstanding.
	const DelugeStatus status = deluge_efatfs_stream_close(handle_);
	handle_ = DELUGE_HANDLE_NONE; // matters even on error: don't let the destructor double-close
	if (status != DELUGE_OK) {
		return std::unexpected(to_status(status));
	}
	return {};
}

} // namespace deluge::io
