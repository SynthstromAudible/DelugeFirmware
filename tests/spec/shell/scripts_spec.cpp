#include "shell/shell.h"
#include "support/script_runner.h"
#include "ui/oled/text.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "../support/spec_main.h"
using namespace deluge::ui::shell;

namespace {

/// The kind `DemoScene` registers under: the first an application may use.
constexpr SceneKind kDemo = SceneKind::FirstApp;

/// The three colours `DemoScene` paints its diagonal with, one per pad; picked distinct in every
/// channel so a `.state` line cannot be confused with its neighbour.
constexpr RGB kDemoColours[] = {RGB{200, 0, 0}, RGB{0, 180, 0}, RGB{0, 0, 160}};

/// The colour a held pad is lit; white, so it cannot be mistaken for any of the three above.
constexpr RGB kHeldColour{255, 255, 255};

/// The period `DemoScene` declares its `Record` blink at.
constexpr uint16_t kRecordBlinkMs = 500;

/// @brief The stand-in application scene: a title, a three-pad diagonal, the `Play` LED, and one
///        navigation gesture (`Clip` pushes another copy of itself, one argument higher).
///
/// One instance covers every stack entry -- the registry holds scene *singletons*, so a second
/// push re-enters this same object with a new `SceneId` -- which is exactly why `arg_` is set in
/// `on_enter` rather than assumed.
class DemoScene : public Scene {
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
		const deluge::gfx::oled::Rect b = frame.bounds();
		deluge::ui::oled::title(frame, "DEMO");
		// The two counters, on one line: `T` is the net `Select` detents `handle()` has seen, `M`
		// how many `Dirty::ClipContent` facts `on_model_changed` has. Drawn unconditionally, so an
		// unturned, unchanged scene still says `T0 M0` -- a counter that appeared only once it was
		// non-zero could not tell "nothing happened" from "the scene stopped drawing it".
		const std::string counters = "T" + std::to_string(turns_) + " M" + std::to_string(model_changes_);
		deluge::ui::oled::text(
		    frame, {0, b.h / 2, b.w, 10}, counters,
		    deluge::ui::oled::TextStyle{deluge::gfx::oled::FontId::Apple7, deluge::ui::oled::Align::Centre});
		if (arg_ != 0) {
			const std::string n = std::to_string(static_cast<unsigned>(arg_));
			deluge::ui::oled::text(
			    frame, {0, b.h - 10, b.w, 10}, n,
			    deluge::ui::oled::TextStyle{deluge::gfx::oled::FontId::Apple7, deluge::ui::oled::Align::Centre});
		}
	}

	void render_grid(deluge::ui::grid::GridFrame& frame) override {
		for (int32_t i = 0; i < 3; i++) {
			(void)frame.pad(i, i, kDemoColours[i]);
		}
		// The base scene's frame starts at the grid's origin, so its local coordinates are the
		// absolute ones a `UiEvent::Pad` carries -- nothing to translate here.
		if (held_pad_) {
			(void)frame.pad(held_pad_->first, held_pad_->second, kHeldColour);
		}
	}

	void leds(deluge::ui::LedFrame& frame) override {
		(void)frame.set(static_cast<deluge::ui::LedId>(ButtonId::Play),
		                deluge::ui::LedState{deluge::ui::LedState::Kind::On, 0, 0});
		// Blinking, not lit: the two are indistinguishable in the fake's `set_led` shadow (which
		// only sees the level `flush` resolved for this tick), so this is what pins that `.state`
		// reports the *declaration* -- spec G §3's `led <Name> blink <period>`.
		(void)frame.set(static_cast<deluge::ui::LedId>(ButtonId::Record),
		                deluge::ui::LedState{deluge::ui::LedState::Kind::Blink, kRecordBlinkMs, 0});
	}

