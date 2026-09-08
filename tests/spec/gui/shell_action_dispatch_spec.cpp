/// @file
/// @brief `ShellHostUI::timerCallback`'s poll of the menu's action outbox -- that it happens, how
///        often, on which paths, and after what.
///
/// **What this file can see, and what it deliberately cannot.** `deluge::gui::ui::dispatchShellActions()`
/// is stubbed here, in `gui/support/fake_legacy_ui.cpp`, on exactly the terms `wireShellGuestServices`
/// and the two `shellGuestMenuLearn*` hooks already are: the real one reads
/// `deluge::scenes::menu_scene()` and its arms name `performanceView`, `audioClipView`, `stemExport`
/// and two `gui/context_menu/` singletons, none of which exist in this suite's link closure
/// (`tests/spec/CMakeLists.txt:28-35`). So **nothing here asserts that a request is performed, or
/// even that the outbox is what gets read** -- that half is
/// `sim/scenes_spec/menu_actions_spec.cpp`'s four `gui::ui::dispatchShellActions` groups (the drain,
/// and one per verb body), in the binary that links the real scene.
///
/// What is left is the half that exists nowhere else: the call site. And it is the half that the
/// hazard is actually about. `MenuScene::take_action()` is "a one-slot mailbox with a silent
/// overwrite, not an exactly-once queue" and announces nothing --
/// "`after_render` returns no `Action` announcing that one is waiting, so nothing enforces the
/// polling: **poll after every tick, or requests are dropped or stale**"
/// (`scenes/menu/menu_scene.h:593-598`). Nothing in the shell, and nothing in the scene, can fail
/// when a tick skips the poll. Only these examples can.

#include "support/fake_legacy_hid.h"
#include "support/fake_legacy_ui.h"

#include "gui/ui/shell_host_ui.h"
#include "gui/ui_timer_manager.h"
#include "hid/button.h"
#include "shell/profiles/deluge.h"

#include "cppspec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "../support/spec_main.h"

using deluge::gui::ui::ShellHostUI;
using namespace deluge::ui::shell;

namespace {

constexpr SceneKind kBase = SceneKind::FirstApp;

/// @brief A scene that counts the three things an ordering question about the tick can be asked
///        against: what it drew, what it was handed, and whether its `after_render` has run.
///
/// Nothing here is a stand-in for `MenuScene`. It is a stand-in for *a scene inside `Shell::tick`*:
/// step 3 routes input to `handle()`, the render steps call `render_oled`, and step 7 calls
/// `after_render()` for every scene that rendered (`shell/shell.cpp:399-407`). A request is filed
/// from that last hook, which is why "did `after_render` already run when the poll happened" is the
/// question worth asking rather than "was a request waiting".
class ProbeScene final : public Scene {
public:
	SceneKind kind() const override { return kBase; }

	void render_oled(SurfaceRole, deluge::ui::oled::OledFrame& frame) override {
		renders++;
		// Real ink, so a tick that rendered is distinguishable from one that did not by something
		// other than this counter if an example ever needs it to be.
		frame.hline(3, 0, 100);
	}

	Result handle(const UiEvent&, const UiContext&) override {
		events++;
		if (remove_self_on_event) {
			// `MenuScene`'s answer to Back at depth 0 (`scenes/menu/menu_scene.cpp:1493-1498`),
			// reduced to the one thing this file needs: a scene that takes itself off the stack
			// from inside the tick's routing, which is what sends `timerCallback` down its early
			// `return`.
			Result r;
			r.handled = true;
			(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kBase});
			return r;
		}
		// Unhandled, so the event is delivered and nothing else about the tick changes.
		return Result::unhandled();
	}

	Result after_render() override {
		after_renders++;
		return {};
	}

	std::optional<uint32_t> next_timer() const override { return timer; }

	/// How many `render_oled` calls this scene has had.
	int renders = 0;
	/// How many events have been routed to it.
	int events = 0;
	/// How many `after_render()` calls it has had -- i.e. how many times step 7 has run for it.
	int after_renders = 0;
	/// Make `handle` take this scene off the stack, the way `MenuScene` answers Back at depth 0.
	bool remove_self_on_event = false;
	/// What `next_timer()` answers. `std::nullopt` -- `Scene`'s own default -- unless set.
	std::optional<uint32_t> timer{};
};

