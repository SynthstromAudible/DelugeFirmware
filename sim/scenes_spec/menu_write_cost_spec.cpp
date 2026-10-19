// The cost of a menu write through the app's sink, per target and scope: evidence for the one-frame budget a write on
// every encoder detent must meet on the device (step 2's decision 5). Prints MENU-WRITE-COST lines; the budget is
// judged from them (a wall-clock assertion would be as flaky as the machine running it).

#include "scenes/bind/write_sink.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "processing/sound/sound_drum.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

using namespace deluge::scenes;
using deluge::scenes::bind::Target;
using deluge::scenes::menu::MenuContext;

namespace {

/// The kit size the probe fans out over: a full kit's worth of sound rows.
constexpr int32_t kKitDrums = 16;
/// Writes timed per target and scope.
constexpr int32_t kWrites = 200;

struct Cost {
	double median_us;
	double p95_us;
};

/// Times @p kWrites writes of @p path through the app's sink, alternating two menu values.
Cost time_writes(const MenuContext& ctx, std::string_view path, int32_t a, int32_t b) {
	bind::WriteSink& sink = menu::host_write_sink();
	std::vector<double> us;
	us.reserve(kWrites);
	for (int32_t i = 0; i < kWrites; ++i) {
		const bind::WriteRequest request{.target = Target{.kind = Target::Kind::SOUND, .path = path},
		                                 .where = ctx.addresses,
		                                 .menu_value = (i % 2) != 0 ? a : b};
		const auto start = std::chrono::steady_clock::now();
		sink.write(request);
		us.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
	}
	std::ranges::sort(us);
	return {.median_us = us[us.size() / 2], .p95_us = us[(us.size() * 95) / 100]};
}

void report(const char* scope, int32_t drums, std::string_view path, Cost cost) {
	char line[160];
	snprintf(line, sizeof line, "MENU-WRITE-COST target=%.*s scope=%s drums=%d us_median=%.1f us_p95=%.1f\n",
	         static_cast<int>(path.size()), path.data(), scope, static_cast<int>(drums), cost.median_us, cost.p95_us);
	std::fputs(line, stdout);
}

} // namespace

// clang-format off
describe menu_write_cost("a menu write's cost through the app's sink", ${
	it("measures a choice, a combined menu and a reshaping write, on a synth and on a full kit under affect-entire", _ {
		{
			spec::SongFixture f = spec::make_synth_song();
			f.song->setCurrentClip(f.clip);
			const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
			report("synth", 0, "polyphonic", time_writes(ctx, "polyphonic", 1, 2));
			report("synth", 0, "transpose", time_writes(ctx, "transpose", 100, -100));
			report("synth", 0, "osc1/type", time_writes(ctx, "osc1/type", 1, 4));
		}
		{
			spec::SongFixture f = spec::make_kit_song();
			for (int32_t d = 0; d < kKitDrums; ++d) {
				spec::add_drum<SoundDrum>(f);
			}
			MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
			ctx.affect_entire_held = true;
			ctx.addresses.affect_entire = menu::bindings::writes_whole_kit(ctx);
			expect(ctx.addresses.affect_entire).to_be_true();
			report("kit", kKitDrums, "polyphonic", time_writes(ctx, "polyphonic", 1, 2));
			report("kit", kKitDrums, "transpose", time_writes(ctx, "transpose", 100, -100));
			report("kit", kKitDrums, "osc1/type", time_writes(ctx, "osc1/type", 1, 4));
		}
	});
});

DELUGE_SPEC(menu_write_cost_spec, menu_write_cost)
