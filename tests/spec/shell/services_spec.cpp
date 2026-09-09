#include "shell/shell.h"
#include "support/fake_surfaces.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "../support/spec_main.h"
using namespace deluge::ui::shell;

namespace {

/// The kind the one test scene registers under.
constexpr SceneKind kBase = SceneKind::FirstApp;

/// `Mod0`'s seat in `uitest::test_profile_input`'s button matrix (`fake_surfaces.h`'s `/*y0*/`
/// row, second column) -- the one event shape that reaches the `mod_knobs` step at all
/// (`Router::route`, `router.h`'s "only consulted when \p event is a mod button ..." note).
constexpr uint8_t kMod0X = 1;
constexpr uint8_t kMod0Y = 0;
/// `Play`'s seat in the same matrix (`/*y3*/`, ninth column): an ordinary button, so the chain
/// runs scene -> global_controls -> midi_learn and skips `mod_knobs`.
constexpr uint8_t kPlayX = 8;
constexpr uint8_t kPlayY = 3;

/// @brief The board `test_profile_input`'s matrix is paired with here: `kBoard2`'s two OLEDs.
///
/// Same construction as `tick_spec.cpp`'s `tick_profile` and `flush_spec.cpp`'s `flush_profile`;
/// the surfaces are incidental to this file (nothing here asserts on a pixel), but `Shell::tick`
/// runs its whole render pass every tick and needs somewhere to draw.
BoardProfile services_profile() {
	static constexpr SurfaceRole kRoles[] = {SurfaceRole::Main, SurfaceRole::Aux};
	static constexpr const deluge::ui::Style* kStyles[] = {&deluge::ui::kDelugeStyle48, &deluge::ui::kDelugeStyle64};
	BoardProfile p = uitest::test_profile_input();
	p.oled_role_by_index = kRoles;
	p.style_by_index = kStyles;
	return p;
}

/// @brief A scene that declines every event, so the routing chain always runs past it into the
///        services -- which is the whole of what this file is about.
class QuietScene final : public Scene {
public:
	SceneKind kind() const override { return kBase; }
	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }
};

std::array<Scene*, 18> table_for(Scene& base) {
	std::array<Scene*, 18> t{};
	t[static_cast<size_t>(kBase)] = &base;
	return t;
}

/// @brief A `Service` that counts every hook the shell reaches it through, and logs its name when
///        it is routed to.
///
/// The four counters are the four hooks a `Shell` reaches a service through: `handle()` via
/// `Router` (`shell.cpp:586`'s `router_.route`), and `tick()`, `leds()` and `next_timer()` via
/// `service_list_` (`Shell::tick_stack` at `:602`, `Shell::derive_leds` -> `LedCompose::compose` at
/// `:877`, `Shell::compute_next_wake` at `:1090`). The list has a fourth reader, `Shell::tick`'s
/// step 8c at `:512`, which also calls `next_timer()` -- but only for a service whose
/// `timer_repaints()` claims a surface, and this double leaves that hook at its `{false, false}`
/// default, so it contributes no count here. A service installed correctly is reachable through all
/// four hooks; the dedup rule is that a service in two slots is reached through each of the last
/// three exactly once per tick.
class CountingService final : public Service {
public:
	CountingService(std::vector<std::string>* log, std::string name) : log_(log), name_(std::move(name)) {}

	Result handle(const UiEvent&, const UiContext&) override {
		handles++;
		log_->push_back(name_);
		return Result::unhandled();
	}
	void leds(deluge::ui::LedFrame&) override { led_calls++; }
	void tick(uint32_t, uint32_t) override { ticks++; }
	std::optional<uint32_t> next_timer() const override {
		timer_queries++;
		return std::nullopt;
	}

	int handles = 0;
	int led_calls = 0;
	int ticks = 0;
	mutable int timer_queries = 0;

private:
	std::vector<std::string>* log_;
	std::string name_;
};

/// A `DELUGE_EVENT_BUTTON` at matrix (x, y), the same shorthand `router_spec.cpp` uses.
DelugeInputEvent board_button(uint8_t x, uint8_t y, bool on) {
	return DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)};
}

