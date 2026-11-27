/// @file
/// @brief The scenes library with no app: every scene the registry builds is there, under its own kind.

#include "ui/scenes/registry.h"

#include "ui/shell/scene.h"

#include <cstddef>
#include <cstdint>

#include "support/spec_main.h"

using deluge::ui::shell::SceneKind;

// clang-format off
describe scenes_link("the scenes library with no app", ${
	it("registers each scene under the kind it reports", _ {
		const deluge::ui::shell::SceneRegistry registry = deluge::ui::scenes::scene_registry();
		std::size_t registered = 0;
		for (std::size_t i = 0; i < registry.by_kind.size(); ++i) {
			const deluge::ui::shell::Scene* scene = registry.by_kind[i];
			if (scene == nullptr) {
				continue;
			}
			++registered;
			expect(static_cast<std::size_t>(scene->kind())).to_equal(i);
		}
		// The menu, rename, the context menu, the browser, the marker editor, the recorder, the slicer, the audio clip
		// and the three overlays (the param popup, the count-in and the screensaver).
		expect(registered).to_equal(std::size_t{11});
	});
});
// clang-format on

DELUGE_SPEC(scenes_link_spec, scenes_link)
