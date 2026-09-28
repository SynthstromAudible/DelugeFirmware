// Fill-differential harness: a from-scratch C++ replica of `finish_fill`'s convert+stitch
// orchestration (storage/audio/stream/async_fill.cpp:110-162), over plain buffers instead of a
// StreamedChunk/SampleStream/Sample -- so the Rust `fill_logic::finish_convert_stitch` port can be
// differentially checked against it.
//
// This mirrors ONLY the orchestration `finish_fill` performs around `convert_cluster_data`/
// `stitch_boundaries` -- which bytes get converted (`payload()`, not the trailing slack), which
// neighbour edges get built from where (a neighbour's OWN trailing-slack tail/head), and in what order
// (convert self, THEN stitch) -- not a re-derivation of either algorithm, which
// `tests/spec_audio_stream/{convert,stitch,convert_cluster}_spec.cpp` and `sample_convert`'s own suite
// already prove byte-identical against SIMDe. `prev_payload`/`next_payload` being null means
// "absent or not loaded" -- mirrors `prevCluster && prevCluster->loaded` / `nextCluster &&
// nextCluster->loaded` (async_fill.cpp:116-157); the caller (the Rust test driver) decides that, same
// as `fill_logic::finish_convert_stitch`'s own `Option<NeighbourView>` contract.
//
// Deliberately does NOT recompile convert.cpp/stitch.cpp/audio_format_helpers.cpp itself (see
// build.rs's doc for the full reasoning): `convert_cluster_data` is a template, so its instantiation
// here (with its own no-op Yield lambda, a distinct type from `deluge_sample_convert`'s own shim.cpp
// instantiation) is compiled straight into THIS translation unit's object -- no external symbol needed,
// and template instantiations are automatically COMDAT-deduplicated, so even an accidental duplicate
// instantiation elsewhere would be harmless. `stitch_boundaries` (a plain, non-template function
// declared in stitch.h) is NOT compiled here at all -- it resolves at link time against
// `deluge_sample_convert`'s ALREADY cc-compiled `stitch.cpp.o` (this crate depends on that crate as a
// normal Rust dependency, so its archive is already on this test binary's link line). Recompiling
// stitch.cpp (and convert.cpp, for the same reason -- `convert_word_in_place` inside the template needs
// the plain, non-template `convert_word` symbol) into a SECOND archive here would define those same
// externally-linked symbols twice, a genuine link-time "duplicate symbol" hazard -- not recompiling them
// sidesteps it entirely.
#include "storage/audio/stream/convert.h"
#include "storage/audio/stream/stitch.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

using namespace deluge::audio::stream;