/// @brief A `Shell` built with `Services{}` -- an application that has installed nothing -- plus
///        three services standing by, unwired.
///
/// This is the shape every application in the tree was stuck in before `set_services`, and the one
/// it exists to get out of: `services::ModKnobs`, `GlobalControls` and `MidiLearn` are each built
/// from a `SceneStack&` (and, for two of them, an `InputLowering&`) that only exists once the
/// `Shell` owning it does, so `Services{}` was the only thing an application could pass.
struct Rig {
	std::vector<std::string> log;
	BoardProfile profile = services_profile();
	QuietScene base;
	std::array<Scene*, 18> table = table_for(base);
	SceneRegistry reg{std::span<Scene* const>{table}};
	uitest::FakeSurfaceSet fake{uitest::kBoard2, profile};
	Shell shell{fake, profile, reg, Services{}, ShellConfig{}};
	CountingService mod_knobs{&log, "mod"};
	CountingService global_controls{&log, "global"};
	CountingService midi_learn{&log, "learn"};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	Rig() = default;

	/// @brief Press one matrix button and run the tick that routes it.
	void press(uint8_t x, uint8_t y, uint32_t now_ms) {
		shell.on_board_event(board_button(x, y, true), now_ms);
		shell.tick(now_ms);
	}
};

/// @brief `Rig` with the screensaver armed and no `SceneKind::Screensaver` scene registered.
///
/// `Screensaver` is one of the two services the **shell itself** puts in `service_list_`
/// (alongside `Notices`), and `push_refused()` is the only one of the two whose ticking is
/// observable from outside without a model: with `idle_ms > 0` and an empty screensaver slot, the
/// deadline comes and the push is refused exactly once, and it takes a `Screensaver::tick` to get
/// there (`shell/screensaver.h`'s note on `push_refused`). So it is the check that a rebuilt
/// `service_list_` still carries the shell's own two.
struct ScreensaverRig {
	BoardProfile profile = services_profile();
	QuietScene base;
	std::array<Scene*, 18> table = table_for(base);
	SceneRegistry reg{std::span<Scene* const>{table}};
	uitest::FakeSurfaceSet fake{uitest::kBoard2, profile};
	Shell shell{fake, profile, reg, Services{}, ShellConfig{.screensaver_idle_ms = 1000}};

	ScreensaverRig(const ScreensaverRig&) = delete;
	ScreensaverRig& operator=(const ScreensaverRig&) = delete;
	ScreensaverRig() = default;
};

} // namespace

