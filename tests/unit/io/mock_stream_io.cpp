// A stand-in for include/libdeluge/stream_io.h's deluge_efatfs_stream_* C-ABI, for host-only deluge::io::Stream
// specs. It keeps no file: an open succeeds with handle 1 and is counted, so a spec can tell whether a call reached
// the platform at all.
#include "mock_stream_io.h"

#include "libdeluge/stream_io.h"

namespace {
uint32_t g_opens = 0;
} // namespace

void mock_stream_io_reset() {
	g_opens = 0;
}

uint32_t mock_stream_io_opens() {
	return g_opens;
}

extern "C" {

DelugeStatus deluge_efatfs_stream_open(const char* /*path*/, uint8_t /*mode*/, DelugeHandle* out_handle) noexcept {
	++g_opens;
	*out_handle = 1;
	return DELUGE_OK;
}

DelugeStatus deluge_efatfs_stream_write_at(DelugeHandle /*handle*/, uint32_t /*byte_offset*/, const void* /*src*/,
                                           uint32_t count, uint32_t* out_written) noexcept {
	*out_written = count;
	return DELUGE_OK;
}

DelugeStatus deluge_efatfs_stream_read_at_via(DelugeHandle /*handle*/, uint32_t /*byte_offset*/, void* /*dst*/,
                                              uint32_t count, uint32_t* out_read) noexcept {
	*out_read = count;
	return DELUGE_OK;
}

DelugeStatus deluge_efatfs_stream_truncate(DelugeHandle /*handle*/, uint32_t /*new_len*/) noexcept {
	return DELUGE_OK;
}

DelugeStatus deluge_efatfs_stream_size(DelugeHandle /*handle*/, uint32_t* out_size) noexcept {
	*out_size = 0;
	return DELUGE_OK;
}

DelugeStatus deluge_efatfs_stream_close(DelugeHandle /*handle*/) noexcept {
	return DELUGE_OK;
}

} // extern "C"