extern "C" {

/// Mirrors `finish_fill`'s convert + neighbour-gather + stitch tail (async_fill.cpp:110-162) exactly,
/// minus the `loaded = true` / `deluge_resource_mark_ready` publish step (out of scope for the
/// fill-differential -- the host end-to-end test covers publish, over a real `deluge_resource`
/// manager).
///
/// @param self_data                             This cluster's FULL `payload_with_trailing_slack()`
///                                               span (`cluster_size + 7` bytes), mutated in place.
/// @param self_len                               `self_data`'s length (`cluster_size + 7`, asserted by
///                                               the safe Rust wrapper, not re-checked here).
/// @param cluster_index                          This cluster's index within the sample.
/// @param format                                 `RawDataFormat`'s `uint8_t` underlying value.
/// @param audio_data_start_pos_bytes             Byte offset of the sample's audio data in the file.
/// @param audio_data_length_bytes                Length of the sample's audio data, in bytes.
/// @param first_cluster_index_with_no_audio_data `sample->getFirstClusterIndexWithNoAudioData()`.
/// @param cluster_size                           The cluster size, in bytes.
/// @param cluster_size_magnitude                 log2(cluster_size).
/// @param self_unconverted_head_out              Receives the pre-conversion first 3 bytes of
///                                               `self_data` (mirrors
///                                               `StreamedChunk::first_three_bytes_pre_data_conversion`).
/// @param self_start_converted                   In/out: this cluster's start-boundary flag.
/// @param self_end_converted                     In/out: this cluster's end-boundary flag.
/// @param prev_payload                           The previous cluster's OWN full trailing-slack span
///                                               (`cluster_size + 7` bytes), or nullptr if absent/not
///                                               loaded.
/// @param prev_payload_len                       `prev_payload`'s length (unused when null); kept for
///                                               symmetry with the Rust FFI wrapper's borrow-length
///                                               bookkeeping, not read here (the prev edge's 11-byte
///                                               tail slice is always `[cluster_size-4, cluster_size+7)`
///                                               of the neighbour's own buffer, same as `finish_fill`).
/// @param prev_end_converted                     In/out: the previous cluster's OWN end-boundary flag
///                                               (unused when `prev_payload` is null).
/// @param next_payload                           The next cluster's OWN full trailing-slack span, or
///                                               nullptr if absent/not loaded.
/// @param next_payload_len                       `next_payload`'s length (unused when null); same
///                                               symmetry note as `prev_payload_len`.
/// @param next_unconverted_head                  The next cluster's OWN pre-conversion first 3 bytes
///                                               (unused when `next_payload` is null).
/// @param next_start_converted                   In/out: the next cluster's OWN start-boundary flag
///                                               (unused when `next_payload` is null).
void region_fill_diff_finish_fill_over_buffers(
    std::byte* self_data, size_t self_len, int32_t cluster_index, uint8_t format, uint32_t audio_data_start_pos_bytes,
    uint64_t audio_data_length_bytes, int32_t first_cluster_index_with_no_audio_data, size_t cluster_size,
    size_t cluster_size_magnitude, std::byte* self_unconverted_head_out, bool* self_start_converted,
    bool* self_end_converted, std::byte* prev_payload, [[maybe_unused]] size_t prev_payload_len,
    bool* prev_end_converted, std::byte* next_payload, [[maybe_unused]] size_t next_payload_len,
    const std::byte* next_unconverted_head, bool* next_start_converted) {
	ConvertGeometry geometry{
	    .audio_data_start_pos_bytes = audio_data_start_pos_bytes,
	    .audio_data_length_bytes = audio_data_length_bytes,
	    .first_cluster_index_with_no_audio_data = first_cluster_index_with_no_audio_data,
	};

	// cluster.convert_data_if_necessary() (cluster.cpp:76-91): converts only payload() (cluster_size
	// bytes), never the trailing slack.
	std::span<std::byte, 3> head_out{self_unconverted_head_out, 3};
	convert_cluster_data(std::span<std::byte>{self_data, cluster_size}, cluster_index,
	                     static_cast<RawDataFormat>(format), geometry, cluster_size, cluster_size_magnitude, head_out,
	                     [] {});

	// Gather the neighbor edge spans exactly as async_fill.cpp:116-157 does: a null payload means
	// "absent or not loaded" (the caller's job to decide, mirroring `prevCluster && prevCluster->loaded`
	// / `nextCluster && nextCluster->loaded`).
	std::optional<StitchPrevEdge> prev_edge;
	if (prev_payload != nullptr) {
		prev_edge = StitchPrevEdge{
		    .tail = std::span<std::byte>(prev_payload + (cluster_size - 4), 11),
		    .end_boundary_converted = prev_end_converted,
		};
	}
	StitchPrevEdge* prev_ptr = prev_edge ? &*prev_edge : nullptr;

	std::optional<StitchNextEdge> next_edge;
	if (next_payload != nullptr) {
		next_edge = StitchNextEdge{
		    .head = std::span<std::byte>(next_payload, 7),
		    .unconverted_head = std::span<const std::byte, 3>(next_unconverted_head, 3),
		    .start_boundary_converted = next_start_converted,
		};
	}
	StitchNextEdge* next_ptr = next_edge ? &*next_edge : nullptr;

	// stitch_boundaries(self_span, ...) (async_fill.cpp:159-162): over the FULL
	// payload_with_trailing_slack() span.
	std::span<std::byte> self_span{self_data, self_len};
	stitch_boundaries(self_span, cluster_index, static_cast<RawDataFormat>(format), audio_data_start_pos_bytes,
	                  cluster_size, *self_start_converted, *self_end_converted, prev_ptr, next_ptr);
}

} // extern "C"
