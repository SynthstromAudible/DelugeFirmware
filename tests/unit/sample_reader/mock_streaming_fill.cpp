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

// No async streaming-fill task in this spec: a requested fill never lands. Declared as an extern by the
// reader crate, so it must exist for the link even though this spec's one scenario never reaches it.
void deluge_streaming_request_chunk(void* /*chunk_backing*/) {
}

// Every reservation enqueue wakes the async fill task through this. With no such task here, there is
// nothing to wake -- a no-op is exact.
void deluge_streaming_signal_fill() {
}

bool deluge_efatfs_read_at(uint32_t /*handle*/, uint32_t /*byte_offset*/, void* /*dst*/, uint32_t /*count*/,
                           uint32_t* /*out_read*/) {
	return false;
}
