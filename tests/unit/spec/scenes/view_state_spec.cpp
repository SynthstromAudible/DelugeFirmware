#include "scenes/services/view_state.h"

#include "cppspec.hpp"

#include "../support/spec_main.h"

using namespace deluge::scenes::services;

namespace {

const WaveformAddress kRange{.kind = WaveformAddress::Kind::RANGE,
                             .slot = {deluge::project::SlotAddress::Kind::OUTPUT, 7},
                             .source = 1,
                             .range = 2};
const WaveformAddress kClip{.kind = WaveformAddress::Kind::AUDIO_CLIP, .clip = {9}};

} // namespace

// clang-format off
describe scenes_view_state("a scene's view state", ${
	it("remembers a view per address and forgets it for another song", _ {
		ViewStateStore store;
		const int song_a = 0;
		const int song_b = 0;
		store.forget_unless(&song_a);
		expect(store.sample_view(kRange).has_value()).to_be_false();
		store.set_sample_view(kRange, {.scroll = 100, .zoom = 4, .loop_locked = true});
		store.set_sample_view(kClip, {.scroll = 5, .zoom = 2, .loop_locked = false});
		store.set_last_range({deluge::project::SlotAddress::Kind::OUTPUT, 7}, 1, 3);
		const auto view = store.sample_view(kRange);
		expect(view.has_value() && view->scroll == 100 && view->zoom == 4 && view->loop_locked).to_be_true();
		expect(store.sample_view(kClip)->scroll).to_equal(int64_t{5});
		expect(store.last_range({deluge::project::SlotAddress::Kind::OUTPUT, 7}, 1)).to_equal(std::optional<uint16_t>{3});
		store.set_sample_view(kRange, {.scroll = 1, .zoom = 1, .loop_locked = false});
		expect(store.sample_view(kRange)->scroll).to_equal(int64_t{1});

		store.forget_unless(&song_b);
		expect(store.sample_view(kRange).has_value()).to_be_false();
		expect(store.last_range({deluge::project::SlotAddress::Kind::OUTPUT, 7}, 1).has_value()).to_be_false();
	});
});

// clang-format on

DELUGE_SPEC(scenes_view_state_spec, scenes_view_state)
