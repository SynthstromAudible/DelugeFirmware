#include "plugin-api/types/plugin_id.h"
#include "plugin-api/types/sync.h"

#include <cppspec.hpp>

using namespace deluge::plugin;

// clang-format off
describe plugin_id("a plugin's id and the sync values its state stores", ${
	it("names the Deluge instrument", _ {
		expect(kDelugeInstrument).to_equal(PluginId{"deluge.instrument"});
		expect(kDelugeInstrument).not_().to_equal(PluginId{"deluge.clip-player"});
	});

	it("stores sync levels relative to the tick magnitude, and reads them back", _ {
		expect(sync_level_to_file(0, 3)).to_equal(0);
		expect(sync_level_to_file(4, 1)).to_equal(4);
		expect(sync_level_to_file(4, 3)).to_equal(6);
		expect(sync_level_from_file(6, 3)).to_equal(4);
		expect(sync_level_from_file(12, 1)).to_equal(9);
	});
});

CPPSPEC_SPEC(plugin_id)
