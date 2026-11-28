#pragma once

/// @file
/// @brief What every `ShellHostUI` spec builds on: a guest over a scene table, with the legacy fakes reset, the
///        boards and profiles the drawing examples are checked at, and the helpers that read legacy's side back.

#include "fake_legacy_hid.h"
#include "fake_legacy_ui.h"

#include "base/notify/dirty_set.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui_timer_manager.h"
#include "hid/button.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "model/settings/runtime_feature_settings.h"
#include "storage/flash_storage.h"
#include "ui/scenes/context_menu/context_menu_scene.h"
#include "ui/scenes/marker/marker_scene.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/widgets/core/style.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace shell_host_ui_rig {

using deluge::gui::ui::ShellHostUI;
using deluge::gui::ui::ShellRuntime;
using deluge::hid::display::OLED;
using namespace deluge::ui::shell;

inline constexpr SceneKind kBase = SceneKind::FirstApp;
inline constexpr SceneKind kOver = static_cast<SceneKind>(static_cast<uint8_t>(SceneKind::FirstApp) + 1);

/// @brief Some other `UI`, for the examples that need `getCurrentUI()` to answer "not the
///        guest".
///
/// A real `UI` rather than a cast pointer: `rearmTimer`'s guard only ever compares the address, but
/// an example that lies about a type to make a comparison come out right is one nobody can later
/// change safely. `UI` has exactly two pure virtuals (`UI::renderOLED` and `UI::getUIType`, `gui/ui/ui.h`), and neither
/// is reached here -- nothing renders this and nothing asks its type.
class OtherUI final : public UI {
public:
	void renderOLED(deluge::hid::display::oled_canvas::Canvas&) override {}
	UIType getUIType() override { return UIType::NONE; }
};

/// @brief A scene that paints a colour derived from (x, y), so a copy that transposes, offsets or
///        drops a column is visible rather than merely "different".
///
/// Every composition answer is a settable member: the two the guest maps
/// (`is_grid_overlay` -> `renderMainPads`'s return, `occludes_grid` -> `canSeeViewUnderneath`) plus
/// `greyout_mask`, so one scene type can stand for both a menu-shaped overlay and a full-screen
/// root without the fixture varying anything else at the same time.
class PatternScene final : public Scene {
public:
	explicit PatternScene(SceneKind k) : kind_(k) {}

	SceneKind kind() const override { return kind_; }

	void render_oled(SurfaceRole, deluge::ui::widgets::oled::OledFrame& frame) override {
		// Three lines at different lengths: enough that a frame served from the wrong surface, or
		// half-served, is not a plausible match.
		frame.hline(3, 0, 100);
		frame.hline(9, 4, 40);
		frame.hline(20, 10, 60);
	}

	void render_grid(deluge::ui::widgets::grid::GridFrame& frame) override {
		for (const deluge::ui::widgets::grid::PadFlash& f : flashes) {
			frame.flash(f.x, f.y, f.colour);
		}
		if (sparse) {
			for (const auto& [x, y] : lit) {
				(void)frame.pad(x, y, lit_colour);
			}
			return;
		}
		const deluge::ui::gfx::grid::Rect b = frame.bounds();
		for (int32_t y = 0; y < b.h; y++) {
			for (int32_t x = 0; x < b.w; x++) {
				(void)frame.pad(x, y, pad_for(x, y));
			}
		}
	}

	/// @brief One routed event, with the parts of the `UiContext` that came with it.
	///
	/// The context is captured rather than the reference kept: `UiContext` is an aggregate of
	/// borrowed references built on the stack per event and does not
	/// outlive the `handle()` call.
	struct Seen {
		UiEvent ev;
		bool shift;
		bool sticky;
		uint32_t now_ms;
		uint32_t acceleration_clock_ms;
	};

