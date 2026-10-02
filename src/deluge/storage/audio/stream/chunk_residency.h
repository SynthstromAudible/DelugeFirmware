/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#pragma once

#include <cstddef>
#include <cstdint>

class Sample;

/// @file
/// Resource-manager Source callbacks for SAMPLE (streamed) chunks, plus the asset-*definition* core.
/// `owner` is always the `Sample*` registered by `deluge_streaming_define_asset()`; each callback
/// reaches that sample's `SampleStream` via `sample->stream()` -- residency itself is manager-owned,
/// so this callback only constructs a chunk's bytes (`loaded` stays false; the async streaming-fill
/// task then reads the card bytes into it — `streaming_loader.rs`, over `deluge_sample_fill`'s
/// `native_begin`/`native_finish`).
/// See sample_stream.h's "Cluster residency" section for the broader contract this implements.

extern "C" {

/// @brief Lazily define @p sample's resource-manager Asset, whose Chunks are its SAMPLE clusters.
///
/// Idempotent: returns the existing id (cached on `sample->stream()`) on later calls. The manager is
/// the sole SDRAM evictor, so a missing manager or an exhausted asset table is fatal (`FREEZE`) --
/// there is no legacy fallback, hence this never returns `DELUGE_RESOURCE_NO_ASSET`.
/// @param sample The sample whose resource-manager Asset should be defined.
/// @return The Asset id.
uint32_t deluge_streaming_define_asset(Sample* sample);

/// @brief Construct the streamed chunk into @p dest (the manager backing) but do not read it
///        (`loaded` stays false), so the audio thread never blocks — the background loader fills it
///        later.
///
/// Defined in Rust (`deluge_sample_fill::chunk`) — the chunk's storage lives there; this decl
/// only lets `deluge_streaming_define_asset()` register the symbol as the asset's construct callback.
/// @param ctx    Manager-supplied Source context (unused here).
/// @param owner  The owning `Sample*`, as registered by deluge_streaming_define_asset().
/// @param index  Cluster index within the sample.
/// @param dest   The manager-owned backing to construct the chunk into.
void deluge_streaming_chunk_construct(void* ctx, void* owner, uint32_t index, void* dest);

} // extern "C"
