#include "mock_streaming_fill.h"

// Trivial, never-actually-exercised bodies -- see mock_streaming_fill.h's own doc for why the link
// needs them at all even though this spec's one scenario (an asset with no registered
// fill-context) never reaches any of them at runtime.

uint32_t deluge_streaming_define_asset(Sample* /*sample*/) {
	return 0;
}

namespace {
DelugeResource* g_active_manager = nullptr;
}

void set_active_manager(DelugeResource* handle) {
	g_active_manager = handle;
}

DelugeResource* deluge_streaming_resource_manager() {
	return g_active_manager;
}

// No async streaming-fill task in this spec: `Reader::acquire_and_fill` gates on
// deluge_streaming_async_active() (false here) and takes its synchronous fill route, never calling
// deluge_streaming_request_chunk. Both are declared as externs by the reader crate, so they must exist
// for the link even though this spec's one scenario never reaches either.
bool deluge_streaming_async_active() {
	return false;
}

void deluge_streaming_request_chunk(void* /*chunk_backing*/) {
}

// Every reservation enqueue wakes the async fill task through this. With no such task here (see
// deluge_streaming_async_active above), there is nothing to wake -- a no-op is exact.
void deluge_streaming_signal_fill() {
}

bool deluge_efatfs_read_at(uint32_t /*handle*/, uint32_t /*byte_offset*/, void* /*dst*/, uint32_t /*count*/,
                           uint32_t* /*out_read*/) {
	return false;
}