	Result handle(const UiEvent& ev, const UiContext& ctx) override {
		seen.push_back(Seen{ev, ctx.shift, ctx.sticky, ctx.now_ms, ctx.acceleration_clock_ms});
		if (remove_self_on_event) {
			// `MenuScene`'s answer to Back at depth 0 (`MenuScene::handle_event`'s Back arm),
			// reduced to the one thing this suite is about: a scene that takes itself off the
			// stack from inside `Shell::tick`'s routing. Off by default so every other example
			// keeps the delivery-only behaviour below.
			Result r;
			r.handled = true;
			(void)r.actions.push_back(
			    deluge::ui::shell::Action{.kind = deluge::ui::shell::Action::Kind::Remove, .pop_to = kind_});
			return r;
		}
		if (handle_midi && ev.kind >= UiEvent::Kind::MidiNoteOn) {
			return Result{.handled = true};
		}
		// Unhandled on purpose: the guest's input hooks are about *delivery*, and a scene that
		// claimed an event would stop it reaching anything below it and make the routing this
		// records depend on what the scene decided.
		return Result::unhandled();
	}

	/// Make `handle` take this scene off the stack, the way `MenuScene` answers Back at depth 0.
	void on_model_changed(const deluge::base::notify::DirtySet&) override { model_changed++; }

	bool remove_self_on_event = false;
	/// Make `handle` use every MIDI learn offer.
	bool handle_midi = false;
	/// How many times `on_model_changed` ran.
	int model_changed = 0;

	/// Every event routed to this scene, in order.
	std::vector<Seen> seen;

	/// @brief Declare `led` for this frame, if `led_declared`.
	///
	/// The whole of the guest's LED story runs through here: `Shell::derive_leds` composes this
	/// frame, `Shell::flush` diffs it and calls `SurfaceSet::set_led` only for an id it holds
	/// (`led_send_needed`), and `GuestSurfaceSet::set_led` turns that into
	/// `indicator_leds::setLedState`. Settable rather than fixed so one scene can be asked both
	/// halves of the ownership rule -- declaring an LED, and then not.
	void leds(deluge::ui::widgets::core::LedFrame& frame) override {
		if (led_declared) {
			(void)frame.set(led_id, led_state);
		}
	}

	std::optional<uint32_t> next_timer() const override { return timer; }

	bool is_grid_overlay() const override { return overlay; }
	PadRegions pad_regions() const override { return regions; }
	bool occludes_grid() const override { return occludes; }
	bool input_transparent() const override { return passive; }
	deluge::ui::widgets::grid::GreyoutMask greyout_mask() const override { return greyout; }
	GridPadClaim grid_pad_claim() const override { return claim; }

	void on_resume() override { resumes++; }
	void on_exit() override { exits++; }
	bool on_enter(const SceneId&) override {
		enters++;
		return true;
	}

	/// @brief The colour this scene paints at (x, y) -- the spec's own sentinel, computed from the
	///        coordinates rather than read back out of anything the guest touched.
	static RGB pad_for(int32_t x, int32_t y) {
		return RGB{static_cast<uint8_t>(1 + x * 7), static_cast<uint8_t>(1 + y * 13),
		           static_cast<uint8_t>(1 + x + y * 3)};
	}

	/// When set, `render_grid` paints only the pads in `lit`, in `lit_colour`, rather than a full
	/// frame: a scene that draws a few pads, or none.
	bool sparse = false;
	std::vector<std::pair<int32_t, int32_t>> lit;
	RGB lit_colour{130, 120, 130};
	/// The pads `render_grid` flashes each time it runs.
	std::vector<deluge::ui::widgets::grid::PadFlash> flashes;
	bool overlay = false;
	/// What `pad_regions()` answers: which regions the guest takes from legacy for this scene.
	PadRegions regions = PadRegions::All;
	bool occludes = false;
	bool passive = false;
	/// What `grid_pad_claim()` answers. `GridPadClaim::All` -- `Scene`'s own default -- unless an
	/// example wants `ShellHostUI::claimsGridPad` to see something else.
	GridPadClaim claim = GridPadClaim::All;
	bool led_declared = false;
	deluge::ui::widgets::core::LedId led_id{};
	deluge::ui::widgets::core::LedState led_state{};
	/// What `next_timer()` answers. `std::nullopt` -- `Scene`'s own default -- unless an example
	/// wants a deadline.
	std::optional<uint32_t> timer{};
	deluge::ui::widgets::grid::GreyoutMask greyout{};
	int enters = 0;
	int exits = 0;
	int resumes = 0;

private:
	SceneKind kind_;
};