/// @brief A `ShellHostUI` over a one-entry table, with both sets of legacy fakes reset.
///
/// The same shape as `shell_host_ui_spec.cpp`'s `Rig` and for the same reason -- `fill()` is a
/// member-function call in the initialiser list so the table is populated before the guest's
/// constructor reads the registry.
struct Rig {
	ProbeScene base{};
	std::array<Scene*, kMaxSceneKinds> table{};
	SceneRegistry reg{std::span<Scene* const>{table}};
	ShellHostUI guest;

	Rig() : guest(fill(), kDelugeBoard, deluge_profile()) {
		fakelegacy::reset();
		fakelegacyui::reset();
	}
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	~Rig() {
		// The callback below captures this rig by reference and the fake that holds it outlives
		// every example. Clearing it here rather than at the end of the one example that sets it
		// means a later example cannot call a lambda over a destroyed `Rig` even if it forgets --
		// and `fakelegacyui::reset()` in the constructor is the belt to this file's braces.
		fakelegacyui::on_action_dispatch = nullptr;
	}

	const SceneRegistry& fill() {
		table[static_cast<size_t>(kBase)] = &base;
		return reg;
	}

	bool open() {
		guest.setInitialScene(SceneId{kBase, 0});
		return guest.opened();
	}
};

/// @brief Put the guest's clock at \p ms.
///
/// `ShellHostUI::nowMs()` is `getSystemTime() * 1000` truncated, and `getSystemTime()` is seconds.
/// The extra quarter-millisecond is `shell_host_ui_spec.cpp`'s, for its reason: `ms / 1000.0 *
/// 1000.0` is not guaranteed to land back on `ms` in a double, and one ulp low truncates to
/// `ms - 1`.
void set_clock_ms(uint32_t ms) {
	fakelegacyui::system_time_s = (static_cast<double>(ms) + 0.25) / 1000.0;
}

} // namespace

