// tests/unit/sample_reader/sample_peek_spec.cpp
//
// deluge_sample_peek over a REAL, linked deluge_resource manager (unlike
// sample_reader_bridge_spec.cpp's deliberately-unregistered-asset scenario): this drives the
// manager C ABI directly (deluge_resource_request/_mark_ready) to build one resident-and-ready
// cluster, one resident-but-not-ready cluster, and one never-touched (absent) cluster, then
// asserts deluge_sample_peek's contract against all three. The frame->cluster mapping and
// window/advance/degrade logic already have exhaustive coverage in the reader crate's own test
// suite (crates/deluge_sample_reader/src/reader.rs's `mod tests`); this spec exists to prove the
// C-ABI wiring end to end, including the manager-handle resolution `mock_streaming_fill.cpp`'s
// `set_active_manager` now makes settable.
#include "mock_streaming_fill.h"

#include "deluge_resource.h"
#include "libdeluge/alloc.h"
#include "libdeluge/sample_reader.h"
#include "libdeluge/streaming_fill.h"

#include "cppspec.hpp"

#include <cstdint>
#include <cstdlib>

namespace {

constexpr uint32_t kClusterSize = 64;                          // bytes
constexpr uint32_t kStride = 2;                                // byte_depth 2 * num_channels 1
constexpr uint32_t kFramesPerCluster = kClusterSize / kStride; // 32

// A chunk's manager backing is a real `StreamedChunk`: a header (carrying, among other fields, the
// payload base `deluge_sample_peek` resolves through) followed by a front guard, and only then the
// cluster payload. So the backing is sized for all three, and construct defers to the same
// `deluge_streaming_chunk_construct` production registers before touching any payload byte -- a
// seeded `payload == backing` stand-in would leave peek reading its pointer out of payload bytes.
// Mirrors the Rust reader crate's own `real_chunk_construct`/`BACKING_SIZE` harness (reader.rs).
//
// `+ 7` is the trailing slack `deluge_sample_fill::native_finish` always touches; rounded up to a
// 16-byte multiple to match the slab's slot alignment. Derived from the real offset rather than a
// hand-rounded literal, so this never drifts out of sync with `StreamedChunk`'s own layout.
inline uint32_t backing_size() {
	uint32_t unrounded = deluge_streamed_chunk_payload_offset() + kClusterSize + 7;
	return (unrounded + 15) & ~15u;
}

// Deterministic per-cluster ramp -- byte `i` of cluster `index` is `(index*kClusterSize + i) mod
// 256` -- the same synthetic-fixture shape the Rust reader crate's own tests use
// (`expected_cluster_bytes` in reader.rs), so a resident-and-ready cluster's peeked bytes are
// independently verifiable here.
extern "C" inline void ramp_construct(void* ctx, void* owner, uint32_t index, void* dest) {
	deluge_streaming_chunk_construct(ctx, owner, index, dest);

	auto* bytes = static_cast<uint8_t*>(dest) + deluge_streamed_chunk_payload_offset();
	uint32_t base = index * kClusterSize;
	for (uint32_t i = 0; i < kClusterSize; i++) {
		bytes[i] = static_cast<uint8_t>(base + i);
	}
}

// A manager + one asset over 3 full clusters of geometry (indices 0/1/2): cluster 0 is
// requested+constructed+marked ready (resident-and-ready), cluster 1 is
// requested+constructed but deliberately never marked ready (resident-but-not-ready), and
// cluster 2 is never touched at all (a true residency miss).
class PeekFixture {
public:
	PeekFixture() {
		raw_ = std::malloc(kArenaSize);
		heap_ = deluge_heap_create(raw_, kArenaSize);
		mgr_ = deluge_resource_create(heap_, /*asset_capacity=*/4, /*chunk_capacity=*/8);
		set_active_manager(mgr_);

		asset_ = deluge_resource_define_asset(mgr_, /*owner=*/nullptr, /*materialize=*/nullptr,
		                                      /*on_evict=*/nullptr, /*ctx=*/nullptr, DELUGE_RESOURCE_COST_IO,
		                                      DELUGE_RESOURCE_BACKING_HEAP);
		deluge_resource_set_construct(mgr_, asset_, ramp_construct);

		DelugeStreamingFillContext ctx{};
		ctx.efatfs_handle = 0;
		ctx.audio_data_start_pos_bytes = 0;
		ctx.audio_data_length_bytes = 3 * kClusterSize; // 3 full clusters
		ctx.first_cluster_index_with_no_audio_data = -1;
		ctx.cluster_size = kClusterSize;
		ctx.cluster_size_magnitude = 6; // 2^6 == 64
		ctx.raw_data_format = 0;        // Native
		ctx.byte_depth = 2;
		ctx.num_channels = 1;
		deluge_streaming_set_fill_context(mgr_, asset_, ctx);

		// Cluster 0: resident and ready.
		void* c0 = deluge_resource_request(mgr_, asset_, /*index=*/0, backing_size());
		deluge_resource_mark_ready(mgr_, c0);

		// Cluster 1: resident, deliberately left NOT ready (no mark_ready call).
		deluge_resource_request(mgr_, asset_, /*index=*/1, backing_size());

		// Cluster 2 is never requested at all -- absent.
	}

	~PeekFixture() {
		set_active_manager(nullptr); // restore the default sample_reader_bridge_spec.cpp relies on
		std::free(raw_);
	}

	[[nodiscard]] uint32_t asset() const { return asset_; }

private:
	static constexpr size_t kArenaSize = 64u * 1024u;
	void* raw_ = nullptr;
	DelugeHeap* heap_ = nullptr;
	DelugeResource* mgr_ = nullptr;
	uint32_t asset_ = 0;
};

} // namespace

// clang-format off
describe sample_peek("deluge_sample_peek", $ {
	it("returns the resident-and-ready cluster's frames, matching the ramp, forward to its end", _{
		PeekFixture fx;
		DelugeFrameWindow w = deluge_sample_peek(fx.asset(), /*start_frame=*/0, /*direction=*/1);
		expect(w.frames != nullptr).to_be_true();
		expect(w.frame_count).to_equal(kFramesPerCluster);
		auto* bytes = static_cast<const uint8_t*>(w.frames);
		for (uint32_t i = 0; i < kClusterSize; i++) {
			expect(bytes[i]).to_equal(static_cast<uint8_t>(i));
		}
	});

	it("returns {NULL, 0} for a cluster that was never requested (absent)", _{
		PeekFixture fx;
		// Cluster 2's first frame: abs byte pos 128 -> frame 64 (stride 2).
		DelugeFrameWindow w = deluge_sample_peek(fx.asset(), /*start_frame=*/64, /*direction=*/1);
		expect(w.frames == nullptr).to_be_true();
		expect(w.frame_count).to_equal(0u);
	});

	it("returns {NULL, 0} for a cluster that is resident but not yet ready", _{
		PeekFixture fx;
		// Cluster 1's first frame: abs byte pos 64 -> frame 32 (stride 2).
		DelugeFrameWindow w = deluge_sample_peek(fx.asset(), /*start_frame=*/32, /*direction=*/1);
		expect(w.frames == nullptr).to_be_true();
		expect(w.frame_count).to_equal(0u);
	});
});

CPPSPEC_SPEC(sample_peek)
