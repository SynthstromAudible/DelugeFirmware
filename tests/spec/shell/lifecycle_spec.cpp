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

std::array<Scene*, 18> table_for(Scene& root, Scene& screensaver) {
	std::array<Scene*, 18> t{};
	t[static_cast<size_t>(kRoot)] = &root;
	t[static_cast<size_t>(SceneKind::Screensaver)] = &screensaver;
	return t;
}

/// @brief A `Shell` over `FakeSurfaceSet`, with the application's root scene and a screensaver
///        stand-in registered. `cfg` is settable per spec (the idle specs need a non-zero
///        `screensaver_idle_ms`), so it is a constructor argument rather than a fixed default.
struct Rig {
	BoardProfile profile = lifecycle_profile();
	RootScene root;
	ScreensaverStub screensaver;
	std::array<Scene*, 18> table = table_for(root, screensaver);
	SceneRegistry reg{std::span<Scene* const>{table}};
	uitest::FakeSurfaceSet fake{uitest::kBoard2, profile};
	ShellConfig cfg;
	Shell shell{fake, profile, reg, Services{}, cfg};

	explicit Rig(ShellConfig c = {}) : cfg(c) {}

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
	it("init pushes Boot and the first tick blits every surface", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.boot("v1.2.3");
		expect(r.shell.stack().top().kind() == SceneKind::Boot).to_be_true();

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
