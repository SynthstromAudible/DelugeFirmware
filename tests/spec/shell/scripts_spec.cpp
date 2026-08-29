#include "shell/shell.h"
#include "support/script_runner.h"
#include "ui/oled/text.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

#include "../support/spec_main.h"
using namespace deluge::ui::shell;

namespace {

/// The kind `DemoScene` registers under: the first an application may use.
constexpr SceneKind kDemo = SceneKind::FirstApp;

/// The three colours `DemoScene` paints its diagonal with, one per pad; picked distinct in every
/// channel so a `.state` line cannot be confused with its neighbour.
constexpr RGB kDemoColours[] = {RGB{200, 0, 0}, RGB{0, 180, 0}, RGB{0, 0, 160}};

/// @brief The stand-in application scene: a title, a three-pad diagonal, the `Play` LED, and one
///        navigation gesture (`Clip` pushes another copy of itself, one argument higher).
///
/// One instance covers every stack entry -- the registry holds scene *singletons*, so a second
/// push re-enters this same object with a new `SceneId` -- which is exactly why `arg_` is set in
/// `on_enter` rather than assumed.
class DemoScene final : public Scene {
public:
	SceneKind kind() const override { return kDemo; }

	bool on_enter(const SceneId& id) override {
		arg_ = id.arg;
		return true;
	}

	void render_oled(SurfaceRole role, deluge::ui::oled::OledFrame& frame) override {
		if (role != SurfaceRole::Main) {
			return;
		}
		deluge::ui::oled::title(frame, "DEMO");
		if (arg_ != 0) {
			const std::string n = std::to_string(static_cast<unsigned>(arg_));
			const deluge::gfx::oled::Rect b = frame.bounds();
			deluge::ui::oled::text(
			    frame, {0, b.h - 10, b.w, 10}, n,
			    deluge::ui::oled::TextStyle{deluge::gfx::oled::FontId::Apple7, deluge::ui::oled::Align::Centre});
		}
	}

	void render_grid(deluge::ui::grid::GridFrame& frame) override {
		for (int32_t i = 0; i < 3; i++) {
			(void)frame.pad(i, i, kDemoColours[i]);
		}
	}

	void leds(deluge::ui::LedFrame& frame) override {
		(void)frame.set(static_cast<deluge::ui::LedId>(ButtonId::Play),
		                deluge::ui::LedState{deluge::ui::LedState::Kind::On, 0, 0});
	}

	Result handle(const UiEvent& event, const UiContext&) override {
		if (event.kind != UiEvent::Kind::Button || event.button != ButtonId::Clip || !event.on) {
			return Result::unhandled();
		}
		Result r;
		r.handled = true;
		(void)r.actions.push_back(
		    Action{.kind = Action::Kind::Push, .id = SceneId{kDemo, static_cast<uint16_t>(arg_ + 1)}});
		return r;
	}

private:
	uint16_t arg_ = 0;
};

/// @brief The scene table both scripts run against: `DemoScene` in the one application slot.
///
/// Declared in construction order (the scene before the table that points at it, the table before
/// the registry that views it) for the same reason `tick_spec.cpp`'s `Rig` is.
struct DemoRegistry {
	DemoScene demo;
	std::array<Scene*, 18> table{};
	SceneRegistry reg{std::span<Scene* const>{table}};

	DemoRegistry() {
		table[static_cast<size_t>(kDemo)] = &demo;
		reg = SceneRegistry{std::span<Scene* const>{table}};
	}
	DemoRegistry(const DemoRegistry&) = delete;
	DemoRegistry& operator=(const DemoRegistry&) = delete;
};

} // namespace

// clang-format off
describe shell_scripts("shell script harness (level 2)", $ {
	it("boots to the splash and snapshots it twice", _ {
		DemoRegistry r;
		// No `initial_scene`: the default boot path, `Shell::boot("test")`, which is what puts the
		// shell's own `Boot` splash up.
		const uitest::RunResult result = uitest::expect_script_goldens(self, "boot", r.reg, Services{});
		expect(result.snapshots).to_equal(uint32_t(2));
	});

	it("navigates, shows a popup and lets it expire", _ {
		DemoRegistry r;
		uitest::RunOptions opts;
		opts.initial_scene = SceneId{kDemo, 0};
		const uitest::RunResult result = uitest::expect_script_goldens(self, "popup", r.reg, Services{}, opts);
		expect(result.snapshots).to_equal(uint32_t(4));
	});

	it("reports an unknown command's line and writes nothing", _ {
		DemoRegistry r;
		const std::string out = std::string(UI_GOT_DIR) + "/shell/frobnicate";
		std::error_code ec;
		std::filesystem::remove_all(out, ec);

		const uitest::RunResult result =
		    uitest::run_script("0 snapshot start\n10 frobnicate\n", r.reg, Services{}, out);

		expect(result.error.has_value()).to_be_true();
		expect(result.error->line).to_equal(uint16_t(2));
		expect(std::string(result.error->message)).to_equal(std::string("unknown command"));
		expect(result.snapshots).to_equal(uint32_t(0));
		// Nothing at all: the parse fails before the shell is ever built, so not even the step at
		// 0 ms runs.
		expect(std::filesystem::exists(out)).to_be_false();
	});

	it("reports an unknown button by its own script line", _ {
		DemoRegistry r;
		const std::string out = std::string(UI_GOT_DIR) + "/shell/unknown-button";
		std::error_code ec;
		std::filesystem::remove_all(out, ec);

		// A name the *parser* accepts (it has no board profile to check it against) and the runner
		// rejects, on line 3 -- the comment and the blank line above it are not steps.
		const uitest::RunResult result = uitest::run_script("# a comment\n\n0 button Frobnicate down\n", r.reg,
		                                                    Services{}, out);

		expect(result.error.has_value()).to_be_true();
		expect(result.error->line).to_equal(uint16_t(3));
		expect(std::string(result.error->message)).to_equal(std::string("unknown button"));
	});

	it("sizes the surfaces from a board step", _ {
		DemoRegistry r;
		const std::string out = std::string(UI_GOT_DIR) + "/shell/board-step";
		std::error_code ec;
		std::filesystem::remove_all(out, ec);

		uitest::RunOptions opts;
		opts.initial_scene = SceneId{kDemo, 0};
		const uitest::RunResult result =
		    uitest::run_script("0 board 128x32+0\n0 snapshot only\n", r.reg, Services{}, out, opts);

		expect(result.error.has_value()).to_be_false();
		expect(result.snapshots).to_equal(uint32_t(1));
		std::ifstream in(out + "/only.oled0.pbm");
		std::string header;
		std::getline(in, header);
		std::string dims;
		std::getline(in, dims);
		expect(header).to_equal(std::string("P1"));
		expect(dims).to_equal(std::string("128 32"));
	});
});
// clang-format on

DELUGE_SPEC(shell_scripts_spec, shell_scripts)