/// @brief The 64-row panel every drawing spec in this stack is also checked at.
inline constexpr DelugeDisplayDesc kDisplays64[] = {{128, 64, 0}};

inline DelugeBoard board_64() {
	DelugeBoard b = kDelugeBoard;
	b.display_height = 64;
	b.display_count = 1;
	b.displays = kDisplays64;
	return b;
}

inline const BoardProfile& profile_64() {
	static const BoardProfile p = [] {
		static constexpr const deluge::ui::widgets::core::Style* kStyles[] = {
		    &deluge::ui::widgets::core::kDelugeStyle64};
		BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

/// @brief A `MIDICable` **address**, and deliberately nothing more.
///
/// The two learn hooks take `MIDICable&` and this suite cannot build one: `MIDICable` is abstract
/// and every shipped subclass needs a USB or DIN backend behind it
/// (`tests/integration/ui/scenes/midi_learn_spec.cpp`'s `SpecCable` makes the same observation, in a target
/// that does link `io/midi/`). It does not need to build one, because **nothing on this path reads
/// through the pointer**: the host names the cable by its handle in `UiEvent::device`
/// (`io/midi/device_registry.h`, faked here by `support/fake_device_registry.cpp`), and the one thing in the
/// tree that dereferences it -- `services::MidiLearn` -- is in the closure this suite does not
/// build. So the MIDI-learn examples (`shell_host_ui_buttons_spec.cpp`) turn the event's device back into its cable
/// and assert it is the address that went in, which is the whole of what this layer transports.
alignas(alignof(std::max_align_t)) inline std::byte cable_storage[sizeof(void*) * 4]{};

inline MIDICable& fake_cable() {
	return *reinterpret_cast<MIDICable*>(&cable_storage[0]);
}

/// @brief A `ShellHostUI` over a four-scene table (base, over, the context menu's kind, the marker's kind),
/// with both sets of legacy fakes reset.
struct Rig {
	PatternScene base{kBase};
	PatternScene over{kOver};
	/// Registered under the context menu's kind, which the long-press exit leaves alone.
	PatternScene menu{deluge::ui::scenes::context_menu::kContextMenuSceneKind};
	/// Registered under the marker editor's kind, which the guest's sidebar answer keys on.
	PatternScene marker{deluge::ui::scenes::marker::kMarkerSceneKind};
	std::array<Scene*, kMaxSceneKinds> table{};
	SceneRegistry reg{std::span<Scene* const>{table}};
	ShellRuntime runtime;
	ShellHostUI guest{runtime};

	Rig() : runtime(fill(), kDelugeBoard, deluge_profile()) {
		fakelegacy::reset();
		fakelegacyui::reset();
		fakelegacyui::current_ui = &guest;
	}
	explicit Rig(const DelugeBoard& board, const BoardProfile& profile) : runtime(fill(), board, profile) {
		fakelegacy::reset();
		fakelegacyui::reset();
		fakelegacyui::current_ui = &guest;
	}
	/// Never leave `getCurrentUI()` pointing at a destroyed guest. Only ever compared, never
	/// dereferenced -- but a dangling pointer that is only compared is still a dangling pointer.
	~Rig() { fakelegacyui::current_ui = nullptr; }
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// Fill the table and hand back the registry -- runs before `guest`'s constructor reads it,
	/// which is why it is a member function call in the initialiser rather than a body statement.
	const SceneRegistry& fill() {
		table[static_cast<size_t>(kBase)] = &base;
		table[static_cast<size_t>(kOver)] = &over;
		table[static_cast<size_t>(deluge::ui::scenes::context_menu::kContextMenuSceneKind)] = &menu;
		table[static_cast<size_t>(deluge::ui::scenes::marker::kMarkerSceneKind)] = &marker;
		return reg;
	}

	bool open(SceneKind k = kBase) {
		guest.setInitialScene(SceneId{k, 0});
		return guest.opened();
	}
};

/// @brief A board whose pad grid is **taller and narrower** than legacy's, with legacy's own panel.
///
/// `SurfaceSet::build` sizes the grid from `DelugeBoard::pad_grid_width`/`pad_grid_height` and
/// `BoardProfile::sidebar_cols` (`SurfaceSet::build`) and never consults legacy's numbers, so
/// nothing but `serveGrid`'s own two `min`s keeps the copy inside
/// `PadLEDs::image[kDisplayHeight][kDisplayWidth + kSideBarWidth]`. `canOpen()` cannot help: it is
/// the *panel's* shape, which this board leaves alone.
///
/// One board exercises both clamps, in opposite directions:
///   * **12 rows** (legacy has 8) — without `min(dims.rows, kDisplayHeight)` the copy writes four
///     rows past the end of the image it was handed.
///   * **8 main + 2 sidebar = 10 columns** (legacy has 18) — without `min(x1, dims.cols())` the copy
///     reads `PadGrid::at(10..15, y)`, which is a `DELUGE_GFX_CONTRACT` violation and traps
///     (`DELUGE_GFX_CONTRACT` in `ui/gfx/contract.h`, `PadGrid::at`).
inline DelugeBoard board_tall_narrow_grid() {
	DelugeBoard b = kDelugeBoard;
	b.pad_grid_width = 8;
	b.pad_grid_height = 12;
	return b;
}

/// @brief A profile whose encoder table is in a **different order** from the Deluge's.
///
/// `ShellHostUI::feedDetents` resolves a role to a board index through
/// `BoardProfile::encoder_by_index` rather than hard-coding one; this is what makes that
/// resolution assertable instead of accidentally right. The Deluge's order is
/// Vertical, Horizontal, Tempo, Select, GoldUpper, GoldLower (`deluge_profile`);
/// this reverses the first four, so an implementation that hard-coded index 1 for Horizontal would
/// feed Select instead.
inline const BoardProfile& profile_permuted_encoders() {
	static const BoardProfile p = [] {
		static constexpr deluge::ui::widgets::core::EncoderRole kOrder[] = {
		    deluge::ui::widgets::core::EncoderRole::Select,     // 0
		    deluge::ui::widgets::core::EncoderRole::Tempo,      // 1
		    deluge::ui::widgets::core::EncoderRole::Horizontal, // 2
		    deluge::ui::widgets::core::EncoderRole::Vertical,   // 3
		    deluge::ui::widgets::core::EncoderRole::GoldUpper,  // 4
		    deluge::ui::widgets::core::EncoderRole::GoldLower,  // 5
		};
		BoardProfile q = deluge_profile();
		q.encoder_by_index = kOrder;
		return q;
	}();
	return p;
}

/// @brief A profile with **no** horizontal encoder, so the "role nothing carries" arm is drivable.
inline const BoardProfile& profile_no_horizontal() {
	static const BoardProfile p = [] {
		static constexpr deluge::ui::widgets::core::EncoderRole kOrder[] = {
		    deluge::ui::widgets::core::EncoderRole::Vertical,
		    deluge::ui::widgets::core::EncoderRole::Tempo,
		    deluge::ui::widgets::core::EncoderRole::Select,
		};
		BoardProfile q = deluge_profile();
		q.encoder_by_index = kOrder;
		return q;
	}();
	return p;
}

/// @brief Zero legacy's pad image without touching anything else, so a later read of it can only be
///        something the guest wrote.
inline void blank_legacy_pads() {
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
			PadLEDs::image[y][x] = RGB{0, 0, 0};
		}
	}
}