// clang-format off
describe shell_services("shell::Shell::set_services", $ {
	it("routes to services installed after construction", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);

		// Nothing installed: the chain runs the scene and then three `null_service()` steps, and
		// the event is dropped.
		r.press(kPlayX, kPlayY, 10);
		expect(r.log.empty()).to_be_true();

		r.shell.set_services(Services{&r.mod_knobs, &r.global_controls, &r.midi_learn});

		// An ordinary button: scene (declines) -> global_controls -> midi_learn. `mod_knobs` is
		// not in this chain -- `Router::route` consults it only for mod buttons and gold knobs.
		r.press(kPlayX, kPlayY, 20);
		expect(r.log).to_equal(std::vector<std::string>{"global", "learn"});
		expect(r.mod_knobs.handles).to_equal(0);

		// A mod button: the full four-step chain, `mod_knobs` between the scene and the tail.
		r.log.clear();
		r.press(kMod0X, kMod0Y, 30);
		expect(r.log).to_equal(std::vector<std::string>{"mod", "global", "learn"});
	});

	it("ticks, lights and polls the timer of every installed service", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);
		expect(r.mod_knobs.ticks).to_equal(0);

		r.shell.set_services(Services{&r.mod_knobs, &r.global_controls, &r.midi_learn});
		r.shell.tick(10);

		expect(r.mod_knobs.ticks).to_equal(1);
		expect(r.global_controls.ticks).to_equal(1);
		expect(r.midi_learn.ticks).to_equal(1);
		expect(r.mod_knobs.led_calls).to_equal(1);
		expect(r.global_controls.led_calls).to_equal(1);
		expect(r.midi_learn.led_calls).to_equal(1);
		// Exactly one `next_timer()` per tick, and from one caller: `Shell::compute_next_wake`'s
		// unguarded loop over `service_list_` (`shell.cpp:1090`). `Shell::tick`'s step 8c reads the
		// same list (`:512`) but asks `next_timer()` only for a service whose `timer_repaints()`
		// claims a surface, and `Service::timer_repaints()`'s default claims none.
		expect(r.mod_knobs.timer_queries).to_equal(1);
	});

	it("lists a service occupying two slots exactly once", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);

		// The dedup rule `Shell`'s constructor has always had, now on the re-install path: one
		// object in two slots is ticked, asked for LEDs and asked for its timer once.
		r.shell.set_services(Services{&r.mod_knobs, &r.mod_knobs, &r.midi_learn});
		r.shell.tick(10);

		expect(r.mod_knobs.ticks).to_equal(1);
		expect(r.mod_knobs.led_calls).to_equal(1);
		expect(r.mod_knobs.timer_queries).to_equal(1);
		expect(r.midi_learn.ticks).to_equal(1);

		// It is still routed to twice, because routing reads the three `Services` slots and not
		// the deduplicated list -- the dedup is about per-tick work, not about the chain's shape.
		r.press(kMod0X, kMod0Y, 20);
		expect(r.log).to_equal(std::vector<std::string>{"mod", "mod", "learn"});
	});

	it("installing the same services twice does not double-tick", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);

		const Services services{&r.mod_knobs, &r.global_controls, &r.midi_learn};
		r.shell.set_services(services);
		r.shell.set_services(services);
		r.shell.tick(10);

		expect(r.mod_knobs.ticks).to_equal(1);
		expect(r.global_controls.ticks).to_equal(1);
		expect(r.midi_learn.ticks).to_equal(1);
		expect(r.mod_knobs.led_calls).to_equal(1);
	});

	it("installing over a live set leaves none of the old ones behind", _ {
		deluge::notify::dirty().drain();
		Rig r;
		std::vector<std::string> second_log;
		CountingService replacement{&second_log, "replacement"};
		r.shell.init({kBase, 0});
		r.shell.tick(0);

		r.shell.set_services(Services{&r.mod_knobs, &r.global_controls, &r.midi_learn});
		r.shell.tick(10);
		expect(r.mod_knobs.ticks).to_equal(1);

		r.shell.set_services(Services{&replacement, &replacement, &replacement});
		r.shell.tick(20);

		// The old three are gone from the list -- not merely joined by the new one.
		expect(r.mod_knobs.ticks).to_equal(1);
		expect(r.global_controls.ticks).to_equal(1);
		expect(r.midi_learn.ticks).to_equal(1);
		expect(r.mod_knobs.led_calls).to_equal(1);
		// ... and the replacement, in all three slots, is listed once.
		expect(replacement.ticks).to_equal(1);
		expect(replacement.led_calls).to_equal(1);

		// The routing chain moved with it: the old three are off the chain entirely.
		r.log.clear();
		r.press(kMod0X, kMod0Y, 30);
		expect(r.log.empty()).to_be_true();
		expect(second_log).to_equal(std::vector<std::string>{"replacement", "replacement", "replacement"});
	});

	it("installing Services{} again puts the shell back to null services", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);

		r.shell.set_services(Services{&r.mod_knobs, &r.global_controls, &r.midi_learn});
		r.shell.tick(10);
		r.shell.set_services(Services{});
		r.shell.tick(20);

		expect(r.mod_knobs.ticks).to_equal(1);
		expect(r.global_controls.ticks).to_equal(1);
		expect(r.midi_learn.ticks).to_equal(1);

		r.log.clear();
		r.press(kMod0X, kMod0Y, 30);
		expect(r.log.empty()).to_be_true();
	});

	it("skips a null slot rather than listing it", _ {
		deluge::notify::dirty().drain();
		Rig r;
		r.shell.init({kBase, 0});
		r.shell.tick(0);

		// `Services{}`'s slots default to `null_service()`, but nothing stops a caller writing a
		// literal null pointer into one; the constructor's list build has always skipped those and
		// the rebuild must too, or the first `service->tick()` of the next tick dereferences it.
		r.shell.set_services(Services{.mod_knobs = nullptr, .global_controls = &r.global_controls,
		                              .midi_learn = nullptr});
		r.shell.tick(10);
		r.shell.tick(20);

		expect(r.global_controls.ticks).to_equal(2);
		expect(r.mod_knobs.ticks).to_equal(0);
	});

	it("keeps the shell's own two services in the rebuilt list", _ {
		deluge::notify::dirty().drain();
		ScreensaverRig r;
		std::vector<std::string> log;
		CountingService installed{&log, "installed"};
		r.shell.init({kBase, 0});
		r.shell.tick(0);

		r.shell.set_services(Services{&installed, &installed, &installed});

		// `Screensaver` is not one of the three slots -- the shell appends it (and `Notices`) to
		// `service_list_` itself -- so a rebuild that only re-listed the three would silently stop
		// ticking both. With `idle_ms = 1000` and no screensaver scene registered, the idle
		// deadline is reached and the push refused, which only a ticked `Screensaver` can do.
		expect(r.shell.screensaver_push_refused()).to_equal(uint32_t(0));
		r.shell.tick(2000);
		expect(r.shell.screensaver_push_refused()).to_equal(uint32_t(1));
		expect(installed.ticks).to_equal(1);
	});
});
// clang-format on

DELUGE_SPEC(shell_services_spec, shell_services)
