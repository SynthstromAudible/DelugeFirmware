#pragma once

/// @brief The ten `MenuContext`s `deluge_menu_dump`'s `main()` (`menu_dump_main.cpp`) needs to
///        resolve a `dynamic_children` node under -- Task 8's fixtures, built directly (this is
///        not a spec, so there is no CppSpec driver to lean on).
///
/// Declared here (an allowed include: `scenes/menu/context.h` only) rather than in
/// `menu_dump_main.cpp` itself so the implementation, which needs
/// `sim/scenes_spec/support/song_fixture.h`, can live outside `src/scenes/` --
/// `scripts/check_layer_rules.py`'s `scenes-includes-outside` rule forbids any file under
/// `src/scenes/**` from including anything outside `scenes/`, `shell/`, `ui/`, `gfx/`, `notify/`,
/// `libdeluge/`, `dsp/`, `io/`, `model/`, `modulation/`, `playback/`, `processing/`, `storage/` and
/// `gui/l10n/`, and `support/` (the scenes-spec test harness's own directory, not one of those
/// prefixes) is not on that list. `sim/scenes_spec/menu_dump_contexts.cpp` -- outside `src/scenes/`,
/// so unrestricted -- is where `#include "support/song_fixture.h"` actually happens.

#include "scenes/menu/context.h"

#include <string_view>
#include <vector>

namespace deluge::scenes::menu_dump {

/// @brief One of the ten `MenuContext::Root` contexts, labelled by its own enumerator's spelling
///        (the label a `"multicontext"` expansion's `"context"` field prints).
struct NamedContext {
	std::string_view name;
	deluge::scenes::menu::MenuContext ctx;
};

/// @brief Build the ten contexts, in `MenuContext::Root` enumerator order.
std::vector<NamedContext> build_contexts();

} // namespace deluge::scenes::menu_dump