	Result handle(const UiEvent& event, const UiContext&) override {
		Result r;
		switch (event.kind) {
		case UiEvent::Kind::Button:
			if (event.button != ButtonId::Clip || !event.on) {
				return Result::unhandled();
			}
			r.handled = true;
			(void)r.actions.push_back(
			    Action{.kind = Action::Kind::Push, .id = SceneId{kDemo, static_cast<uint16_t>(arg_ + 1)}});
			return r;
		case UiEvent::Kind::Pad:
			// Read in `handle()`, never through a frame: reading an input while rendering claims it
			// (`ui/core/frame_context.h` rule 2), and this scene draws on both surfaces in a tick,
			// so a frame read would claim the same press twice. No `Repaint` needed -- an ordinary
			// pad event is routed by `pump_input` *before* the frame is built, so this tick's grid
			// already shows the change.
			if (event.on) {
				held_pad_ = std::pair<uint8_t, uint8_t>{event.x, event.y};
			}
			else {
				held_pad_.reset();
			}
			r.handled = true;
			return r;
		case UiEvent::Kind::Encoder:
			if (event.encoder != deluge::ui::EncoderRole::Select) {
				return Result::unhandled();
			}
			turns_ += event.delta;
			r.handled = true;
			// A turn is widget-owned, so the shell holds it back and delivers it *after* render
			// (`Shell::deliver_post_render_input`) -- this tick's frame was built before `turns_`
			// moved. Asking for the surface is how a scene reacts to a held-back event; the shell
			// honours it on the next tick.
			(void)r.actions.push_back(Action{.kind = Action::Kind::Repaint, .surface = SurfaceRole::Main});
			return r;
		default:
			return Result::unhandled();
		}
	}

	void on_model_changed(const deluge::notify::DirtySet& dirty) override {
		if (!dirty.has(deluge::notify::Dirty::ClipContent)) {
			return;
		}
		model_changes_++;
		// `Dirty::ClipContent` invalidates the *grid* only (`Shell::apply_dirty`), so a counter
		// drawn on the OLED would go stale. `on_model_changed` cannot return an action, but the
		// grid does repaint this tick, which earns this scene an `after_render()` -- the shell's
		// own "ask for a surface on the next tick" path.
		oled_stale_ = true;
	}

	Result after_render() override {
		if (!oled_stale_) {
			return {};
		}
		oled_stale_ = false;
		Result r;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Repaint, .surface = SurfaceRole::Main});
		return r;
	}

private:
	uint16_t arg_ = 0;
	int32_t turns_ = 0;
	uint32_t model_changes_ = 0;
	bool oled_stale_ = false;
	std::optional<std::pair<uint8_t, uint8_t>> held_pad_;
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

/// @brief A `DemoScene` twin that draws one extra OLED pixel -- used only to prove the migration
///        double-run (`RunOptions::second_registry`, spec G §6) catches a real divergence between
///        two scene tables. Everything else is identical to `DemoScene`.
class DemoSceneExtraPixel final : public DemoScene {
public:
	void render_oled(SurfaceRole role, deluge::ui::oled::OledFrame& frame) override {
		DemoScene::render_oled(role, frame);
		if (role == SurfaceRole::Main) {
			frame.canvas.draw_pixel(0, 0);
		}
	}
};

/// @brief As `DemoRegistry`, but with `DemoSceneExtraPixel` in the one application slot.
struct DemoRegistryExtraPixel {
	DemoSceneExtraPixel demo;
	std::array<Scene*, 18> table{};
	SceneRegistry reg{std::span<Scene* const>{table}};

	DemoRegistryExtraPixel() {
		table[static_cast<size_t>(kDemo)] = &demo;
		reg = SceneRegistry{std::span<Scene* const>{table}};
	}
	DemoRegistryExtraPixel(const DemoRegistryExtraPixel&) = delete;
	DemoRegistryExtraPixel& operator=(const DemoRegistryExtraPixel&) = delete;
};

