#pragma once

#include "io/file.hpp" // reuses deluge::io::Status / to_status / to_deluge_status
#include "libdeluge/stream_io.h"

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace deluge::io {

class Stream {
public:
	Stream(Stream&) = delete;
	Stream(Stream&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
	Stream& operator=(Stream&) = delete;
	Stream& operator=(Stream&& other) noexcept {
		if (this != &other) {
			if (handle_) {
				// Route through close() rather than the raw C-ABI so the handle teardown
				// lives in exactly one place -- see stream.cpp's close().
				(void)close();
			}
			handle_ = other.handle_;
			other.handle_ = nullptr;
		}
		return *this;
	}
	~Stream() {
		if (handle_) {
			(void)close();
		}
	}

	[[nodiscard]] static std::expected<Stream, Status> open(std::string_view path, DelugeStreamMode mode);
	std::expected<uint32_t, Status> write_at(uint32_t byte_offset, std::span<const std::byte> buffer);

	/// @brief Ask for @p buffer to be written at @p byte_offset, without blocking.
	///
	/// @param byte_offset Absolute offset within the file.
	/// @param buffer      Caller-owned. Must stay alive and unmodified until @ref collect_write_at
	///                    returns a terminal status. NOT copied -- the pointer crosses the boundary,
	///                    the bytes do not.
	/// @return false when the request was refused (a write is already in flight for this stream, or
	///         the channel is not running). The caller must take its synchronous arm.
	[[nodiscard]] bool request_write_at(uint32_t byte_offset, std::span<const std::byte> buffer);

	/// @brief Collect the in-flight write, if it has finished.
	/// @param out_written Written only on `DELUGE_FILE_WRITE_READY`, with the true byte count.
	/// @return a `DELUGE_FILE_WRITE_*` value.
	[[nodiscard]] uint8_t collect_write_at(uint32_t* out_written);
	/// @brief Read up to @p dst.size() bytes at absolute @p byte_offset back through this stream's
	///        OWN open write context, bounded by its live (not yet flushed to disk) size.
	///
	/// Used by `SampleRecorder::alterFile()` (a positional read/write pass over its own open write
	/// context) and `finalizeRecordedFile()`'s header patch-back. Backed by
	/// `deluge_efatfs_stream_read_at_via`. EOF-honest -- the returned count may be short of
	/// @p dst.size().
	/// @param byte_offset Absolute byte offset into the stream to read from.
	/// @param dst          Buffer to read into; up to its full size may be filled.
	/// @return The number of bytes actually read (may be less than dst.size() at EOF), or a Status
	///         error.
	std::expected<uint32_t, Status> read_at_via(uint32_t byte_offset, std::span<std::byte> dst);
	std::expected<void, Status> truncate(uint32_t new_size);
	std::expected<uint32_t, Status> size();
	std::expected<uint32_t, Status> sector_of(uint32_t cluster_index);
	std::expected<void, Status> close();

private:
	explicit Stream(DelugeStream* handle) : handle_(handle) {}
	DelugeStream* handle_ = nullptr;
};

} // namespace deluge::io
