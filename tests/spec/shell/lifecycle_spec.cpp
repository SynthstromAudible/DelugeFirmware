#include "shell/shell.h"
#include "support/fake_surfaces.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "../support/spec_main.h"
using namespace deluge::ui::shell;
using deluge::notify::Dirty;

namespace {

/// The kind the application's song-root scene registers under in these specs.
constexpr SceneKind kRoot = SceneKind::FirstApp;

/// @brief A BUTTON event at matrix (x, y), as `tick_spec.cpp`/`input_lowering_spec.cpp` build them.
DelugeInputEvent button_ev(uint8_t x, uint8_t y, bool on) {
	return DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)};
}

BoardProfile lifecycle_profile() {
	static constexpr SurfaceRole kRoles[] = {SurfaceRole::Main, SurfaceRole::Aux};
	static constexpr const deluge::ui::Style* kStyles[] = {&deluge::ui::kDelugeStyle48, &deluge::ui::kDelugeStyle64};
	BoardProfile p = uitest::test_profile_input();
	p.oled_role_by_index = kRoles;
	p.style_by_index = kStyles;
	return p;
}

/// @brief The application's song-root scene: records entry/exit/handled events so the song-swap
///        specs can see whether the shell reached it.
class RootScene final : public Scene {
public:
	SceneKind kind() const override { return kRoot; }
	Result handle(const UiEvent&, const UiContext&) override {
		events++;
		return Result::unhandled();
	}
	bool on_enter(const SceneId& id) override {
		enters++;
		last_arg = id.arg;
		return true;
	}
	void on_exit() override { exits++; }

	int enters = 0, exits = 0, events = 0;
	uint16_t last_arg = 0;
};

/// @brief The test registry's stand-in for spec E3's real `ScreensaverScene` (per this task's
///        controller ruling): records entry/exit so the idle specs can see it pushed and popped,
///        without drawing anything of its own.
class ScreensaverStub final : public Scene {
public:
	SceneKind kind() const override { return SceneKind::Screensaver; }
	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }
	bool on_enter(const SceneId& id) override {
		enters++;
		last_arg = id.arg;
		return true;
	}
	void on_exit() override { exits++; }

	int enters = 0, exits = 0;
	uint16_t last_arg = 0;
};

/// @param register_screensaver False leaves `SceneKind::Screensaver` unregistered -- the branch's
///        documented state until spec E3 writes the real animation, and the case the shell has to
///        survive without repainting every surface every tick.
std::array<Scene*, 18> table_for(Scene& root, Scene& screensaver, bool register_screensaver) {
	std::array<Scene*, 18> t{};
	t[static_cast<size_t>(kRoot)] = &root;
	if (register_screensaver) {
		t[static_cast<size_t>(SceneKind::Screensaver)] = &screensaver;
	}
	return t;
}

/// @brief A `Shell` over `FakeSurfaceSet`, with the application's root scene and (by default) a
///        screensaver stand-in registered. `cfg` is settable per spec (the idle specs need a
///        non-zero `screensaver_idle_ms`), so it is a constructor argument rather than a fixed
///        default, and so is whether the screensaver kind is registered at all.
struct Rig {
	BoardProfile profile = lifecycle_profile();
	RootScene root;
	ScreensaverStub screensaver;
	/// Declared before `table`, which is built from it.
	bool screensaver_registered;
	std::array<Scene*, 18> table;
	SceneRegistry reg{std::span<Scene* const>{table}};
	uitest::FakeSurfaceSet fake{uitest::kBoard2, profile};
	ShellConfig cfg;
	Shell shell{fake, profile, reg, Services{}, cfg};

	explicit Rig(ShellConfig c = {}, bool register_screensaver = true)
	    : screensaver_registered(register_screensaver), table(table_for(root, screensaver, register_screensaver)),
	      cfg(c) {}

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
};

/// A fixed `root_for_song` the song-swap specs share: always the application's one root scene,
/// carrying `arg` as the "which song" marker so a test can tell a fresh `on_enter` apart from the
/// one `Shell::init` did at start of day.
SceneId root_for_song(void*) {
	return SceneId{kRoot, 42};
}

} // namespace

