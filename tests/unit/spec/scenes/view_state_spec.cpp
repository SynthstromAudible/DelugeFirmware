#include "scenes/services/view_state.h"

#include "cppspec.hpp"

#include "../support/spec_main.h"

using namespace deluge::scenes::services;

namespace {

const ViewKey kRange{.kind = ViewKey::Kind::RANGE, .id = 7};
const ViewKey kClip{.kind = ViewKey::Kind::AUDIO_CLIP, .id = 9};
const ViewKey kClone{.kind = ViewKey::Kind::AUDIO_CLIP, .id = 10};

} // namespace

// clang-format off
describe scenes_view_state("a scene's view state", ${
	it("remembers a view per address and forgets it for another song", _ {
		ViewStateStore store;
		constexpr uint64_t song_a = 1;
		constexpr uint64_t song_b = 2;
		store.forget_unless(song_a);
		expect(store.sample_view(kRange).has_value()).to_be_false();
		store.set_sample_view(kRange, {.scroll = 100, .zoom = 4, .loop_locked = true});
		store.set_sample_view(kClip, {.scroll = 5, .zoom = 2, .loop_locked = false});
		const auto view = store.sample_view(kRange);
		expect(view.has_value() && view->scroll == 100 && view->zoom == 4 && view->loop_locked).to_be_true();
		expect(store.sample_view(kClip)->scroll).to_equal(int64_t{5});
		store.set_sample_view(kRange, {.scroll = 1, .zoom = 1, .loop_locked = false});
		expect(store.sample_view(kRange)->scroll).to_equal(int64_t{1});

		store.forget_unless(song_b);
		expect(store.sample_view(kRange).has_value()).to_be_false();
	});

	it("keeps a view for its key alone, and tells a range's from a clip's of the same number", _ {
		ViewStateStore store;
		store.forget_unless(1);
		store.set_sample_view(kRange, {.scroll = 100, .zoom = 4, .loop_locked = true});
		expect(store.sample_view(kRange).has_value() && store.sample_view(kRange)->loop_locked).to_be_true();
		expect(store.sample_view(ViewKey{.kind = ViewKey::Kind::AUDIO_CLIP, .id = 7}).has_value()).to_be_false();
	});

	it("gives another key the scroll and zoom remembered for one, without the loop lock", _ {
		ViewStateStore store;
		store.forget_unless(1);
		store.set_sample_view(kClip, {.scroll = 5, .zoom = 2, .loop_locked = true});
		store.copy_view(kClip, kClone);
		const auto view = store.sample_view(kClone);
		expect(view.has_value() && view->scroll == 5 && view->zoom == 2 && !view->loop_locked).to_be_true();
		store.copy_view(kRange, ViewKey{.kind = ViewKey::Kind::RANGE, .id = 8}); // nothing remembered for kRange
		expect(store.sample_view(ViewKey{.kind = ViewKey::Kind::RANGE, .id = 8}).has_value()).to_be_false();
	});
});

// clang-format on

DELUGE_SPEC(scenes_view_state_spec, scenes_view_state)
