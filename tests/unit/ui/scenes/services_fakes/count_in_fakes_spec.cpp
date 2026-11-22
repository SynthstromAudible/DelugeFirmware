/// @file
/// @brief The count-in overlay (`overlay/count_in_scene.cpp`) over the transport port's fake: the digit it shows, the
///        removal it files when the count ends, and what it does with no transport installed.

#include "ui/scenes/overlay/count_in_scene.h"

#include "ui/shell/scene.h"

#include "fakes.h"

#include "cppspec.hpp"

#include <string>

#include "support/spec_main.h"

using deluge::ui::scenes::bind::test::FakeTransport;
using deluge::ui::scenes::overlay::CountInScene;
using deluge::ui::scenes::overlay::kCountInSceneKind;
using deluge::ui::shell::Action;
using deluge::ui::shell::Result;

// clang-format off
describe count_in_fakes("overlay::CountInScene over the transport's fake", ${
	it("shows the transport's count after a tick, and stops with one Remove when the count reaches zero", _ {
		FakeTransport transport;
		transport.count_in = 4;
		CountInScene scene;
		scene.set_transport(&transport);
		scene.tick(0);
		expect(scene.showing()).to_be_true();
		expect(std::string{scene.shown_text()}).to_equal("4");
		expect(scene.after_render().actions.size()).to_equal(0u);

		transport.count_in = 0;
		scene.tick(0);
		expect(scene.showing()).to_be_false();
		const Result removed = scene.after_render();
		expect(removed.actions.size()).to_equal(1u);
		if (removed.actions.size() == 1) {
			expect(removed.actions[0].kind).to_equal(Action::Kind::Remove);
			expect(removed.actions[0].pop_to).to_equal(kCountInSceneKind);
		}
		// The removal is filed once.
		expect(scene.after_render().actions.size()).to_equal(0u);
	});

	it("follows the transport's count as it changes", _ {
		FakeTransport transport;
		CountInScene scene;
		scene.set_transport(&transport);
		transport.count_in = 3;
		scene.tick(0);
		expect(std::string{scene.shown_text()}).to_equal("3");
		transport.count_in = 2;
		scene.tick(0);
		expect(scene.showing()).to_be_true();
		expect(std::string{scene.shown_text()}).to_equal("2");
	});

	it("never shows with no transport installed", _ {
		CountInScene scene;
		for (int i = 0; i < 3; ++i) {
			scene.tick(0);
			expect(scene.showing()).to_be_false();
			expect(scene.after_render().actions.size()).to_equal(0u);
		}
	});
});
// clang-format on

DELUGE_SPEC(count_in_fakes_spec, count_in_fakes)
