#include "engine-api/types/engine_id.h"
#include "engine-api/types/sync.h"

#include <cppspec.hpp>

using namespace deluge::engine;

// clang-format off
describe engine_id("an engine's id and the sync values its state stores", ${
	it("names the Deluge instrument", _ {
		expect(kDelugeInstrument == EngineId{"deluge.instrument"}).to_equal(true);
		expect(kDelugeInstrument == EngineId{"deluge.clip-player"}).to_equal(false);
	});

	it("stores sync levels relative to the tick magnitude, and reads them back", _ {
		expect(sync_level_to_file(0, 3)).to_equal(0);
		expect(sync_level_to_file(4, 1)).to_equal(4);
		expect(sync_level_to_file(4, 3)).to_equal(6);
		expect(sync_level_from_file(6, 3)).to_equal(4);
		expect(sync_level_from_file(12, 1)).to_equal(9);
	});
});

CPPSPEC_SPEC(engine_id)
