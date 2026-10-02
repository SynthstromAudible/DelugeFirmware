#include "storage/audio/stream/stitch.h"
#include "storage/audio/stream/convert.h"
#include <algorithm>
#include <cstddef>
#include <utility>

namespace deluge::audio::stream {

namespace {
// Swaps byte 0 and byte 2 of a 3-byte group in place (the ENDIANNESS_WRONG_24 fixup).
void byteswap3(std::byte* group) {
	std::swap(group[0], group[2]);
}

// Give extra bytes to the previous cluster: refresh its overhang from our own head (unconditionally),
// then -- the first time either half of this boundary visits it -- finish converting whichever word
// straddles the boundary.
void stitch_prev(std::span<std::byte> self_data, StitchPrevEdge& prev, RawDataFormat format, int32_t misalignment,
                 int32_t cluster_index, uint32_t audio_data_start_pos_bytes, size_t cluster_size) {
	// prev.tail[4..11) mirrors prevCluster->payload()[cluster_size..cluster_size+7) -- kept in sync with our
	// own head regardless of format or whether this boundary was already converted.
	auto prev_overhang = prev.tail.subspan<4, 7>();
	std::ranges::copy(self_data.first<7>(), prev_overhang.begin());

	if (format == RawDataFormat::ENDIANNESS_WRONG_24) {
		if (*prev.end_boundary_converted) {
			return; // Already finished the straddling group on an earlier visit.
		}

		uint32_t bytes_before_cluster =
		    static_cast<uint32_t>(cluster_index) * static_cast<uint32_t>(cluster_size) - audio_data_start_pos_bytes;
		int32_t straddle_offset = static_cast<int32_t>(bytes_before_cluster % 3);

		// straddle_offset == 0: no 3-byte group straddles the boundary -- nothing to convert.
		if (straddle_offset != 0) {
			// The straddling group starts this far into prev.tail; convert it now (we've probably just
			// copied over the group after it too, which was already converted).
			byteswap3(&prev.tail[4 - straddle_offset]);

			// Copy back the (at most 2) converted bytes that land in our own head -- the maximum a
			// 24-bit group can overhang the boundary.
			std::ranges::copy(prev_overhang.first(2), self_data.begin());
		}

		*prev.end_boundary_converted = true;
	}
	else if (format != RawDataFormat::NATIVE) {
		if (*prev.end_boundary_converted) {
			return; // Already finished the straddling word on an earlier visit.
		}

		// misalignment == 0: the boundary falls on a word boundary -- nothing straddles it.
		if (misalignment != 0) {
			// The straddling word starts this far into prev.tail; convert it now (we've probably just
			// moved over the word after it too, which was already converted).
			convert_word_in_place(&prev.tail[misalignment], format);

			// Copy back the (at most 3) converted bytes that land in our own head -- the maximum a word
			// can overhang the boundary.
			std::ranges::copy(prev_overhang.first(3), self_data.begin());
		}

		*prev.end_boundary_converted = true;
	}
	// NATIVE: nothing to convert; end_boundary_converted is left untouched.
}

// Grab extra bytes from the next cluster: finish converting whichever word straddles the boundary (the
// first time either half of this boundary visits it), then finalize this cluster's overhang from the
// next cluster's head.
void stitch_next(std::span<std::byte> self_data, StitchNextEdge& next, RawDataFormat format, int32_t misalignment,
                 int32_t cluster_index, uint32_t audio_data_start_pos_bytes, size_t cluster_size) {
	auto self_overhang = self_data.last<7>(); // self_data[cluster_size..cluster_size+7)
	bool need_copy7 = false;

	if (format == RawDataFormat::ENDIANNESS_WRONG_24) {
		uint32_t bytes_before_next_cluster =
		    static_cast<uint32_t>(cluster_index + 1) * static_cast<uint32_t>(cluster_size) - audio_data_start_pos_bytes;
		int32_t straddle_offset = static_cast<int32_t>(bytes_before_next_cluster % 3);

		// straddle_offset == 0: no 3-byte group straddles the boundary -- just finalize the overhang.
		if (straddle_offset == 0) {
			need_copy7 = true;
		}
		else {
			bool const already_converted = *next.start_boundary_converted;

			// Stage the bytes the straddling group needs: the full head if this is the first visit, or
			// just the 2 bytes we backed up if we've already converted (and overwritten) next.head.
			if (!already_converted) {
				std::ranges::copy(next.head, self_overhang.begin());
			}
			else {
				std::ranges::copy(next.unconverted_head.first(2), self_overhang.begin());
			}

			// The straddling group starts this far into our overhang; convert it now.
			int32_t start_pos = static_cast<int32_t>(cluster_size) - straddle_offset;
			byteswap3(&self_data[start_pos]);

			if (!already_converted) {
				*next.start_boundary_converted = true;
				// Copy back the (at most 2) converted bytes that land in the next cluster's head.
				std::ranges::copy(self_overhang.first(2), next.head.begin());
			}
			else {
				need_copy7 = true;
			}
		}
	}
	else if (format != RawDataFormat::NATIVE) {
		// misalignment == 0: the boundary falls on a word boundary -- just finalize the overhang.
		if (misalignment == 0) {
			need_copy7 = true;
		}
		else {
			int32_t start_pos = static_cast<int32_t>(cluster_size) - 4 + misalignment;
			bool const already_converted = *next.start_boundary_converted;

			if (!already_converted) {
				// Stage the next cluster's first 7 bytes. This MUST happen before the convert below:
				// start_pos can reach cluster_size-1, so the straddling word overlaps this freshly
				// staged overhang.
				std::ranges::copy(next.head, self_overhang.begin());
				convert_word_in_place(&self_data[start_pos], format);

				// Copy back the (at most 3) converted bytes that land in the next cluster's head.
				std::ranges::copy(self_overhang.first(3), next.head.begin());
				*next.start_boundary_converted = true;
			}
			else {
				// Stage from the backed-up unconverted bytes (transient -- the straddling word can
				// still overlap this range, but the need_copy7 finalize below overwrites it with the
				// real next.head regardless).
				std::ranges::copy(next.unconverted_head, self_overhang.begin());
				convert_word_in_place(&self_data[start_pos], format);
				need_copy7 = true;
			}
		}
	}
	else {
		need_copy7 = true; // NATIVE
	}

	if (need_copy7) {
		// Finalize this cluster's overhang wholesale from the next cluster's first 7 bytes.
		std::ranges::copy(next.head, self_overhang.begin());
	}
}
} // namespace

// Fixes up the raw bytes straddling a cluster boundary (byte-swap for ENDIANNESS_WRONG_24, or the
// general word convert otherwise) using the neighboring cluster's overhang, then keeps both clusters'
// overhang copies in sync. Invoked by the streaming fill's convert/stitch tail: the Rust
// `deluge_sample_fill` fill (`fill_logic`) routes here through the `deluge_sample_convert` shim on
// the device app-convert build. See stitch.h for the edge-struct index bases.
void stitch_boundaries(std::span<std::byte> self_data, int32_t cluster_index, RawDataFormat format,
                       uint32_t audio_data_start_pos_bytes, size_t cluster_size, bool& self_start_boundary_converted,
                       bool& self_end_boundary_converted, StitchPrevEdge* prev, StitchNextEdge* next) {
	int32_t misalignment = static_cast<int32_t>(audio_data_start_pos_bytes & 0b11);

	if (prev != nullptr) {
		stitch_prev(self_data, *prev, format, misalignment, cluster_index, audio_data_start_pos_bytes, cluster_size);
		self_start_boundary_converted = true;
	}
	if (next != nullptr) {
		stitch_next(self_data, *next, format, misalignment, cluster_index, audio_data_start_pos_bytes, cluster_size);
		self_end_boundary_converted = true;
	}
}

} // namespace deluge::audio::stream
