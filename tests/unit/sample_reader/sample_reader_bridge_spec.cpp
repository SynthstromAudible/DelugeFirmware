// tests/unit/sample_reader/sample_reader_bridge_spec.cpp
//
// SampleFrameReader RAII/move-semantics smoke over the REAL, linked deluge_sample_reader_* C-ABI.
// Wiring a fully manager-backed, byte-producing reader asset here would mean
// replicating chunk_residency.cpp's/async_fill.cpp's real Sample/StreamedChunk machinery inside a
// host test -- disproportionate to this task (the reader crate's OWN test suite already proves
// window()/advance()/lease correctness exhaustively; see crates/deluge_sample_reader/src/reader.rs's
// `mod tests`, 20+ cases including boundary straddle and degrade paths). Instead this exercises the
// bridge's real, linked lifecycle over an asset with NO registered fill-context -- `Reader::open`
// still runs for real (calls the real, linked `deluge_sample_reader_open`), but resolves `ok() ==
// false` immediately (mirrors reader.rs's own documented "no registered fill-context" case), so
// `window()`/`read()` short-circuit without ever touching a manager -- proving the wrapper compiles
// against the real ABI, forwards every call correctly, and its RAII/move semantics never
// double-close (a double-close would abort the process, failing this whole spec binary).
#include "mock_streaming_fill.h"

#include "model/sample/sample_reader_bridge.h"

#include "cppspec.hpp"

#include <array>
#include <cstddef>
#include <utility>

using deluge::sample::SampleFrameReader;

namespace {
// Never defined in any fill-context table this process registers -- Reader::open() resolves
// ok() == false for it, exactly like a reader opened before its sample's first stream use (see
// SampleFrameReader's header doc / the reader crate's own `Reader::open` doc).
constexpr uint32_t kUnregisteredSourceId = 0xDEADBEEFu;
} // namespace

// clang-format off
describe sample_reader_bridge("deluge::sample::SampleFrameReader", $ {
	it("opens, reports not-ok with an empty window, and closes cleanly over an unregistered asset", _{
		SampleFrameReader reader(kUnregisteredSourceId, /*start_frame=*/0, /*direction=*/1, DELUGE_READ_CACHED);
		DelugeFrameWindow w = reader.window();
		expect(w.frames == nullptr).to_be_true();
		expect(w.frame_count).to_equal(0u);
		expect(reader.ok()).to_be_false();
		// advance()/seek() must stay safe no-ops over an unresolved cursor.
		reader.advance(4);
		reader.seek(10);
		// Destructor runs at scope exit -- the real deluge_sample_reader_close.
	});

	it("move-constructs, nulling the source so its destructor becomes a no-op", _{
		SampleFrameReader a(kUnregisteredSourceId, 0, 1, DELUGE_READ_SCAN);
		SampleFrameReader b(std::move(a));
		// `a`'s destructor (a no-op close on its now-null handle) and `b`'s (the real close) both
		// run at scope exit; a double-close would abort the process before the spec can report.
		expect(b.ok()).to_be_false(); // still unregistered -- unaffected by the move itself
	});

	it("move-assigns, closing whatever the destination already held before stealing the source", _{
		SampleFrameReader a(kUnregisteredSourceId, 0, 1, DELUGE_READ_CACHED);
		SampleFrameReader b(kUnregisteredSourceId, 5, -1, DELUGE_READ_SCAN);
		b = std::move(a); // b's original (real) handle is closed here; a is left null.
		expect(b.ok()).to_be_false();
		// Both destructors run at scope exit: a's is a no-op, b's closes the handle it stole.
	});

	it("SampleFrameReader::read forwards to deluge_sample_read and returns 0 for an unresolved asset", _{
		std::array<std::byte, 64> dest{};
		uint32_t written = SampleFrameReader::read(kUnregisteredSourceId, 0, 16, dest.data(), dest.size());
		expect(written).to_equal(0u); // no registered geometry -- nothing to copy, per the header's contract
	});
});

CPPSPEC_SPEC(sample_reader_bridge)