// clang-format off
describe shell_action_dispatch("gui::ui::dispatchShellActions -- the poll in the tick", $ {

	it("polls on a tick that routed nothing and rendered nothing, which is the whole hazard", _ {
		// `menu_scene.h:593-598`: no `Action` announces a waiting request, so a poll gated on
		// "something happened this tick" would look correct and would strand a request filed on the
		// tick before it. This is the example that says the poll is not gated on anything.
		Rig r;
		expect(r.open()).to_be_true();

		// One tick to settle the first frame. Asserted rather than assumed, because everything
		// below is "and now nothing renders" -- which a scene that never rendered at all would
		// satisfy for the wrong reason.
		set_clock_ms(10);
		(void)r.guest.timerCallback();
		expect(r.base.renders > 0).to_be_true();

		const int settled_renders = r.base.renders;
		fakelegacyui::action_dispatches = 0;

		for (uint32_t i = 0; i < 5; i++) {
			set_clock_ms(20 + i * 10);
			(void)r.guest.timerCallback();
		}

		// Nothing was fed, so nothing was routed; nothing asked for a repaint, so nothing was drawn.
		expect(r.base.events).to_equal(0);
		expect(r.base.renders).to_equal(settled_renders);
		// And the poll ran on every one of those five ticks regardless. Exactly five, not "at
		// least": a poll that ran twice per tick would read the outbox a second time and, once the
		// arms exist, perform nothing on the second read -- harmless today, but not what the one
		// call site claims.
		expect(fakelegacyui::action_dispatches).to_equal(5);
	});

	it("polls AFTER Shell::tick, so a request this tick's after_render filed is taken this tick", _ {
		// The ordering, and the reason for it: a request is filed by `MenuScene::after_render`,
		// which `Shell::tick` runs as its step 7 (`shell/shell.cpp:399-407`). A poll above
		// `shell_.tick` reads the outbox before step 7 can have filled it and is therefore always
		// one tick late -- delivered, per `menu_scene.h:594-598`, "against whatever state exists
		// then".
		//
		// The scene here files nothing (it cannot -- see this file's header). What it records is
		// the two facts that make "the poll was after the tick" true: routing had already happened,
		// and `after_render` had already run.
		Rig r;
		expect(r.open()).to_be_true();

		int events_at_poll = -1;
		int after_renders_at_poll = -1;
		fakelegacyui::on_action_dispatch = [&r, &events_at_poll, &after_renders_at_poll] {
			events_at_poll = r.base.events;
			after_renders_at_poll = r.base.after_renders;
		};

		// Fed, not actioned: the guest's input hooks only queue, and `Shell::pump_input` -- which
		// only `Shell::tick` calls -- is what routes (`shell/shell.cpp:329`). So at this point the
		// scene has seen nothing.
		(void)r.guest.buttonAction(deluge::hid::button::BACK, true, false);
		expect(r.base.events).to_equal(0);
		expect(r.base.after_renders).to_equal(0);

		set_clock_ms(10);
		(void)r.guest.timerCallback();

		expect(fakelegacyui::action_dispatches).to_equal(1);
		// Both are strictly stronger than "the poll happened": each is false if the call moves above
		// `shell_.tick(now)`, and neither can be satisfied by a poll that merely ran.
		expect(events_at_poll).to_equal(1);
		expect(after_renders_at_poll).to_equal(1);
	});

	it("polls on the tick that closes the guest, which returns before the re-arm", _ {
		// `timerCallback`'s close arm `return`s: it does not fall through to `rearmTimer`, because
		// `closeUI` unsets `TimerName::UI_SPECIFIC` itself. So "after the tick and before the
		// re-arm" is satisfied by two different lines, and only one of them is on every path. A
		// request filed earlier in this same tick and left in the one-slot outbox would sit there
		// until the guest was next opened AND ticked, and would then run against a song and a root
		// UI chosen since -- `menu_scene.h:594-596`'s "delivered whenever someone finally does".
		Rig r;
		expect(r.open()).to_be_true();
		r.base.remove_self_on_event = true;

		(void)r.guest.buttonAction(deluge::hid::button::BACK, true, false);
		expect(r.guest.shell().stack().depth()).to_equal(size_t(1));

		// Reset AFTER the press and before the tick, so both counts below are about the tick alone:
		// `buttonAction` arms `kMinRearmMs` on its way out (`armTickSoon`), which is correct and
		// would otherwise be what the "no re-arm" expectation saw.
		fakelegacyui::reset();
		set_clock_ms(10);
		expect(r.guest.timerCallback()).to_equal(ActionResult::DEALT_WITH);

		// The close really did happen on this tick -- otherwise this example is asserting the poll
		// on an ordinary tick and saying "close path" about it.
		expect(r.guest.shell().stack().depth()).to_equal(size_t(0));
		expect(fakelegacyui::closed.size()).to_equal(size_t(1));
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(-1));

		expect(fakelegacyui::action_dispatches).to_equal(1);
	});

	it("polls from the tick and from nowhere else -- no input hook polls", _ {
		// The complement of the ordering example. Routing happens in `Shell::tick` and nowhere else
		// (`shell/shell.cpp:579-589`), so a poll from an input hook would run *before* the press it
		// arrived with had been routed -- the same one-tick-late error as polling above the tick,
		// wearing a different hat. Five hooks, which is all of them
		// (`gui/ui/shell_host_ui.h`'s input section).
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::action_dispatches = 0;

		(void)r.guest.buttonAction(deluge::hid::button::BACK, true, false);
		(void)r.guest.padAction(0, 0, 64);
		(void)r.guest.horizontalEncoderAction(1);
		(void)r.guest.verticalEncoderAction(1, false);
		r.guest.selectEncoderAction(1);

		expect(fakelegacyui::action_dispatches).to_equal(0);

		// And the very next tick polls once, so the zero above is about *where* the poll is rather
		// than about a guest that had stopped polling.
		set_clock_ms(10);
		(void)r.guest.timerCallback();
		expect(fakelegacyui::action_dispatches).to_equal(1);
	});
});
// clang-format on

DELUGE_SPEC(gui_shell_action_dispatch_spec, shell_action_dispatch)
