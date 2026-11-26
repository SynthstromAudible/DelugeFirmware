/*
 * Copyright © 2026 Synthstrom Audible Limited
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

/// deluge_storage_sample_peaks.h — the C header of the `deluge_storage_sample_peaks` crate: a sample's waveform peak
/// index.
///
/// One lock-free pyramid per sample, measured in frames: level 0 holds one 16-bit peak per 256 frames, and each level
/// up one peak per 16 entries of the level below. The cluster fill, the recorder and the background scan fold peaks
/// in; the waveform columns read them out. Only thread mode ([task]) allocates or frees; a fold never allocates,
/// blocks or locks, so it runs on any tier.
#ifndef DELUGE_STORAGE_SAMPLE_PEAKS_H
#define DELUGE_STORAGE_SAMPLE_PEAKS_H

#include "libdeluge/abi.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// A sample's waveform peak index. Opaque and reference-counted.
typedef struct DelugePeakIndex DelugePeakIndex;

/// The lowest and highest 16-bit sample value over some frames, every channel folded together.
typedef struct DelugePeak {
	int16_t min; ///< the lowest value
	int16_t max; ///< the highest value
} DelugePeak;

/// A peak, or nothing.
typedef struct DelugeMaybePeak {
	bool has;        ///< whether `peak` holds anything
	DelugePeak peak; ///< the peak, when `has`
} DelugeMaybePeak;

/// One column's plan: pending, or an interior peak plus up to two frame ranges `[read_lo[i], read_hi[i])` the caller
/// reads and folds in.
typedef struct DelugePeaksColumn {
	bool pending;        ///< a block the span covers wholly is not scanned yet
	bool has_interior;   ///< whether `interior` holds anything
	DelugePeak interior; ///< the stored peaks' fold
	uint8_t read_count;  ///< how many of the ranges below to read
	uint64_t read_lo[2]; ///< each range's first frame
	uint64_t read_hi[2]; ///< each range's end frame (exclusive)
} DelugePeaksColumn;

/// Allocates `size` bytes aligned to `align`; null on failure.
typedef void* (*DelugePeaksAllocFn)(size_t size, size_t align);
/// Frees what a DelugePeaksAllocFn returned.
typedef void (*DelugePeaksFreeFn)(void* ptr);

/// @brief [task] Install the index's allocator. Once, at boot, before any index is made.
/// @param alloc Allocates zero or more bytes; thread mode only.
/// @param free  Frees what `alloc` returned.
void deluge_peaks_init(DelugePeaksAllocFn alloc, DelugePeaksFreeFn free) DELUGE_NOEXCEPT;

/// @brief [task] A new index with one reference.
/// @param frames          The frames the sample holds (a take: those captured so far).
/// @param growing         True for a take whose length is not known yet.
/// @param capacity_frames The most frames the index may come to hold (a file: `frames`).
/// @return The index, or null on allocation failure.
DelugePeakIndex* deluge_peaks_create(uint64_t frames, bool growing, uint64_t capacity_frames) DELUGE_NOEXCEPT;

/// @brief [task] [audio] [isr] Take a reference.
/// @param index The index; null is ignored.
void deluge_peaks_retain(DelugePeakIndex* index) DELUGE_NOEXCEPT;

/// @brief [task] [audio] [isr] Drop a reference; the last one queues the index for deluge_peaks_collect.
/// @param index The index; null is ignored.
void deluge_peaks_release(DelugePeakIndex* index) DELUGE_NOEXCEPT;

/// @brief [task] Free every index whose last reference has gone.
void deluge_peaks_collect(void) DELUGE_NOEXCEPT;

/// @brief [task] Let the fill tap find `index` by the stream asset id; replaces any index bound to it.
/// @param asset The stream asset id.
/// @param index The index; it gains a reference. Null unbinds.
void deluge_peaks_bind_asset(uint32_t asset, DelugePeakIndex* index) DELUGE_NOEXCEPT;

/// @brief [task] Unbind the asset id, dropping the reference the binding held. A no-op if unbound.
/// @param asset The stream asset id.
void deluge_peaks_unbind_asset(uint32_t asset) DELUGE_NOEXCEPT;

/// @brief [task] Grow a take's index to hold `frames` frames. A fold past what is reserved is dropped.
/// @param index  The index; null is ignored.
/// @param frames The frames to make room for.
void deluge_peaks_reserve(DelugePeakIndex* index, uint64_t frames) DELUGE_NOEXCEPT;

/// @brief [task] Keep level-0 values for frames `[first, end)` from now on, so columns ending there read fewer
///        frames.
/// @param index The index; null is ignored.
/// @param first The first frame.
/// @param end   The end frame (exclusive).
void deluge_peaks_ensure_level0(DelugePeakIndex* index, uint64_t first, uint64_t end) DELUGE_NOEXCEPT;

/// @brief [task] [audio] Fold native frames starting at `first_frame`, completing every 256-frame block wholly
///        inside them. Never allocates.
/// @param index       The index; null is ignored.
/// @param first_frame The frame `bytes` starts at.
/// @param bytes       Whole native-format frames.
/// @param length      The bytes' length.
/// @param byte_depth  Bytes per sample, 1 to 4.
/// @param channels    Channels per frame, 1 or 2.
void deluge_peaks_fold_frames(DelugePeakIndex* index, uint64_t first_frame, const uint8_t* bytes, uint32_t length,
                              uint8_t byte_depth, uint8_t channels) DELUGE_NOEXCEPT;

/// @brief [task] [audio] Complete 256-frame block `block` with `peak`. Never allocates. A block already complete only
///        fills its kept level-0 value (the levels above keep its first fold, so fold one block's own frames only);
///        one past a final index's end is dropped.
/// @param index The index; null is ignored.
/// @param block The block: frames `[block * 256, block * 256 + 256)`, clipped to the index's end.
/// @param peak  The block's peak.
void deluge_peaks_fold_block(DelugePeakIndex* index, uint64_t block, DelugePeak peak) DELUGE_NOEXCEPT;

/// @brief [task] A take's frame count so far; `is_final` once its length is known.
/// @param index    The index; null is ignored.
/// @param frames   The frames it holds.
/// @param is_final True once the take's length is known.
void deluge_peaks_set_frames(DelugePeakIndex* index, uint64_t frames, bool is_final) DELUGE_NOEXCEPT;

/// @brief [task] The plan for frames `[first, end)`.
/// @param index The index; null answers pending.
/// @param first The first frame.
/// @param end   The end frame (exclusive); clipped to the index's frames.
/// @return The plan.
DelugePeaksColumn deluge_peaks_column(const DelugePeakIndex* index, uint64_t first, uint64_t end) DELUGE_NOEXCEPT;

/// @brief [task] [audio] The peak of native frames.
/// @param bytes      Whole native-format samples.
/// @param length     The bytes' length.
/// @param byte_depth Bytes per sample, 1 to 4.
/// @return The peak, or nothing for no whole sample.
DelugeMaybePeak deluge_peaks_of_frames(const uint8_t* bytes, uint32_t length, uint8_t byte_depth) DELUGE_NOEXCEPT;

/// @brief [task] The range over every block completed so far.
/// @param index The index; null answers nothing.
/// @return The range, or nothing before the first block completes.
DelugeMaybePeak deluge_peaks_range(const DelugePeakIndex* index) DELUGE_NOEXCEPT;

/// @brief [task] The frames the index holds: a file's length, or a take's frames captured so far.
/// @param index The index; null answers 0.
/// @return The frames.
uint64_t deluge_peaks_frames(const DelugePeakIndex* index) DELUGE_NOEXCEPT;

/// @brief [task] The first frame of the first incomplete block that ends after `from` (it may start up to 255 frames
///        before it).
/// @param index The index; null answers UINT64_MAX.
/// @param from  The frame to look from.
/// @return That block's first frame, or UINT64_MAX if every block from there is complete.
uint64_t deluge_peaks_next_unscanned(const DelugePeakIndex* index, uint64_t from) DELUGE_NOEXCEPT;

/// A sidecar's key: the audio file on its card, and the shape of its samples. A sidecar written for one key is
/// refused for any other.
typedef struct DelugePeakKey {
	uint32_t first_cluster; ///< the file's first cluster on its card
	uint32_t file_size;     ///< the file's size in bytes
	uint32_t fat_datetime;  ///< the file's FAT modify date and time, date << 16 | time
	uint32_t sample_rate;   ///< frames per second
	uint64_t frames;        ///< frames in the file
	uint8_t channels;       ///< channels per frame
	uint8_t byte_depth;     ///< bytes per sample
	uint16_t reserved;      ///< 0
} DelugePeakKey;

/// A sidecar section other than the peaks, carried byte for byte.
typedef struct DelugeSidecarSection {
	uint32_t tag;         ///< four ASCII bytes, little-endian
	const uint8_t* bytes; ///< its bytes; null only when `len` is 0
	uint32_t len;         ///< their count
} DelugeSidecarSection;

/// @brief [task] The length of the index's sidecar with `extras`.
/// @param index       The index; null answers 0.
/// @param extras      The other sections, at most 7, none tagged PEAK; null when `extra_count` is 0.
/// @param extra_count How many.
/// @return The length in bytes, or 0 if it cannot be written.
uint32_t deluge_peaks_sidecar_len(const DelugePeakIndex* index, const DelugeSidecarSection* extras,
                                  uint32_t extra_count) DELUGE_NOEXCEPT;

/// @brief [task] Writes the index's sidecar: its level 1 as the PEAK section, then `extras`.
/// @param index       The index; null writes nothing.
/// @param key         The audio file's key.
/// @param extras      As deluge_peaks_sidecar_len.
/// @param extra_count How many.
/// @param out         Where to write.
/// @param cap         Room at `out`, in bytes.
/// @return The bytes written, or 0 if they do not fit.
uint32_t deluge_peaks_sidecar_write(const DelugePeakIndex* index, const DelugePeakKey* key,
                                    const DelugeSidecarSection* extras, uint32_t extra_count, uint8_t* out,
                                    uint32_t cap) DELUGE_NOEXCEPT;

/// @brief [task] Checks a sidecar's header, section table and every section's checksum against `key`.
/// @return 0, or a negative reason: -1 short, -2 magic, -3 version, -4 header CRC, -5 key, -6 table, -7 section CRC.
int32_t deluge_peaks_sidecar_check(const uint8_t* bytes, uint32_t len, const DelugePeakKey* key) DELUGE_NOEXCEPT;

/// @brief [task] Restores a sidecar's PEAK section into the index, a final index of its frames. Entries already
///        complete are left as they are.
/// @return The complete level-1 entries it held, or a negative reason as deluge_peaks_sidecar_check, -5 also for an
///         index of other frames, -8 for a missing or misfitting PEAK section.
int32_t deluge_peaks_sidecar_restore(DelugePeakIndex* index, const DelugePeakKey* key, const uint8_t* bytes,
                                     uint32_t len) DELUGE_NOEXCEPT;

/// @brief [task] A section of a sidecar that passes deluge_peaks_sidecar_check.
/// @param out_bytes Set to the section's bytes, inside `bytes`.
/// @param out_len   Set to their count.
/// @return Whether the sidecar checks out and holds a section tagged `tag`.
bool deluge_peaks_sidecar_section(const uint8_t* bytes, uint32_t len, const DelugePeakKey* key, uint32_t tag,
                                  const uint8_t** out_bytes, uint32_t* out_len) DELUGE_NOEXCEPT;

/// @brief [task] How many of the index's level-1 entries (4,096 frames each) are complete. Null answers 0.
uint32_t deluge_peaks_complete_level1(const DelugePeakIndex* index) DELUGE_NOEXCEPT;

/// @brief [task] How many level-1 entries the index has. Null answers 0.
uint32_t deluge_peaks_level1_entries(const DelugePeakIndex* index) DELUGE_NOEXCEPT;

/// @brief [task] Keeps level-0 values for frames [first, end), together with every window kept in the last second.
void deluge_peaks_keep_level0(DelugePeakIndex* index, uint64_t first, uint64_t end, uint32_t now_ms) DELUGE_NOEXCEPT;

/// @brief [task] Frees the level-0 chunks earlier calls retired, once no fold is in flight, and retires those outside
///        the kept window (all of them once it is older than a second).
/// @return How many it retired.
uint32_t deluge_peaks_trim_level0(DelugePeakIndex* index, uint32_t now_ms) DELUGE_NOEXCEPT;

/// @brief [task] How many level-0 chunks (1,024 blocks each) the index holds. Null answers 0.
uint32_t deluge_peaks_level0_chunks(const DelugePeakIndex* index) DELUGE_NOEXCEPT;

/// A take's peaks between clusters: the recorder's fold state. Opaque.
typedef struct DelugePeaksTake DelugePeaksTake;

/// @brief [task] A new take fold, before the file's first byte.
/// @return The fold, or null on allocation failure.
DelugePeaksTake* deluge_peaks_take_create(void) DELUGE_NOEXCEPT;

/// @brief [task] Free a take fold.
/// @param take The fold; null is ignored.
void deluge_peaks_take_drop(DelugePeaksTake* take) DELUGE_NOEXCEPT;

/// @brief [task] Fold the file's next bytes, from where the last call stopped (the first call: byte 0): whole 256-frame
///        blocks of 24-bit little-endian frames, the header skipped, a frame split across two calls carried. Reserves
///        the index's room for what it folds.
/// @param take        The fold; null is ignored.
/// @param index       The take's index; null folds nothing but counts the bytes.
/// @param bytes       The file's next bytes.
/// @param length      The bytes' length.
/// @param audio_start The byte the audio starts at.
/// @param channels    Channels per frame, 1 or 2.
void deluge_peaks_take_feed(DelugePeaksTake* take, DelugePeakIndex* index, const uint8_t* bytes, uint32_t length,
                            uint32_t audio_start, uint8_t channels) DELUGE_NOEXCEPT;

/// @brief [task] Fold the last, part-filled block. Call once the take's index is final.
/// @param take  The fold; null is ignored.
/// @param index The take's index; null is ignored.
void deluge_peaks_take_finish(DelugePeaksTake* take, DelugePeakIndex* index) DELUGE_NOEXCEPT;

/// @brief [task] The frames folded or counted so far.
/// @param take The fold; null answers 0.
/// @return The frames.
uint64_t deluge_peaks_take_frames(DelugePeaksTake* take) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif // DELUGE_STORAGE_SAMPLE_PEAKS_H
