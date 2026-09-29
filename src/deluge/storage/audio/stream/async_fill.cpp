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

} // extern "C"