/// @brief Is any byte of the canvas set? A frame comparison between two blank panels passes
///        forever; every render example here asserts this too.
inline bool any_ink(std::span<const uint8_t> bytes) {
	for (uint8_t b : bytes) {
		if (b != 0) {
			return true;
		}
	}
	return false;
}

inline std::vector<uint8_t> copy_of(std::span<const uint8_t> bytes) {
	return std::vector<uint8_t>{bytes.begin(), bytes.end()};
}

/// @brief The first event of \p kind in \p scene, or null.
///
/// A feed usually leaves more than one event in the queue -- a shift press and the pad it modifies,
/// a replayed deferral and the press that released the lock -- so an example that means "the pad"
/// has to say so rather than index blindly.
inline const PatternScene::Seen* first_of(const PatternScene& scene, UiEvent::Kind kind) {
	for (const PatternScene::Seen& s : scene.seen) {
		if (s.ev.kind == kind) {
			return &s;
		}
	}
	return nullptr;
}

/// @brief A `Service` that declares one knob indicator, standing in for `ModKnobs`.
///
/// `Shell::flush` calls `SurfaceSet::set_indicator` only for an indicator declared this tick
/// and the only declarer in the shipped tree is
/// `ui::scenes::services::ModKnobs` -- a *service*, which this guest's `Services` does not carry in
/// this example. This is the smallest thing that stands where `ModKnobs`
/// will stand, so "the guest writes no indicator" can be shown to be about the declaration rather
/// than about the guest.
class IndicatorService final : public Service {
public:
	void leds(deluge::ui::widgets::core::LedFrame& frame) override {
		(void)frame.indicator(deluge::ui::widgets::core::IndicatorId::Lower,
		                      deluge::ui::widgets::core::KnobIndicator{{0x11, 0x22, 0x5A, 0x77}});
	}
};

