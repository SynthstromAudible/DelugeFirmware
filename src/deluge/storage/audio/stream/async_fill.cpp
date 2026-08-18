/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

#include "storage/audio/stream/async_fill.h"

#include "libdeluge/file_io.h"   // deluge_efatfs_file_*/_dir_* weak stubs
#include "libdeluge/stream_io.h" // deluge_efatfs_stream_* weak stubs

#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"
#include "storage/audio/stream/sample_stream.h"
#include "storage/audio/stream/stitch.h"
#include "storage/cluster/cluster.h"
#include <cstddef>
#include <optional>
#include <span>

#include "deluge_resource.h" // deluge_resource_mark_ready

// FFI layout guard: `StreamingFillDescriptor` crosses the C++/Rust boundary by value (see
// streaming_fill.h) with a hand-written `#[repr(C)]` mirror in streaming_loader.rs. These
// static_asserts catch field drift at compile time on whichever side notices first. Expressed
// pointer-width-relative (not hardcoded byte offsets) so the same assertions hold unchanged on
// both the 32-bit ARM device and the 64-bit host_app build: `dest` leads at offset 0, then
// `num_sectors` (u32, packed right after the pointer), then `ok` (1 byte, at ptr+4), then 3 pad
// bytes to realign the next u32, then `handle` at ptr+8 and `byte_offset` at ptr+12, and the
// struct's overall size pads up to the pointer's own alignment (its strictest member) —
// sizeof(ptr)+16 covers that on both widths (20 on 32-bit: dest[4]+num_sectors[4]+ok[1]->pad[3]+
// handle[4]+byte_offset[4]=20; 24 on 64-bit: dest[8]+num_sectors[4]+ok[1]->pad[3]+handle[4]+
// byte_offset[4]=24, already a multiple of the 8-byte pointer alignment). Verified by compiling
// both builds, not derived from a naive guess.
static_assert(offsetof(StreamingFillDescriptor, dest) == 0);
static_assert(offsetof(StreamingFillDescriptor, num_sectors) == sizeof(uint8_t*));
static_assert(offsetof(StreamingFillDescriptor, ok) == sizeof(uint8_t*) + 4);
static_assert(offsetof(StreamingFillDescriptor, handle) == sizeof(uint8_t*) + 8);
static_assert(offsetof(StreamingFillDescriptor, byte_offset) == sizeof(uint8_t*) + 12);
static_assert(sizeof(StreamingFillDescriptor) == sizeof(uint8_t*) + 16);

// FFI layout guard for DelugeStreamingFillContext, mirroring FillContext's own
// `core::mem::offset_of!` guard in streaming_loader.rs. Unlike StreamingFillDescriptor, this struct
// holds no pointer, so its layout is identical on the 32-bit device and the 64-bit host_app build:
// two leading u32s (0, 4), then the u64 realigned at its natural 8-byte boundary (already aligned,
// at 8), then i32/u32/u32 (16, 20, 24), then the trailing u8 (28), padded up to the u64 member's
// 8-byte alignment (32 total).
static_assert(offsetof(DelugeStreamingFillContext, efatfs_handle) == 0);
static_assert(offsetof(DelugeStreamingFillContext, audio_data_start_pos_bytes) == 4);
static_assert(offsetof(DelugeStreamingFillContext, audio_data_length_bytes) == 8);
static_assert(offsetof(DelugeStreamingFillContext, first_cluster_index_with_no_audio_data) == 16);
static_assert(offsetof(DelugeStreamingFillContext, cluster_size) == 20);
static_assert(offsetof(DelugeStreamingFillContext, cluster_size_magnitude) == 24);
static_assert(offsetof(DelugeStreamingFillContext, raw_data_format) == 28);
static_assert(offsetof(DelugeStreamingFillContext, byte_depth) == 29);
static_assert(offsetof(DelugeStreamingFillContext, num_channels) == 30);
static_assert(sizeof(DelugeStreamingFillContext) == 32);

// The DelugeChunkConvertState FFI layout guard lives Rust-side only (the crate that owns the
// convert-state accessors, `deluge_sample_fill`, guards its own mirror in `lib.rs`): those accessor
// bodies live in Rust, so this TU no longer touches the struct.

