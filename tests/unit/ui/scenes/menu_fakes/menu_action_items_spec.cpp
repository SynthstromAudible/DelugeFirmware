/// @file
/// @brief The menu's verb items (`menu/nodes/actions.cpp`) over the UI fake: a verb asks for its action when it is
///        entered, and the performance editor's page opens on the mode the UI port answers.

#include "ui/scenes/bind/ports/ui_facts.h"
#include "ui/scenes/menu/action.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_state.h"
#include "ui/scenes/menu/nodes/actions.h"

#include "fakes.h"

#include "cppspec.hpp"

#include <cstdint>

#include "support/spec_main.h"

using deluge::ui::scenes::bind::PerformanceEditing;
using deluge::ui::scenes::bind::test::FakeHost;
using deluge::ui::scenes::menu::MenuAction;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::MenuState;

namespace nodes = deluge::ui::scenes::menu::nodes;

// clang-format off
describe menu_action_items("scenes::menu::nodes -- the verb items", ${
	it("asks for its action when entered", _ {
		MenuState state{};
		nodes::kSetClipLengthItem.on_focus(MenuContext{}, state);
		expect(state.walk.pending_action.action).to_equal(MenuAction::SetClipLengthToSampleLength);
	});

	it("opens the performance editor on the mode the UI port answers, and on Disabled with no port", _ {
		FakeHost host;
		host.ui.performance = PerformanceEditing::Param;
		MenuContext ctx{};
		ctx.ports = host.ports();
		MenuState state{};
		nodes::kPerformEditorItem.on_focus(ctx, state);
		expect(int32_t{state.list[0].cursor}).to_equal(static_cast<int32_t>(PerformanceEditing::Param));

		nodes::kPerformEditorItem.on_focus(MenuContext{}, state);
		expect(int32_t{state.list[0].cursor}).to_equal(static_cast<int32_t>(PerformanceEditing::Disabled));
	});
});
// clang-format on

DELUGE_SPEC(menu_action_items_spec, menu_action_items)