/// @brief The guest's gold-knob service, as far as this spec asks of it: every event the router hands it.
class KnobRecorder final : public Service {
public:
	std::vector<UiEvent> seen;
	Result handle(const UiEvent& event, const UiContext& /*ctx*/) override {
		seen.push_back(event);
		Result result;
		result.handled = true;
		return result;
	}
	/// @brief How many of the events it saw were of @p kind.
	[[nodiscard]] size_t count(UiEvent::Kind kind) const {
		size_t n = 0;
		for (const UiEvent& e : seen) {
			n += e.kind == kind ? 1 : 0;
		}
		return n;
	}
};

/// @brief Put the guest's clock at \p ms: `ShellRuntime::nowMs()` counts legacy's audio-sample counter, 44 samples
///        a millisecond.
inline void set_clock_ms(uint32_t ms) {
	AudioEngine::audioSampleTimer = ms * 44;
}

/// @brief Run the guest's own timer chain for \p rounds callbacks, with **no input at all**.
///
/// Nothing here decides when the guest should run. Each round reads the duration the guest last
/// asked `uiTimerManager` for, advances the clock by exactly that, clears the timer the way
/// `UITimerManager::routine` does before it dispatches (`gui/ui_timer_manager.cpp`), and calls
/// `timerCallback()` the way that function's `UI_SPECIFIC` arm does. A guest that
/// re-arms nothing ends the loop early -- which is what makes "the blink froze on an idle system" a
/// failed expectation rather than a slow test.
/// @return The clock each callback ran at, in order; shorter than \p rounds if the chain stopped.
inline std::vector<uint32_t> run_quiet(ShellHostUI& guest, uint32_t& now_ms, int rounds) {
	std::vector<uint32_t> fired;
	for (int i = 0; i < rounds; i++) {
		const int32_t in = fakelegacyui::ui_specific_armed_ms();
		if (in < 0) {
			break; // Nothing armed: the chain is over, and the caller's expectations say so.
		}
		now_ms += static_cast<uint32_t>(in);
		set_clock_ms(now_ms);
		uiTimerManager.getTimer(TimerName::UI_SPECIFIC).active = false;
		(void)guest.timerCallback();
		fired.push_back(now_ms);
	}
	return fired;
}

/// @brief `v[i]`, or a value no expectation here uses, when the sequence is shorter than that.
///
/// Every assertion that reads a `run_quiet` result through this is about *which* tick fired, so a mutation that stops
/// the chain early must fail an expectation rather than index off the end of the vector -- an out-of-range read is
/// undefined behaviour, and `expect` does not abort the example the way an assert would.
template <typename T>
inline T nth_or(const std::vector<T>& v, size_t i, T fallback) {
	return i < v.size() ? v[i] : fallback;
}

/// @brief Every `indicator_leds::setLedState` call for \p board_led, as its on/off sequence.
inline std::vector<bool> led_history(uint8_t board_led) {
	std::vector<bool> out;
	for (const fakelegacy::LedWrite& w : fakelegacy::led_writes) {
		if (w.led == board_led) {
			out.push_back(w.on);
		}
	}
	return out;
}

} // namespace shell_host_ui_rig