extern "C" {

DelugeResource* deluge_streaming_resource_manager(void) {
	return GeneralMemoryAllocator::get().resourceManager();
}

// Weak fallback for the region-port open() bridge's stream-backing -> resource-asset accessor.
// The real definition (sample_stream.cpp) forwards to
// deluge_streaming_define_asset() (chunk_residency.cpp) wherever a real SampleStream is compiled in; build
// configs that link this TU without one (a minimal test driver assembling its own source list,
// mirroring the other weak fallbacks in this file) resolve this no-op instead.
__attribute__((weak)) uint32_t deluge_sample_stream_asset_id(void* /*stream_backing*/) {
	return DELUGE_RESOURCE_NO_ASSET;
}

// The streamed chunk's seven field accessors (`deluge_streaming_chunk_{unloadable,set_unloadable,
// payload,set_loaded,loaded,convert_state,set_convert_state}`) live in Rust
// (`deluge_sample_fill::chunk`) alongside the chunk's storage — this TU does not define them or
// read the chunk's byte layout. `deluge_streaming_resource_manager` (above) and the weak fallbacks
// (below) stay here; only the chunk-field bodies live elsewhere.

// Weak fallbacks for the two async-streaming-loader selector/wakeup symbols. The Rust Embassy BSP
// provides the real definitions (streaming_loader.rs) whenever it links this crate —
// unconditionally, so `deluge_streaming_async_active()` always resolves there regardless of
// whether `async_streaming_loader` is enabled (its return value depends on the cargo feature; the
// symbol's existence does not). Every other BSP/config (legacy/host-cooperative sim, rza1) never
// links that crate, so these weak definitions are what resolve instead: "no async backing, never
// signalled" — those retired configs have no streaming-fill drainer at all.
__attribute__((weak)) bool deluge_streaming_async_active(void) {
	return false;
}

__attribute__((weak)) void deluge_streaming_signal_fill(void) {
	// No async task to wake on this BSP/config.
}

// Weak fallback for the reader range-fill's async-BSP blocking-fill routine. The Rust Embassy BSP
// provides the real definition (streaming_loader.rs) whenever it links that crate; every other
// BSP/config resolves this instead. Unreachable on the hot path there: those BSPs report
// deluge_streaming_async_active() false, so the reader takes its synchronous `fill_now` branch and
// never calls this. Returns false so any stray call degrades to a not-ready read rather than
// silently claiming success.
__attribute__((weak)) bool deluge_streaming_fill_chunk_blocking(void* /*chunk_backing*/) {
	return false;
}

// Weak fallback for the offline stem-export drain-all-queued routine. The Rust Embassy BSP provides
// the real definition (streaming_loader.rs) whenever it links that crate; every other BSP/config
// resolves this instead. StemExport::renderWait (the only caller) runs only on BSPs that do offline
// export, which are exactly the ones that link the real definition — so this fallback just returns
// false (nothing drained) for a stray call and is never reached on the hot path.
__attribute__((weak)) bool deluge_streaming_drain_queue_blocking(void) {
	return false;
}

// Weak fallbacks for the non-blocking request/poll pairs, matching the two blocking routines above:
// the Rust Embassy BSP provides the real definitions (streaming_loader.rs), every other BSP/config
// resolves these. The request halves are no-ops. The two poll halves answer differently, and
// deliberately: chunk_ready reports NOT ready, degrading a stray call to a not-ready read rather than
// silently claiming a fill succeeded (the same choice deluge_streaming_fill_chunk_blocking's fallback
// makes, and unreachable for the same reason -- these BSPs report async_active false, so the reader
// takes its synchronous fill_now branch). queue_drained reports drained, because there is no queue on
// such a BSP and answering otherwise would loop a poller forever.
__attribute__((weak)) void deluge_streaming_request_chunk(void* /*chunk_backing*/) {
}

__attribute__((weak)) bool deluge_streaming_chunk_ready(void* /*chunk_backing*/) {
	return false;
}

__attribute__((weak)) void deluge_streaming_request_drain(void) {
}

__attribute__((weak)) bool deluge_streaming_queue_drained(void) {
	return true;
}

// Weak fallbacks for the embedded-fatfs streaming READ symbols. The Rust Embassy BSP provides the
// real definitions (efatfs_fs.rs / streaming_loader.rs) whenever it links this crate with the
// `efatfs_streaming` feature; every other BSP/config resolves these instead: "no efatfs backing" —
// open/read_at always fail, close is a no-op, and the selector is false. NOTE: the streaming read is
// efatfs-only — there is NO C-FatFS read fallback. On a non-efatfs BSP (the
// legacy C/C++ RZA1 BSP, committed for retirement in favour of the Rust BSP) open_read_stream()
// therefore fails and streamed samples do not load; that BSP's streaming read is retired, not
// silently falling back.
__attribute__((weak)) bool deluge_efatfs_open(const char* /*path*/, uint32_t* /*out_handle*/, bool* out_table_full) {
	if (out_table_full != nullptr) {
		*out_table_full = false; // No efatfs handle table on this BSP/config -- never "full".
	}
	return false;
}

__attribute__((weak)) void deluge_efatfs_close(uint32_t /*handle*/) {
	// No efatfs handle table on this BSP/config.
}

__attribute__((weak)) bool deluge_efatfs_read_at(uint32_t /*handle*/, uint32_t /*byte_offset*/, void* /*dst*/,
                                                 uint32_t /*count*/, uint32_t* /*out_read*/) {
	return false; // No efatfs handle table on this BSP/config.
}

// Weak fallback for `deluge_file_invalidate_cache` (`include/libdeluge/file_io.h`): a hook a few
// call sites (deluge.cpp, delete_file.cpp, sample_recorder.cpp, save_song_ui.cpp, stem_export.cpp)
// still use to invalidate directory-listing caches after writing through some path other than
// `deluge::io::File`/`Directory` itself. No BSP maintains such a cache any more (the C-FatFS
// adapter that once did is deleted) -- a no-op everywhere, kept only so those call sites keep
// linking.
__attribute__((weak)) void deluge_file_invalidate_cache(void) {
}

// Weak fallbacks for the task-context efatfs file/directory C-ABI
// (`include/libdeluge/file_io.h`). The Rust Embassy BSP provides the real
// definitions (`efatfs_fs.rs` device / `efatfs_host_shim.rs` host) whenever it
// links this crate with the `efatfs_streaming` feature; every other
// BSP/config resolves these instead. `deluge::io::File`/`Directory`
// (file.cpp) call these unconditionally, so a BSP without the real symbols
// still links but every call fails -- these exist purely so the link
// succeeds. The 13 fallible ops report DELUGE_ERR_NODEV ("no such device"):
// there is no efatfs handle table on this BSP/config at all.
__attribute__((weak)) DelugeStatus deluge_efatfs_file_open(const char* /*path*/, uint8_t /*mode*/,
                                                           uint32_t* /*out_handle*/) {
	return DELUGE_ERR_NODEV;
}

__attribute__((weak)) DelugeStatus deluge_efatfs_file_read(uint32_t /*handle*/, void* /*dst*/, uint32_t /*count*/,
                                                           uint32_t* /*out_read*/) {
	return DELUGE_ERR_NODEV;
}

__attribute__((weak)) DelugeStatus deluge_efatfs_file_read_exact(uint32_t /*handle*/, void* /*dst*/, uint32_t /*count*/,
                                                                 uint32_t* /*out_read*/) {
	return DELUGE_ERR_NODEV;
}

__attribute__((weak)) DelugeStatus deluge_efatfs_file_write(uint32_t /*handle*/, const void* /*src*/,
                                                            uint32_t /*count*/, uint32_t* /*out_written*/) {
	return DELUGE_ERR_NODEV;
}

__attribute__((weak)) DelugeStatus deluge_efatfs_file_seek(uint32_t /*handle*/, uint32_t /*offset*/) {
	return DELUGE_ERR_NODEV;
}

__attribute__((weak)) DelugeStatus deluge_efatfs_file_size(uint32_t /*handle*/, uint32_t* /*out_size*/) {
	return DELUGE_ERR_NODEV;
}

__attribute__((weak)) bool deluge_efatfs_stats(uint32_t* /*out_free_clusters*/, uint32_t* /*out_total_clusters*/) {
	return false;
}

__attribute__((weak)) bool deluge_efatfs_mount(void) {
	return false;
}

__attribute__((weak)) bool deluge_efatfs_is_mounted(void) {
	return false;
}

__attribute__((weak)) bool deluge_efatfs_remount(void) {
	return false;
}

__attribute__((weak)) bool deluge_efatfs_cluster_size(uint32_t* /*out_bytes*/) {
	return false;
}

__attribute__((weak)) DelugeStatus deluge_efatfs_file_truncate(uint32_t /*handle*/, uint32_t /*new_len*/) {
	return DELUGE_ERR_NODEV;
}

__attribute__((weak)) void deluge_efatfs_file_close(uint32_t /*handle*/) {
	// No task-context file table on this BSP/config.
}

__attribute__((weak)) DelugeStatus deluge_efatfs_dir_open(const char* /*path*/, uint32_t* /*out_handle*/) {
	return DELUGE_ERR_NODEV;
}

__attribute__((weak)) DelugeStatus deluge_efatfs_dir_read(uint32_t /*handle*/, char* /*out_name*/,
                                                          uint32_t /*out_name_cap*/, bool* /*out_is_dir*/,
                                                          uint32_t* /*out_size*/, uint32_t* /*out_modified*/,
                                                          uint8_t* /*out_attrs*/, bool* /*out_has_entry*/) {
	return DELUGE_ERR_NODEV;
}

__attribute__((weak)) void deluge_efatfs_dir_close(uint32_t /*handle*/) {
	// No dir-cursor table on this BSP/config.
}

__attribute__((weak)) DelugeStatus deluge_efatfs_mkdir(const char* /*path*/) {
	return DELUGE_ERR_NODEV;
}

__attribute__((weak)) DelugeStatus deluge_efatfs_unlink(const char* /*path*/) {
	return DELUGE_ERR_NODEV;
}

__attribute__((weak)) DelugeStatus deluge_efatfs_rename(const char* /*old_path*/, const char* /*new_path*/) {
	return DELUGE_ERR_NODEV;
}

__attribute__((weak)) DelugeStatus deluge_efatfs_set_time(const char* /*path*/, uint16_t /*year*/, uint8_t /*month*/,
                                                          uint8_t /*day*/, uint8_t /*hour*/, uint8_t /*minute*/,
                                                          uint8_t /*second*/) {
	return DELUGE_ERR_NODEV;
}

// Weak fallbacks for the persistent stream-write efatfs C-ABI
// (`include/libdeluge/stream_io.h`). The Rust Embassy BSP provides the real definitions
// (`efatfs_fs.rs` device / `efatfs_host_shim.rs` host) whenever it links this crate with the
// `efatfs_streaming` feature; every other BSP/config resolves these instead. `deluge::io::Stream`
// (stream.cpp) calls these unconditionally, so a BSP without the real symbols still links but every
// call fails -- these exist purely so the link succeeds.
__attribute__((weak)) bool deluge_efatfs_stream_open(const char* /*path*/, uint8_t /*mode*/, uint32_t* /*out_handle*/) {
	return false;
}

__attribute__((weak)) bool deluge_efatfs_stream_write_at(uint32_t /*handle*/, uint32_t /*byte_offset*/,
                                                         const void* /*src*/, uint32_t /*count*/,
                                                         uint32_t* /*out_written*/) {
	return false;
}

__attribute__((weak)) bool deluge_efatfs_stream_read_at_via(uint32_t /*handle*/, uint32_t /*byte_offset*/,
                                                            void* /*dst*/, uint32_t /*count*/, uint32_t* /*out_read*/) {
	return false;
}

__attribute__((weak)) bool deluge_efatfs_stream_flush(uint32_t /*handle*/) {
	return false;
}

__attribute__((weak)) bool deluge_efatfs_stream_truncate(uint32_t /*handle*/, uint32_t /*new_len*/) {
	return false;
}

__attribute__((weak)) bool deluge_efatfs_stream_size(uint32_t /*handle*/, uint32_t* /*out_size*/) {
	return false;
}

__attribute__((weak)) bool deluge_efatfs_stream_close(uint32_t /*handle*/) {
	return false; // No stream-write context table on this BSP/config.
}

} // extern "C"