// clang-format off
describe shell_lifecycle("shell::Shell lifecycle", $ {
	it("boot pushes nothing and the first tick still blits every surface", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.boot();
		// **Nothing on the stack, deliberately.** `boot()` used to push a "DELUGE" + version splash;
		// the port must not add UI the instrument did not have, and legacy's firmware draws nothing
		// before its first root frame (`Shell::boot`'s doc). An empty stack reports the `Null` scene.
		expect(r.shell.stack().depth()).to_equal(size_t(0));
		expect(r.shell.stack().top().kind() == SceneKind::Null).to_be_true();

		r.shell.tick(0);
		// `boot()` requested a repaint on every surface, so the very first tick draws and blits
		// both of `kBoard2`'s OLED panels regardless of any dirty fact.
		expect(r.fake.blits.size()).to_equal(size_t(2));
		expect(r.fake.oled()[0].blit_pending).to_be_false();
		expect(r.fake.oled()[1].blit_pending).to_be_false();
	});

	it("screensaver pushes after idle and pops on activity", _ {
		deluge::notify::dirty().drain();
		Rig r{ShellConfig{.screensaver_idle_ms = 1000, .seed = 7}};
		r.shell.init(SceneId{kRoot, 0});

		// Idle counts from the first tick (no prior activity was ever noted).
		r.shell.tick(0);
		expect(r.shell.stack().top().kind() == kRoot).to_be_true();
		r.shell.tick(500);
		expect(r.shell.stack().top().kind() == kRoot).to_be_true();
		r.shell.tick(999);
		expect(r.shell.stack().top().kind() == kRoot).to_be_true();

		r.shell.tick(1000);
		expect(r.shell.stack().top().kind() == SceneKind::Screensaver).to_be_true();
		expect(r.screensaver.enters).to_equal(1);

		// Activity pops it, and the very event that woke it is still lowered and routed normally --
		// it is not swallowed by waking the screensaver.
		r.shell.on_board_event(button_ev(8, 3, true), 1010);
		r.shell.tick(1020);
		expect(r.shell.stack().top().kind() == kRoot).to_be_true();
		expect(r.screensaver.exits).to_equal(1);
		expect(r.root.events).to_equal(1);

		// Deterministic: a second service built from the same seed pushes with the same
		// instantiation argument at the same idle deadline.
		Rig r2{ShellConfig{.screensaver_idle_ms = 1000, .seed = 7}};
		r2.shell.init(SceneId{kRoot, 0});
		r2.shell.tick(0);
		r2.shell.tick(1000);
		expect(r2.shell.stack().top().kind() == SceneKind::Screensaver).to_be_true();
		expect(r2.screensaver.last_arg).to_equal(r.screensaver.last_arg);
	});

	it("an unregistered screensaver kind latches the service off instead of retrying every tick", _ {
		deluge::notify::dirty().drain();
		// No `SceneKind::Screensaver` in the registry -- the branch's documented state until spec
		// E3 lands the real animation -- with the idle timer switched on, which is what an
		// application setting `screensaver_idle_ms` today actually gets.
		Rig r{ShellConfig{.screensaver_idle_ms = 1000, .seed = 7}, /*register_screensaver=*/false};
		r.shell.init(SceneId{kRoot, 0});
		r.shell.tick(0);
		const size_t blits_after_first_frame = r.fake.blits.size();
		expect(r.shell.screensaver_push_refused()).to_equal(uint32_t(0));

		// The idle deadline: one refused push, counted, and then the service is done.
		r.shell.tick(1000);
		expect(r.shell.stack().top().kind() == kRoot).to_be_true();
		expect(r.shell.screensaver_push_refused()).to_equal(uint32_t(1));

		// Ninety more ticks past the (permanently past) deadline. Nothing repaints: the refused
		// push neither flags the stack as changed nor is retried, so no gate is armed and no frame
		// is drawn or blitted.
		for (uint32_t t = 1010; t <= 1900; t += 10) {
			r.shell.tick(t);
		}
		expect(r.fake.blits.size()).to_equal(blits_after_first_frame);
		expect(r.shell.screensaver_push_refused()).to_equal(uint32_t(1));
		// And the scheduler is free to sleep: `next_wake()` is not pinned to now by a service
		// reporting a deadline it can never satisfy.
		expect(r.shell.next_wake().has_value()).to_be_false();
	});

	it("set_screensaver_idle_ms is the port of settingsChanged: it arms, re-arms and switches off", _ {
		deluge::notify::dirty().drain();
		// **The state an application actually starts in**, and the reason this setter exists: a host
		// built at static-init cannot read flash, so its `ShellConfig` carries the 0 default and the
		// screensaver can never show however the two settings are set (`shell/shell.h`'s
		// `screensaver_idle_ms`). Driven from that state rather than from a rig already armed, so
		// the arm below cannot be one the constructor made.
		Rig r{ShellConfig{.seed = 7}};
		r.shell.init(SceneId{kRoot, 0});
		r.shell.tick(0);
		r.shell.tick(100000); // a hundred seconds of idle at "off"
		expect(r.shell.stack().top().kind() == kRoot).to_be_true();
		expect(r.screensaver.enters).to_equal(0);
		expect(r.shell.next_wake().has_value()).to_be_false();

		// `Screensaver::settingsChanged`'s `arm()` arm (`hid/display/screensaver.cpp`): the deadline
		// is re-armed from *now*, which for this layer is the last tick's clock -- 100000, not 0.
		// Asserted through `next_wake()` before the push, so the example is about the arm and not
		// only about the eventual appearance.
		r.shell.set_screensaver_idle_ms(1000);
		expect(r.shell.screensaver_idle_ms()).to_equal(uint32_t(1000));
		r.shell.tick(100010);
		expect(r.shell.next_wake().has_value()).to_be_true();
		expect(*r.shell.next_wake()).to_equal(uint32_t(101000));

		r.shell.tick(101000);
		expect(r.shell.stack().top().kind() == SceneKind::Screensaver).to_be_true();
		expect(r.screensaver.enters).to_equal(1);

		// The OFF arm (`:171-174`'s `unsetTimer`), and legacy's ordering: the wake is unconditional
		// and happens *before* the OFF test, so switching the screensaver off while it is showing
		// takes it down rather than stranding the panel.
		r.shell.set_screensaver_idle_ms(0);
		expect(r.shell.screensaver_idle_ms()).to_equal(uint32_t(0));
		expect(r.shell.stack().top().kind() == kRoot).to_be_true();
		expect(r.screensaver.exits).to_equal(1);
		r.shell.tick(101010);
		expect(r.shell.next_wake().has_value()).to_be_false();
		r.shell.tick(999999);
		expect(r.screensaver.enters).to_equal(1);
	});

	it("a re-supplied timeout takes effect at the new period, not the old one", _ {
		deluge::notify::dirty().drain();
		// The half of the settings change a "did it arm?" example cannot see: the *value* has to be
		// the new one. Two rigs, same script, only the second number varied -- so an implementation
		// that re-armed at whatever it already held passes neither.
		const auto pushes_at = [](uint32_t first_ms, uint32_t second_ms) {
			Rig r{ShellConfig{.screensaver_idle_ms = first_ms, .seed = 7}};
			r.shell.init(SceneId{kRoot, 0});
			r.shell.tick(0);
			r.shell.set_screensaver_idle_ms(second_ms);
			for (uint32_t t = 100; t <= 20000; t += 100) {
				r.shell.tick(t);
				if (r.shell.stack().top().kind() == SceneKind::Screensaver) {
					return t;
				}
			}
			return uint32_t(0);
		};
		expect(pushes_at(1000, 5000)).to_equal(uint32_t(5000));
		expect(pushes_at(1000, 15000)).to_equal(uint32_t(15000));
		// And the first number really was different, so "it pushed at the second" is not "it pushed
		// at the only one there ever was".
		expect(pushes_at(1000, 1000)).to_equal(uint32_t(1000));
	});

	it("the screensaver idle deadline survives the uint32 clock wrap", _ {
		deluge::notify::dirty().drain();
		Rig r{ShellConfig{.screensaver_idle_ms = 1000, .seed = 7}};
		r.shell.init(SceneId{kRoot, 0});

		// 256 ms before the millisecond clock wraps, so every deadline armed below lands on the
		// far side of it. An absolute `now_ms >= deadline_ms_` reads those wrapped (tiny) deadlines
		// as long past and pushes the screensaver over the view the user is actively touching.
		constexpr uint32_t kNearWrap = 0xFFFFFF00u;
		r.shell.tick(kNearWrap);
		r.shell.on_board_event(button_ev(8, 3, true), kNearWrap + 10); // arms kNearWrap + 1010
		r.shell.tick(kNearWrap + 20);
		expect(r.shell.stack().top().kind() == kRoot).to_be_true();
		// The idle period is still 1000 ms away, so that is what the shell asks to be woken at --
		// the wrapped value, not 0 and not the pre-wrap arithmetic's answer.
		expect(r.shell.next_wake().has_value()).to_be_true();
		expect(*r.shell.next_wake()).to_equal(static_cast<uint32_t>(kNearWrap + 1010));

		r.shell.tick(static_cast<uint32_t>(kNearWrap + 500)); // past the wrap, short of the deadline
		expect(r.shell.stack().top().kind() == kRoot).to_be_true();
		expect(r.screensaver.enters).to_equal(0);

		r.shell.tick(static_cast<uint32_t>(kNearWrap + 1010)); // the deadline itself
		expect(r.shell.stack().top().kind() == SceneKind::Screensaver).to_be_true();
		expect(r.screensaver.enters).to_equal(1);
	});

	it("song_loading(true) resets to Loading and locks input; (false) restores the song root", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init(SceneId{kRoot, 0});
		r.shell.set_song_hooks(SongHooks{.root_for_song = &root_for_song, .ctx = nullptr});
		r.shell.tick(0);
		expect(r.root.enters).to_equal(1);

		r.shell.song_loading(true);
		expect(r.shell.stack().top().kind() == SceneKind::Loading).to_be_true();
		expect(r.shell.lowering().input_locked()).to_be_true();
		expect(r.root.exits).to_equal(1);

		// Dropped at the lowering level while the input lock is held, so no scene ever sees it.
		r.shell.on_board_event(button_ev(8, 3, true), 10);
		r.shell.tick(10);
		expect(r.root.events).to_equal(0);

		r.shell.song_loading(false);
		expect(r.shell.stack().top().kind() == kRoot).to_be_true();
		expect(r.shell.lowering().input_locked()).to_be_false();
		expect(r.root.enters).to_equal(2);
		expect(r.root.last_arg).to_equal(uint16_t(42));

		// With no hooks set, `(false)` pops Loading and stops: the stack is left empty (NullScene),
		// not resurrecting some other root on its own.
		Rig unhooked;
		unhooked.shell.init(SceneId{kRoot, 0});
		unhooked.shell.tick(0);
		unhooked.shell.song_loading(true);
		unhooked.shell.song_loading(false);
		expect(unhooked.shell.stack().depth()).to_equal(size_t(0));
		expect(unhooked.shell.stack().top().kind() == SceneKind::Null).to_be_true();
	});

	it("Dirty::SongSwapped replaces the root", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init(SceneId{kRoot, 0});
		r.shell.set_song_hooks(SongHooks{.root_for_song = &root_for_song, .ctx = nullptr});
		r.shell.tick(0);
		expect(r.root.enters).to_equal(1);

		deluge::notify::dirty().raise(Dirty::SongSwapped);
		r.shell.tick(10);
		expect(r.shell.stack().top().kind() == kRoot).to_be_true();
		expect(r.root.exits).to_equal(1);
		expect(r.root.enters).to_equal(2);
		expect(r.root.last_arg).to_equal(uint16_t(42));

		// With no hooks set, a bare SongSwapped is a no-op: the stack is untouched.
		deluge::notify::dirty().drain();
		Rig unhooked;
		unhooked.shell.init(SceneId{kRoot, 0});
		unhooked.shell.tick(0);
		deluge::notify::dirty().raise(Dirty::SongSwapped);
		unhooked.shell.tick(10);
		expect(unhooked.shell.stack().top().kind() == kRoot).to_be_true();
		expect(unhooked.root.exits).to_equal(0);
	});
});
// clang-format on

DELUGE_SPEC(shell_lifecycle_spec, shell_lifecycle)