/// @brief The whole contents of \p path.
/// @param path File to read.
/// @return Its bytes, or an empty string if it could not be opened.
std::string read_text(const std::string& path) {
	std::ifstream in(path);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

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

	it("lowers pad, encoder and model steps", _ {
		DemoRegistry r;
		uitest::RunOptions opts;
		opts.initial_scene = SceneId{kDemo, 0};
		const uitest::RunResult result = uitest::expect_script_goldens(self, "input", r.reg, Services{}, opts);
		expect(result.snapshots).to_equal(uint32_t(6));

		// Explicit facts, read out of the run's own output rather than trusting the goldens: the
		// pad the script pressed is lit white while held and gone once released, and each counter
		// step redraws the OLED (so no two of the three counter frames are the same image).
		const std::string dir = std::string(UI_GOT_DIR) + "/shell/input/";
		const std::string idle = read_text(dir + "idle.state");
		expect(idle.find("pad 2 3") == std::string::npos).to_be_true();
		// The declared LED states, not the levels `flush` resolved: `Play` is steady, `Record`
		// blinks, and the fake's `set_led` shadow cannot tell those two apart.
		expect(idle.find("led Play on\n") != std::string::npos).to_be_true();
		expect(idle.find("led Record blink 500\n") != std::string::npos).to_be_true();
		expect(read_text(dir + "pad-held.state").find("pad 2 3 255 255 255") != std::string::npos).to_be_true();
		expect(read_text(dir + "pad-released.state")).to_equal(idle);

		const std::string before = read_text(dir + "idle.oled0.pbm");
		const std::string up = read_text(dir + "turn-up.oled0.pbm");
		const std::string down = read_text(dir + "turn-down.oled0.pbm");
		const std::string model = read_text(dir + "model-changed.oled0.pbm");
		expect(before.empty()).to_be_false();
		expect(up == before).to_be_false();   // T0 -> T1
		expect(down == up).to_be_false();     // T1 -> T-1
		expect(model == down).to_be_false();  // M0 -> M1
	});

	it("refuses an encoder step it would have to narrow", _ {
		DemoRegistry r;
		const std::string out = std::string(UI_GOT_DIR) + "/shell/encoder-overflow";
		std::error_code ec;
		std::filesystem::remove_all(out, ec);

		// 63 detents x 2 edges = 126, which fits an int8_t; 64 x 2 = 128, which does not -- and
		// would wrap to -128, silently reversing the turn.
		uitest::RunOptions opts;
		opts.initial_scene = SceneId{kDemo, 0};
		expect(uitest::run_script("0 encoder Select +63\n", r.reg, Services{}, out, opts).error.has_value())
		    .to_be_false();

		const uitest::RunResult result = uitest::run_script("0 encoder Select +64\n", r.reg, Services{}, out, opts);
		expect(result.error.has_value()).to_be_true();
		expect(result.error->line).to_equal(uint16_t(1));
		expect(std::string(result.error->message))
		    .to_equal(std::string("too many detents for one on_encoder_edges call"));
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

	it("migration hook: the same registry run twice produces identical trees", _ {
		// Spec G §6's migration acceptance, the "nothing changed" side: two independent
		// DemoRegistry instances (so the second run is not literally the same objects), same
		// script, same scene table shape -- the double-run diff must find no disagreement.
		DemoRegistry r1;
		DemoRegistry r2;
		uitest::RunOptions opts;
		opts.initial_scene = SceneId{kDemo, 0};
		opts.second_registry = &r2.reg;
		const uitest::RunResult result = uitest::expect_script_goldens(self, "popup", r1.reg, Services{}, opts);
		expect(result.snapshots).to_equal(uint32_t(4));
	});

	it("migration hook: a registry that draws one extra pixel is caught, naming the .pbm", _ {
		// The other side: two registries that visibly disagree. Run both by hand (bypassing
		// expect_script_goldens, which would fail this very `it` block on the mismatch it exists
		// to detect) and assert on the diff helper directly.
		DemoRegistry good;
		DemoRegistryExtraPixel bad;
		const std::string out_good = std::string(UI_GOT_DIR) + "/shell/migration-mismatch-good";
		const std::string out_bad = std::string(UI_GOT_DIR) + "/shell/migration-mismatch-bad";
		std::error_code ec;
		std::filesystem::remove_all(out_good, ec);
		std::filesystem::remove_all(out_bad, ec);

		uitest::RunOptions opts;
		opts.initial_scene = SceneId{kDemo, 0};
		const std::string script = read_text(std::string(UI_SCRIPTS_DIR) + "/popup.txt");

		const uitest::RunResult a = uitest::run_script(script, good.reg, Services{}, out_good, opts);
		const uitest::RunResult b = uitest::run_script(script, bad.reg, Services{}, out_bad, opts);
		expect(a.error.has_value()).to_be_false();
		expect(b.error.has_value()).to_be_false();

		const std::string mismatch = uitest::detail::first_mismatch(out_good, out_bad);
		expect(mismatch.empty()).to_be_false();
		const std::string suffix = ".oled0.pbm";
		const bool names_a_pbm =
		    mismatch.size() >= suffix.size() && mismatch.compare(mismatch.size() - suffix.size(), suffix.size(), suffix) == 0;
		expect(names_a_pbm).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(shell_scripts_spec, shell_scripts)
