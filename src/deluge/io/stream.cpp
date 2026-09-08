#include "io/stream.hpp"

#include "libdeluge/file_write.h"
#include "libdeluge/storage_wait.h"

#include <cstdint>

namespace deluge::io {

namespace {

// Mirrors file.cpp's box_file_handle boxing (see its doc for the full rationale) -- the efatfs
// backend's persistent stream-write `u32` handle is boxed into the opaque `DelugeStream*`,
// `handle + 1` so slot 0 doesn't box to null (Stream's RAII `close()` guards on
// `handle_ != nullptr`).
DelugeStream* box_stream_handle(uint32_t handle) {
	return reinterpret_cast<DelugeStream*>(static_cast<uintptr_t>(handle) + 1);
}
uint32_t unbox_stream_handle(DelugeStream* handle) {
	return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(handle) - 1);
}

/// The handle `Stream::close()` is currently draining. `RunCondition` (libdeluge/storage_wait.h)
/// takes no context pointer, so the predicate reads it from here. Only ever touched between the two
/// statements that set and clear it, on the storage owner.
uint32_t g_draining_handle = 0;

/// Bound on that drain. Generous, because exceeding it means losing a cluster the channel had
/// accepted; finite, because a driver that has stopped answering must not wedge teardown.
constexpr double kDrainTimeoutSeconds = 5.0;

} // namespace

std::expected<Stream, Status> Stream::open(std::string_view path, DelugeStreamMode mode) {
	// One persistent write context for the whole recording -- every subsequent
	// write_at/read_at_via/close on this Stream reuses it; no per-call reopen.
	uint32_t handle = 0;
	if (!deluge_efatfs_stream_open(path.data(), static_cast<uint8_t>(mode), &handle)) {
		return std::unexpected(Status::ERR);
	}
	return Stream(box_stream_handle(handle));
}

std::expected<uint32_t, Status> Stream::write_at(uint32_t byte_offset, std::span<const std::byte> buffer) {
	uint32_t out_written = 0;
	// No-flush write -- see stream_io.h's `deluge_efatfs_stream_write_at` doc. The persisted
	// size/mtime edit is flushed to disk on `close()` (or an explicit mid-recording flush, not
	// exercised by this port).
	if (!deluge_efatfs_stream_write_at(unbox_stream_handle(handle_), byte_offset, buffer.data(),
	                                   static_cast<uint32_t>(buffer.size()), &out_written)) {
		return std::unexpected(Status::ERR);
	}
	return out_written;
}

bool Stream::request_write_at(uint32_t byte_offset, std::span<const std::byte> buffer) {
	if (handle_ == nullptr) {
		return false;
	}
	return deluge_file_write_request(unbox_stream_handle(handle_), byte_offset,
	                                 reinterpret_cast<const uint8_t*>(buffer.data()),
	                                 static_cast<uint32_t>(buffer.size()));
}

uint8_t Stream::collect_write_at(uint32_t* out_written) {
	if (handle_ == nullptr) {
		// UNKNOWN, not PENDING: nothing is in flight and nothing ever will be on this stream.
		return DELUGE_FILE_WRITE_UNKNOWN;
	}
	return deluge_file_write_collect(unbox_stream_handle(handle_), out_written);
}

std::expected<uint32_t, Status> Stream::read_at_via(uint32_t byte_offset, std::span<std::byte> dst) {
	uint32_t out_read = 0;
	if (!deluge_efatfs_stream_read_at_via(unbox_stream_handle(handle_), byte_offset, dst.data(),
	                                      static_cast<uint32_t>(dst.size()), &out_read)) {
		return std::unexpected(Status::ERR);
	}
	return out_read;
}

std::expected<void, Status> Stream::truncate(uint32_t new_size) {
	if (!deluge_efatfs_stream_truncate(unbox_stream_handle(handle_), new_size)) {
		return std::unexpected(Status::ERR);
	}
	return {};
}

std::expected<uint32_t, Status> Stream::size() {
	uint32_t out_size = 0;
	if (!deluge_efatfs_stream_size(unbox_stream_handle(handle_), &out_size)) {
		return std::unexpected(Status::ERR);
	}
	return out_size;
}

std::expected<uint32_t, Status> Stream::sector_of(uint32_t /*cluster_index*/) {
	// The efatfs backend has no `sector_of` accessor: `SampleRecorder::writeCluster` resolves the
	// write-side "most recently written cluster" a different way, and this function's only
	// remaining caller (AudioFileManager's read-mode cold-path "did this file move on the
	// reinserted card" identity check, audio_file_manager.cpp) opens fresh, for reading, with
	// nothing written -- there is nothing such an accessor could resolve for it anyway (it could
	// only ever answer for a context's just-completed write). Fail explicitly rather than
	// resolving a sector: `handle_` on an efatfs-opened Stream is a boxed opaque `u32`, not a real
	// `DelugeStream*`.
	return std::unexpected(Status::UNSUPPORTED);
}

std::expected<void, Status> Stream::close() {
	// Drain any in-flight async write BEFORE the handle goes away. The channel holds a raw pointer
	// into the caller's buffer, and every caller frees that buffer after close() returns --
	// `SampleRecorder::releaseCaptureBuffers()` most importantly. Yielding here is legal: every
	// caller of close() is on the storage owner (finalize, alterFile, truncate, the abort path),
	// which is exactly where waiting is allowed. The timeout means a driver that has stopped
	// answering cannot wedge teardown either -- we lose the write, not the device.
	if (handle_ != nullptr) {
		g_draining_handle = unbox_stream_handle(handle_);
		(void)yieldingRoutineWithTimeoutForSD(
		    []() -> bool {
			    uint32_t written = 0;
			    return deluge_file_write_collect(g_draining_handle, &written) != DELUGE_FILE_WRITE_PENDING;
		    },
		    kDrainTimeoutSeconds);
		g_draining_handle = 0;
	}

	bool ok = deluge_efatfs_stream_close(unbox_stream_handle(handle_));
	handle_ = nullptr; // matters even on error: don't let the destructor double-close
	if (!ok) {
		return std::unexpected(Status::ERR);
	}
	return {};
}

} // namespace deluge::io
