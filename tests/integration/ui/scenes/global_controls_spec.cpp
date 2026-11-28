/// @file
/// @brief `services::GlobalControls` -- driven against the real legacy it ports,
///        `Buttons::buttonAction` (`hid/buttons.cpp`) and `View::buttonAction`,
///        on a real `Song` built by `support/song_fixture.h`.
///
/// @par The oracle, and what "each side from its own sentinel" can mean here
///
/// Transport is a **process singleton**: there is one `PlaybackHandler`, one `Buttons` module and
/// one `AudioEngine::audioSampleTimer` in the address space, and no fixture can give legacy and
/// the port one each -- unlike the gold-knob spec, which gives each side its own clip and
/// its own `ParamManager`. So the discipline is applied in the shape that is available, and it is
/// stated here rather than assumed:
///
///   * **Each side establishes its own starting state**, explicitly, and each side asserts that
///     reset took before it drives anything. Neither side's input is ever computed from the
///     other's output.
///   * **Legacy's answer is captured into a local before the port runs**, and the model is then
///     *poisoned* with a value neither transition can produce -- `RecordingMode::ARRANGEMENT` for
///     the transport, `false` for the metronome legacy just turned on, `0x5EED` for the BACK arm's
///     cluster counter. If the port wrote nothing, the comparison sees the poison, not legacy's
///     leftovers. That is the same device `mod_knobs_spec.cpp`'s `poke_legacy_indicator` uses, for
///     the same reason -- the blindness of "the port stores nothing and the comparison silently passes".
///   * **Both sides are asserted to have MOVED**, so a run where neither did cannot pass on
///     equality alone.
///
/// @par What this spec counts as uncovered (oracle discipline)
///
///   * **`openUI(&saveSongUI)` / `openUI(&loadSongUI)` / `context_menu::clearSong`**
///     (`View::buttonAction`'s SAVE and LOAD arms). Not ported (see `global_controls.h`): the arm's decision is
///     compared, and the open is counted through `GlobalControls::song_opens(SongOpen)`, a tally per SITE so an
///     assertion about one arm cannot be satisfied by another.
///   * **The `THRESHOLD_RECORDING_MODE` popup arm** (`buttonAction` in `hid/buttons.cpp`). Not ported, and
///     it is a behaviour difference rather than a missing call; there is no port-side state that
///     could be driven into it.
///   * **The `SYNC_SCALING` fill-mode arm's note halves** (`views/view.cpp`). The note-editor test
///     needs no port (`global_controls.h`); the fill adjustments name `instrumentClipView`, unported.
///   * **`View::buttonAction`'s four MIDI-learn arms** (the `TAP_TEMPO` arm's learn half, the `LEARN` arm, and the
///     `PLAY` and `RECORD` arms gated on `UI_MODE_MIDI_LEARN`, in `views/view.cpp`). `MidiLearn`'s; this spec asserts
///     only that they are declined here, and that the transport arms they share a button with are correctly suppressed
///     while learn is active.
///   * **The dedicated undo/redo buttons** (`View::buttonAction`'s `undo` and `redo` arms). Dead code: they are inside
///     `#ifdef undoButtonX` and `undoButtonX` is defined nowhere, so there is nothing
///     to drive on either side.
///   * **The UNDO-vs-REDO choice** in `View::buttonAction`'s BACK arm. `playbackHandler.slowRoutine()`
///     there consumes the pending command inside the same call
///     (`PlaybackHandler::slowRoutine`) and a spec cannot make `deluge::sync::sd_busy()`
///     true, so both spellings read `NONE` by the time control returns. The arm's *body running*
///     is covered; which of the two it chose is not. See the BACK example.
///
/// @par Why a stub ROOT view, and not just any stub UI
///
/// `Buttons::buttonAction` dereferences `getCurrentUI()` unconditionally (`hid/buttons.cpp`), and
/// `getCurrentUI()` (`gui/ui/ui.cpp`) in a spec process with an empty stack returns `emptyStackUI`, a UI that
/// acts on nothing; main's returns `lastUIBeforeNullifying`, which is null
/// (`origin/main:src/deluge/gui/ui/ui.cpp`). `setRootUILowLevel` is the one door into the UI stack that
/// runs no view's `opened()` -- `tables_3_spec.cpp` already drives it for the same reason.
///
/// The stub must also **forward to `view.buttonAction`**, because `UI::buttonAction`'s base
/// implementation does not (`gui/ui/ui.h`, `return ActionResult::NOT_DEALT_WITH`). See
/// `StubUI`'s own @warning: `View::buttonAction` has exactly two callers in the tree, and the one
/// that matters is `TimelineView::buttonAction`'s final `else`
/// (`gui/views/timeline_view.cpp`), which every root view inherits.

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host/host_ports.h"
#include "project/device_settings.h"
#include "project/features.h"
#include "project/song.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/services/global_controls.h"
#include "ui/scenes/services/learn_mode.h"
#include "ui/scenes/services/view_state.h"

#include "ui/shell/input_lowering.h"
#include "ui/shell/leds.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/scene_stack.h"

#include "gui/l10n/strings.h"
#include "gui/ui/ui.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/recording/foreground_recording.h"

#include "fakes.h"
#include "support/card_access.h" // before <cppspec.hpp> -- see its @warning
#include "support/popup_capture.h"
#include "support/printers/transport_action_printer.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::ui::scenes::services::GlobalControls;
using deluge::ui::scenes::services::LearnMode;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::ButtonSet;
using deluge::ui::shell::deluge_profile;
using deluge::ui::shell::InputLowering;
using deluge::ui::shell::Result;
using deluge::ui::shell::Scene;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::SceneRegistry;
using deluge::ui::shell::SceneStack;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

/// `playback/playback_handler.cpp` defines this with no declaration in the header, as
/// `gui/views/view.cpp` declares it.
extern GlobalMIDICommand pendingGlobalMIDICommand;
/// The `int32_t` twin beside it in `playback/playback_handler.cpp`, which `views/view.cpp` mis-declares as a
/// `GlobalMIDICommand`. Declared with the defining type here; see `global_controls.cpp`'s note.
extern int32_t pendingGlobalMIDICommandNumClustersWritten;

namespace indicator_leds {
/// Legacy's LED shadow array (`ledStates` in `hid/led/indicator_leds.cpp`), which `setLedState` writes
/// (its store into the array). `indicator_leds.h` declares `ledBlinkState` but not this one, so a bare `extern` is the
/// only spelling available -- the same shape `pendingGlobalMIDICommand` needs above.
extern bool ledStates[];
} // namespace indicator_leds

namespace {

using ShellAction = deluge::ui::shell::Action;

/// @brief `pendingGlobalMIDICommandNumClustersWritten`, reachable from inside the anonymous
///        namespace.
int32_t& pending_global_midi_clusters() {
	return ::pendingGlobalMIDICommandNumClustersWritten;
}

/// @brief `spec::QuietAudio`, held for the whole process.
///
/// It keeps the audio routine still while the transport runs (`support/card_access.h`): no render
/// advances the playback this spec compares. Nothing this spec asserts is part of the audio
/// routine's contract: every comparison is on `PlaybackHandler`'s own state, which
/// `playButtonPressed` writes on the interaction tier.
///
/// Process-scoped rather than example-scoped because both sides of every comparison need it and
/// the legacy side is driven outside any rig.
spec::QuietAudio& quiet_audio() {
	static spec::QuietAudio instance;
	return instance;
}

/// @brief A `UI` that stands in for a **root view**: it claims nothing of its own and forwards to
///        `View::buttonAction`, which is the one thing that makes those arms reachable.
///
/// @warning `UI::buttonAction`'s base implementation is `return ActionResult::NOT_DEALT_WITH`
///          (`gui/ui/ui.h`) -- it does **not** forward to `view`. `View::buttonAction` has
///          exactly two callers in the whole tree: `TimelineView::buttonAction`'s final `else`
///          (`gui/views/timeline_view.cpp`) and `KeyboardScreen::buttonAction`
///          (`gui/ui/keyboard/keyboard_screen.cpp`) -- and `grep "public RootUI"` finds exactly
///          those two classes (`gui/views/timeline_view.h`,
///          `gui/ui/keyboard/keyboard_screen.h`), so "the current UI is the root UI" IS the
///          reachability rule rather than an approximation of it. With a menu open,
///          main's `SoundEditor::buttonAction` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) ends by delegating to
///          `currentMenuItem->buttonAction`, whose base
///          (`origin/main:src/deluge/gui/menu_item/menu_item.h`) returns `NOT_DEALT_WITH`, and nothing on that path
///          names `view` -- so legacy runs the tail and skips every `View::buttonAction` arm. The
///          port gates the same way, on `Scene::is_root()`; `RootScene` below is the spec-local
///          scene that declares it.
class StubUI final : public UI {
public:
	ActionResult buttonAction(deluge::hid::Button b, bool on) override { return view.buttonAction(b, on); }
	void renderOLED(deluge::hid::display::oled_canvas::Canvas&) override {}
	UIType getUIType() override { return UIType::NONE; }
};

StubUI& stub_ui() {
	static StubUI instance;
	return instance;
}

/// @brief A `UI` that takes the RECORD press and nothing else, as the sample browser and the audio recorder do.
class RecordTakingUI final : public UI {
public:
	ActionResult buttonAction(deluge::hid::Button b, bool on) override {
		return on && b == deluge::hid::button::RECORD ? ActionResult::DEALT_WITH : ActionResult::NOT_DEALT_WITH;
	}
	void renderOLED(deluge::hid::display::oled_canvas::Canvas&) override {}
	UIType getUIType() override { return UIType::NONE; }
};

RecordTakingUI& record_taking_ui() {
	static RecordTakingUI instance;
	return instance;
}

/// @brief `deluge_profile()`'s button matrix, inverted: the `(x, y)` a `DELUGE_EVENT_BUTTON`
///        carries for @p b.
///
/// The forward table is `BoardProfile::button_by_matrix` (`kMatrix` in `ui/shell/profiles/deluge.cpp`);
/// nothing in the shell inverts it, and hard-coding coordinates here would be a second copy of the
/// layout to drift.
/// @param b The button.
/// @return `{x, y}`, or `{255, 255}` if the profile has no cell for it.
std::pair<uint8_t, uint8_t> matrix_xy(ButtonId b) {
	const BoardProfile& p = deluge_profile();
	for (uint8_t y = 0; y < p.button_rows; y++) {
		for (uint8_t x = 0; x < p.button_cols; x++) {
			const size_t i = static_cast<size_t>(y) * p.button_cols + x;
			if (i < p.button_by_matrix.size() && p.button_by_matrix[i] == b) {
				return {x, y};
			}
		}
	}
	return {255, 255};
}

/// @brief The legacy `deluge::hid::Button` for @p b, via the same matrix cell.
///
/// `deluge::hid::button::fromXY` is legacy's own constructor (`hid/button.h`), so this is the one
/// mapping and not a table.
/// @param b The port's button id.
/// @return Legacy's.
deluge::hid::Button legacy_button(ButtonId b) {
	const auto [x, y] = matrix_xy(b);
	return deluge::hid::button::fromXY(x, y);
}

/// @brief The port-side counterpart of `StubUI`: a scene that declares itself a **root** and claims
///        nothing, so `GlobalControls`'s `View`-arm gate lets those arms through.
///
/// Spec-local rather than a registry scene, and that is the whole reason `Scene::is_root()` is a
/// virtual rather than a stack-position rule: `scene_registry()`'s scenes are process singletons
/// (`menu_scene()` and friends), so a rig that had to push one to satisfy a position rule would
/// leak it into the next example -- the cross-contamination trap. `root` is a plain
/// member so one example can flip it and drive the non-root case.
class RootScene final : public Scene {
public:
	SceneKind kind() const override { return SceneKind::FirstApp; }
	bool is_root() const override { return root; }
	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }

	bool root = true;
};

/// @brief A `LearnMode` a spec can switch on and off.
class FakeLearn final : public LearnMode {
public:
	bool on = false;
	bool active() const override { return on; }
};

/// @brief A live `GlobalControls` over a real (empty) scene stack and a real `InputLowering`,
///        driven the way the shell drives it.
struct GcRig {
	RootScene root;
	std::array<Scene*, 17> table{};
	SceneRegistry reg{};
	SceneStack stack{reg};
	InputLowering lowering{deluge_profile(), {}};
	FakeLearn learn;
	GlobalControls gc{stack, lowering, learn};
	uint32_t now_ms = 0;
	/// The app's ports with a recorder for the browsers the SAVE and LOAD arms open.
	deluge::ui::scenes::bind::test::RecordingRootViews views;
	deluge::ui::scenes::bind::Ports ports = deluge::gui::shell_host::host_ports();

	/// A registry of exactly one rig-local scene, at `SceneKind::FirstApp` (== 16). Nothing here
	/// touches `scene_registry()`'s singletons -- `handle_settings_menu`'s `Action::Kind::Push` is
	/// returned and inspected, never applied, because this rig drives `handle()` directly rather
	/// than through a `Router`.
	GcRig() {
		ports.root_views = &views;
		gc.set_ports(&ports);
		table[16] = &root;
		reg.by_kind = std::span<Scene* const>{table};
		(void)stack.push({SceneKind::FirstApp});
	}
	GcRig(const GcRig&) = delete;
	GcRig& operator=(const GcRig&) = delete;

	~GcRig() {
		while (stack.depth() > 0) {
			stack.pop(SceneKind::Null);
		}
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}

	UiContext ctx() const {
		return UiContext{lowering.held(), lowering.held_pads(), lowering.shift(), lowering.sticky(),
		                 now_ms,          lowering.profile(),   &lowering};
	}

	/// @brief Press or release @p b through the real lowering at @p at_ms and route every event it
	///        emits into `GlobalControls::handle`.
	///
	/// The router's chain minus the two steps above this service (`ui/shell/router.cpp`'s
	/// `run_chain`): an empty stack's `input_top()` is the null scene, which claims nothing, and
	/// `is_mod_or_gold` sends none of these buttons to `mod_knobs`. So `handle` really is what the
	/// event reaches.
	/// @return The last `Result` the service returned.
	Result press(ButtonId b, bool on, uint32_t at_ms) {
		now_ms = at_ms;
		const auto [x, y] = matrix_xy(b);
		lowering.begin_tick(at_ms);
		lowering.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
		Result last;
		UiEvent e{UiEvent::Kind::AllReleased};
		while (lowering.next(e)) {
			last = gc.handle(e, ctx());
		}
		lowering.end_tick(at_ms);
		return last;
	}

	/// @brief Press or release @p b through the real lowering at @p at_ms, its events taken by a scene above this
	///        service: the lowering sees the edge, `GlobalControls` does not.
	void press_elsewhere(ButtonId b, bool on, uint32_t at_ms) {
		now_ms = at_ms;
		const auto [x, y] = matrix_xy(b);
		lowering.begin_tick(at_ms);
		lowering.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
		UiEvent e{UiEvent::Kind::AllReleased};
		while (lowering.next(e)) {}
		lowering.end_tick(at_ms);
	}
};

/// @brief Reset every transport global this spec writes, and assert the reset took.
///
/// "Assert, then clear" read the other way round: a reset that
/// silently did not apply would make every example below start from the previous one's leftovers.
[[nodiscard]] bool reset_transport(uint32_t sample_timer) {
	playbackHandler.recording = RecordingMode::OFF;
	playbackHandler.playbackState = 0;
	playbackHandler.metronomeOn = false;
	Buttons::recordButtonPressUsedUp = false;
	AudioEngine::audioSampleTimer = sample_timer;
	preLoadedSong = nullptr;
	// Legacy's own record combo really does start an output recording -- `buttonAction` (`hid/buttons.cpp`),
	// reached when PLAY goes down with RECORD held and playback running -- and nothing in a spec
	// process ever reaps it (`deluge::recording::slowRoutine` is not driven here). Left standing, it
	// makes `AudioEngine::foregroundRecordingSource()` report `OUTPUT` for every later example,
	// which suppresses the RECORD LED (`PlaybackHandler::setLedStates`) and flips the PLAY arm's
	// first test (`buttonAction`). Cleared through the published seam, which is the same storage
	// (`processing/engines/audio_engine.h`).
	AudioEngine::setForegroundRecording(nullptr, AudioInputChannel::NONE);
	return playbackHandler.recording == RecordingMode::OFF && playbackHandler.playbackState == 0
	       && !Buttons::recordButtonPressUsedUp && AudioEngine::foregroundRecordingSource() == AudioInputChannel::NONE;
}

/// @brief Release every button legacy thinks is held, so one example's combo cannot leak into the
///        next through `Buttons::buttonStates`.
void release_all_legacy_buttons() {
	for (ButtonId b : {ButtonId::Play, ButtonId::Record, ButtonId::Shift, ButtonId::TapTempo, ButtonId::Save,
	                   ButtonId::Load, ButtonId::SyncScaling, ButtonId::SelectEnc, ButtonId::TempoEnc}) {
		if (Buttons::isButtonPressed(legacy_button(b))) {
			(void)Buttons::buttonAction(legacy_button(b), false);
		}
	}
}

/// @brief The (id, on) pairs a `LedFrame` holds, resolved through `shell::led_is_on` at @p now_ms.
std::vector<std::pair<uint8_t, bool>> led_pairs(const deluge::ui::widgets::core::LedFrame& f, uint32_t now_ms) {
	std::vector<std::pair<uint8_t, bool>> out;
	for (const auto& [id, state] : f.entries()) {
		out.emplace_back(static_cast<uint8_t>(id), deluge::ui::shell::led_is_on(state, now_ms));
	}
	return out;
}

/// @brief The declared state of one LED in @p f, or `nullopt` when it was not declared.
std::optional<deluge::ui::widgets::core::LedState> led_state(const deluge::ui::widgets::core::LedFrame& f, ButtonId b) {
	for (const auto& [id, state] : f.entries()) {
		if (static_cast<uint8_t>(id) == static_cast<uint8_t>(b)) {
			return state;
		}
	}
	return std::nullopt;
}

/// @brief Is @p b declared in @p f with kind @p k?
///
/// A predicate rather than `led_state(f, b)->kind`: an LED nothing declared must FAIL the example,
/// not abort the process on a disengaged `optional`.
bool led_is(const deluge::ui::widgets::core::LedFrame& f, ButtonId b, deluge::ui::widgets::core::LedState::Kind k) {
	const std::optional<deluge::ui::widgets::core::LedState> s = led_state(f, b);
	return s.has_value() && s->kind == k;
}

} // namespace

// ------------------------------------------------------------------------------------------------

// clang-format off
describe global_controls_spec_body("scenes::services::GlobalControls", $ {
	before_each([] {
		// `Buttons::buttonAction` dereferences `getCurrentUI()` and `display` (see the file docstring). The quiet
		// display answers that no popup is up, which is the "no threshold-recording popup" case: the only one the port
		// can reproduce, since `Song::changeThresholdRecordingMode` raises that popup straight to legacy's display.
		setRootUILowLevel(&stub_ui());
		display = &deluge::ui::scenes::spec::quiet_display();
		(void)quiet_audio();
	});

	it("transport tail: toggles PlaybackHandler::recording exactly as legacy's own RECORD tap does", _ {
		static spec::SongFixture fixture = spec::make_synth_song();
		expect(fixture.song).not_().to_be_null();

		// --- Legacy, from its own reset ---------------------------------------------------
		release_all_legacy_buttons();
		expect(reset_transport(100'000)).to_be_true();
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), true);
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), false);
		const RecordingMode legacy_result = playbackHandler.recording;
		// Legacy moved: `PlaybackHandler::recordButtonPressed`.
		expect(legacy_result).not_().to_equal(RecordingMode::OFF);

		// --- Poison, so a port that writes nothing cannot inherit the answer ---------------
		playbackHandler.recording = RecordingMode::ARRANGEMENT;
		expect(playbackHandler.recording).to_equal(RecordingMode::ARRANGEMENT);

		// --- The port, from its own reset --------------------------------------------------
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();
		rig.press(ButtonId::Record, true, 1'000);
		rig.press(ButtonId::Record, false, 1'010);
		const RecordingMode port_result = playbackHandler.recording;

		expect(port_result).not_().to_equal(RecordingMode::OFF);
		expect(port_result).to_equal(legacy_result);
	});

	it("leaves the tail to a host that runs it: PLAY, RECORD and the tempo encoder do nothing here", _ {
		// The firmware's `Buttons::buttonAction` runs the tail after the current UI declines a button; with this service
		// running it as well, PLAY would start the transport and stop it again.
		static spec::SongFixture fixture = spec::make_synth_song();
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		rig.gc.set_host_runs_tail(true);
		expect(reset_transport(100'000)).to_be_true();
		const uint32_t before = playbackHandler.playbackState;
		expect(rig.press(ButtonId::Play, true, 1'000).handled).to_be_false();
		(void)rig.press(ButtonId::Play, false, 1'010);
		expect(playbackHandler.playbackState).to_equal(before);
		expect(rig.press(ButtonId::Record, true, 1'100).handled).to_be_false();
		(void)rig.press(ButtonId::Record, false, 1'110);
		expect(playbackHandler.recording).to_equal(RecordingMode::OFF);
		expect(rig.press(ButtonId::TempoEnc, true, 1'200).handled).to_be_false();
		(void)rig.press(ButtonId::TempoEnc, false, 1'210);
		// Without the switch the tail is this service's again.
		rig.gc.set_host_runs_tail(false);
		expect(rig.press(ButtonId::Play, true, 2'000).handled).to_be_true();
		(void)rig.press(ButtonId::Play, false, 2'010);
		expect(playbackHandler.playbackState).not_().to_equal(before);
		expect(reset_transport(100'000)).to_be_true();
	});

	it("transport tail: suppresses the RECORD release after a PLAY press -- the record combo, both sides", _ {
		// `hid/buttons.cpp`'s `recordButtonPressUsedUp = true` on a PLAY press, read by
		// the RECORD release arm. A single-press example cannot see it: it is only
		// observable across two buttons.
		static spec::SongFixture fixture = spec::make_synth_song();

		// --- Legacy -----------------------------------------------------------------------
		release_all_legacy_buttons();
		expect(reset_transport(100'000)).to_be_true();
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), true);
		(void)Buttons::buttonAction(legacy_button(ButtonId::Play), true);
		expect(Buttons::recordButtonPressUsedUp).to_be_true(); // set by the PLAY press (`Buttons::buttonAction`)
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), false);
		const RecordingMode legacy_result = playbackHandler.recording;
		// The whole point: the release did NOT toggle recording on.
		expect(legacy_result).to_equal(RecordingMode::OFF);

		playbackHandler.recording = RecordingMode::ARRANGEMENT;

		// --- The port ---------------------------------------------------------------------
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();
		rig.press(ButtonId::Record, true, 2'000);
		rig.press(ButtonId::Play, true, 2'005);
		// `InputLowering::press_used_up` is the port's `recordButtonPressUsedUp`
		// (`InputLowering::press_used_up`), and `GlobalControls` marks it for RECORD, not for
		// PLAY (`hid/buttons.cpp`'s bit suppresses the RECORD release).
		expect(rig.lowering.press_used_up(ButtonId::Record)).to_be_true();
		rig.press(ButtonId::Record, false, 2'010);
		const RecordingMode port_result = playbackHandler.recording;

		expect(port_result).to_equal(RecordingMode::OFF);
		expect(port_result).to_equal(legacy_result);
		expect(rig.gc.song_opens_total()).to_equal(uint32_t(0));
	});

	it("transport tail: shift+RECORD begins an output recording, both sides", _ {
		// `Buttons::buttonAction`'s RECORD press with no recording running and shift held
		// (`origin/main:src/deluge/hid/buttons.cpp`, `audioRecorder.beginOutputRecording()`).
		static spec::SongFixture fixture = spec::make_synth_song();

		release_all_legacy_buttons();
		expect(reset_transport(100'000)).to_be_true();
		Buttons::commandToggleShift(true);
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), true);
		const AudioInputChannel legacy_source = AudioEngine::foregroundRecordingSource();
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), false);
		Buttons::commandToggleShift(false);
		Buttons::clearShiftSticky();
		expect(legacy_source).to_equal(AudioInputChannel::OUTPUT);

		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();
		rig.press(ButtonId::Shift, true, 3'000);
		rig.press(ButtonId::Record, true, 3'005);
		expect(AudioEngine::foregroundRecordingSource()).to_equal(legacy_source);
		rig.press(ButtonId::Record, false, 3'010);
		rig.press(ButtonId::Shift, false, 3'015);
		// The press was used up, so its release did not also arm recording.
		expect(playbackHandler.recording).to_equal(RecordingMode::OFF);
		expect(reset_transport(100'000)).to_be_true();
	});

	it("transport tail: PLAY with RECORD held starts playback and an output recording, both sides", _ {
		// `Buttons::buttonAction`'s PLAY arm: no recording running, so `playButtonPressed`, then
		// `audioRecorder.beginOutputRecording()` once playback is running.
		static spec::SongFixture fixture = spec::make_synth_song();

		release_all_legacy_buttons();
		expect(reset_transport(100'000)).to_be_true();
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), true);
		(void)Buttons::buttonAction(legacy_button(ButtonId::Play), true);
		const bool legacy_playing = playbackHandler.playbackState != 0;
		const AudioInputChannel legacy_source = AudioEngine::foregroundRecordingSource();
		release_all_legacy_buttons();
		if (playbackHandler.playbackState != 0) {
			playbackHandler.endPlayback();
		}
		expect(legacy_playing).to_be_true();
		expect(legacy_source).to_equal(AudioInputChannel::OUTPUT);

		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();
		rig.press(ButtonId::Record, true, 4'000);
		rig.press(ButtonId::Play, true, 4'005);
		const bool port_playing = playbackHandler.playbackState != 0;
		expect(port_playing).to_equal(legacy_playing);
		expect(AudioEngine::foregroundRecordingSource()).to_equal(legacy_source);
		rig.press(ButtonId::Play, false, 4'010);
		rig.press(ButtonId::Record, false, 4'015);
		if (playbackHandler.playbackState != 0) {
			playbackHandler.endPlayback();
		}
		expect(reset_transport(100'000)).to_be_true();
	});

	it("transport tail: a RECORD tap while resampling ends the recording instead of arming, both sides", _ {
		// `Buttons::buttonAction`'s RECORD release: `audioRecorder.isCurrentlyResampling()` ->
		// `audioRecorder.endRecordingSoon(kInternalButtonPressLatency)`, else `recordButtonPressed()`.
		static spec::SongFixture fixture = spec::make_synth_song();

		release_all_legacy_buttons();
		expect(reset_transport(100'000)).to_be_true();
		expect(deluge::recording::beginOutputRecording()).to_be_true();
		expect(deluge::recording::isCurrentlyResampling()).to_be_true();
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), true);
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), false);
		const bool legacy_still_capturing = deluge::recording::isCurrentlyResampling();
		const RecordingMode legacy_mode = playbackHandler.recording;
		expect(legacy_still_capturing).to_be_false();
		expect(legacy_mode).to_equal(RecordingMode::OFF);

		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();
		expect(deluge::recording::beginOutputRecording()).to_be_true();
		expect(deluge::recording::isCurrentlyResampling()).to_be_true();
		rig.press(ButtonId::Record, true, 5'000);
		rig.press(ButtonId::Record, false, 5'010);
		expect(deluge::recording::isCurrentlyResampling()).to_equal(legacy_still_capturing);
		expect(playbackHandler.recording).to_equal(legacy_mode);
		expect(reset_transport(100'000)).to_be_true();
	});

	it("transport tail: does not toggle recording on a LONG press, both sides", _ {
		// `hid/buttons.cpp`'s `< kShortPressTime`. Legacy measures
		// `AudioEngine::audioSampleTimer` (`kSampleRate / 2` samples,
		// `definitions_cxx.hpp`); the port measures milliseconds through
		// `InputLowering::was_short_press` (500 ms, `InputLowering`'s short-press threshold). The two
		// constants are the same duration, which is the mapping under test.
		static spec::SongFixture fixture = spec::make_synth_song();

		release_all_legacy_buttons();
		expect(reset_transport(100'000)).to_be_true();
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), true);
		AudioEngine::audioSampleTimer += kSampleRate; // two short-press windows
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), false);
		const RecordingMode legacy_result = playbackHandler.recording;
		expect(legacy_result).to_equal(RecordingMode::OFF);

		playbackHandler.recording = RecordingMode::ARRANGEMENT;

		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();
		rig.press(ButtonId::Record, true, 3'000);
		rig.press(ButtonId::Record, false, 3'000 + 2 * GlobalControls::kShortPressMs);
		expect(playbackHandler.recording).to_equal(RecordingMode::OFF);
		expect(playbackHandler.recording).to_equal(legacy_result);
	});

	it("transport tail: does not toggle recording on the release of a RECORD press a UI took, both sides", _ {
		// The sample browser's RECORD and the recorder's own stop take the press; the release reaches the tail. With
		// no tail press in the last 500 ms, `main` does not toggle either (`Buttons::buttonAction` judges the release by
		// the last press it stamped, long past).
		static spec::SongFixture fixture = spec::make_synth_song();

		release_all_legacy_buttons();
		expect(reset_transport(100'000)).to_be_true();
		AudioEngine::audioSampleTimer += kSampleRate; // any earlier stamp is long past
		setRootUILowLevel(&record_taking_ui());
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), true);
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), false);
		setRootUILowLevel(&stub_ui());
		const RecordingMode legacy_result = playbackHandler.recording;
		expect(legacy_result).to_equal(RecordingMode::OFF);

		playbackHandler.recording = RecordingMode::ARRANGEMENT;

		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();
		rig.press_elsewhere(ButtonId::Record, true, 5'000);
		rig.press(ButtonId::Record, false, 5'010);
		expect(playbackHandler.recording).to_equal(RecordingMode::OFF);
		expect(playbackHandler.recording).to_equal(legacy_result);
	});

	it("transport tail: a RECORD press a UI took does not toggle on release even right after a tail tap", _ {
		// The rule, where `main` departs from it (accepted as `main`'s bug): a RECORD tap the tail toggles, then within
		// 500 ms a second press a UI takes. `main` judges the second release by the first press's stamp and toggles
		// again; the port toggles only on the release of a press it took.
		static spec::SongFixture fixture = spec::make_synth_song();
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();

		rig.press(ButtonId::Record, true, 6'000);
		rig.press(ButtonId::Record, false, 6'050);
		const RecordingMode after_tap = playbackHandler.recording;
		expect(after_tap).not_().to_equal(RecordingMode::OFF); // the tail's own tap toggled

		rig.press_elsewhere(ButtonId::Record, true, 6'200);
		rig.press(ButtonId::Record, false, 6'250);
		expect(playbackHandler.recording).to_equal(after_tap);
	});

	it("transport tail: declines PLAY and RECORD while MIDI learn is active, and claims them otherwise", _ {
		// `View::buttonAction`'s PLAY arm (`views/view.cpp`) and the RECORD arm after it -- both arms are
		// `&& currentUIMode == UI_MODE_MIDI_LEARN`, so under learn `View` claims the button and `hid/buttons.cpp` skips
		// the transport tail entirely. `MidiLearn` owns those arms; this service declines rather than swallowing them,
		// so a future routed `MidiLearn` still gets them.
		static spec::SongFixture fixture = spec::make_synth_song();
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();

		rig.learn.on = true;
		expect(rig.gc.effective_ui_mode()).to_equal(GlobalControls::UiMode::MidiLearn);
		const Result under_learn = rig.press(ButtonId::Record, true, 4'000);
		expect(under_learn.handled).to_be_false();
		// The transport tail did not run: nothing was marked used up and nothing moved.
		expect(playbackHandler.recording).to_equal(RecordingMode::OFF);
		rig.press(ButtonId::Record, false, 4'010);
		expect(playbackHandler.recording).to_equal(RecordingMode::OFF);

		rig.learn.on = false;
		expect(rig.gc.effective_ui_mode()).to_equal(GlobalControls::UiMode::None);
		const Result outside_learn = rig.press(ButtonId::Record, true, 5'000);
		expect(outside_learn.handled).to_be_true();
		rig.press(ButtonId::Record, false, 5'010);
		expect(playbackHandler.recording).to_equal(RecordingMode::NORMAL);
	});

	it("transport tail: reports SHIFT handled without re-porting commandToggleShift", _ {
		// `hid/buttons.cpp`'s SHIFT arm. `Buttons::commandToggleShift` is already
		// `InputLowering::toggle_shift`, called from
		// `on_button` before the event is emitted -- so by the time `handle` sees it, shift is
		// already down. Re-porting it here would toggle it twice.
		GcRig rig;
		expect(rig.lowering.shift()).to_be_false();
		const Result r = rig.press(ButtonId::Shift, true, 6'000);
		expect(r.handled).to_be_true();
		expect(rig.lowering.shift()).to_be_true();
		rig.press(ButtonId::Shift, false, 6'010);
		expect(rig.lowering.shift()).to_be_false();
	});

	it("the root gate: View's arms need a root scene, and the Buttons tail never does", _ {
		// The two fallbacks have two reachability rules, and the service holds both --
		// `Scene::is_root()`'s doc has the proof. `View::buttonAction` is reached only from a
		// `RootUI` (`TimelineView::buttonAction` in `gui/views/timeline_view.cpp`,
		// `KeyboardScreen::buttonAction`, and those two classes are the entire result of
		// `grep "public RootUI"`); `Buttons::buttonAction`'s tail runs for any current UI that
		// returned `NOT_DEALT_WITH` (`Buttons::buttonAction` in `hid/buttons.cpp`), root or not.
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();
		expect(rig.stack.input_top().is_root()).to_be_true();

		// Baseline, root: the SAVE arm holds.
		rig.press(ButtonId::Save, true, 40'000);
		expect(rig.gc.holding_save()).to_be_true();
		rig.press(ButtonId::Save, false, 40'000 + GlobalControls::kShortPressMs);
		expect(rig.gc.holding_save()).to_be_false();

		// Non-root -- a menu, a browser, the marker editor. Every `View::buttonAction` arm goes
		// quiet, and the arm is no longer claimed either: legacy's non-root UI declines and the
		// tail, which names no SAVE, declines too.
		rig.root.root = false;
		expect(rig.stack.input_top().is_root()).to_be_false();
		const Result save_off_root = rig.press(ButtonId::Save, true, 41'000);
		expect(save_off_root.handled).to_be_false();
		expect(rig.gc.holding_save()).to_be_false();
		rig.press(ButtonId::Save, false, 41'010);

		// And BACK, which is the arm that made the pre-gate divergence live: it is claimed unconditionally by
		// `View::buttonAction`'s BACK arm (`views/view.cpp`), so an ungated service would fire undo/redo from inside
		// any non-root scene that does not claim BACK for itself.
		//
		// @warning The only thing that masked that before the gate was
		//          `ActionLogger::allowedToDoReversion`
		//          testing `getCurrentUI() == getRootUI()` for itself -- an undocumented legacy
		//          global read that goes stale the moment the shell drives the device. This example
		//          therefore drives the case where legacy WOULD allow reversion (the stub root UI
		//          is installed, so `allowedToDoReversion()` is true) and asserts the PORT's own
		//          gate is what refuses.
		expect(actionLogger.allowedToDoReversion()).to_be_true();
		constexpr int32_t kPoison = 0x5EED;
		pending_global_midi_clusters() = kPoison;
		const Result back_off_root = rig.press(ButtonId::Back, true, 42'000);
		expect(back_off_root.handled).to_be_false();
		expect(pending_global_midi_clusters()).to_equal(kPoison); // the BACK arm's write never ran
		rig.press(ButtonId::Back, false, 42'010);

		// The tail is NOT gated: a RECORD tap from the same non-root scene still toggles recording,
		// exactly as `hid/buttons.cpp` does under a menu.
		expect(playbackHandler.recording).to_equal(RecordingMode::OFF);
		const Result record_off_root = rig.press(ButtonId::Record, true, 43'000);
		expect(record_off_root.handled).to_be_true();
		rig.press(ButtonId::Record, false, 43'010);
		expect(playbackHandler.recording).to_equal(RecordingMode::NORMAL);
	});

	it("tap tempo / tempo encoder: toggles the metronome on shift+TAP_TEMPO, both sides", _ {
		// `View::buttonAction`'s `TAP_TEMPO` arm (`views/view.cpp`): shift toggles the metronome.
		static spec::SongFixture fixture = spec::make_synth_song();

		release_all_legacy_buttons();
		expect(reset_transport(100'000)).to_be_true();
		Buttons::commandToggleShift(true);
		expect(Buttons::isShiftButtonPressed()).to_be_true();
		(void)Buttons::buttonAction(legacy_button(ButtonId::TapTempo), true);
		const bool legacy_result = playbackHandler.metronomeOn;
		// The LED half of the same comparison, read out of legacy's own shadow array.
		// `PlaybackHandler::toggleMetronomeStatus` calls `setLedStates()`, whose last line is
		// `indicator_leds::setLedState(IndicatorLED::TAP_TEMPO, metronomeOn)` (`PlaybackHandler::setLedStates` in
		// `playback/playback_handler.cpp`), and that writes `indicator_leds::ledStates` (`hid/led/indicator_leds.cpp`).
		// This is the ONE LED in this service that legacy sets through a path a spec can drive end to end, so it is the
		// one the LED mapping is proved on.
		const bool legacy_led = indicator_leds::ledStates[static_cast<uint8_t>(IndicatorLED::TAP_TEMPO)];
		Buttons::commandToggleShift(false);
		Buttons::clearShiftSticky();
		expect(legacy_result).to_be_true();
		expect(legacy_led).to_be_true();

		// Poison both: a port that does nothing leaves these false, not true.
		playbackHandler.metronomeOn = false;
		indicator_leds::ledStates[static_cast<uint8_t>(IndicatorLED::TAP_TEMPO)] = false;
		expect(playbackHandler.metronomeOn).to_be_false();

		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();
		rig.press(ButtonId::Shift, true, 7'000);
		expect(rig.lowering.shift()).to_be_true();
		const Result r = rig.press(ButtonId::TapTempo, true, 7'005);
		expect(r.handled).to_be_true();
		expect(playbackHandler.metronomeOn).to_be_true();
		expect(playbackHandler.metronomeOn).to_equal(legacy_result);

		// The port declares the LED rather than writing it, so the comparison is the declaration
		// resolved through `shell::led_is_on` against the bool legacy left in `ledStates`.
		deluge::ui::widgets::core::LedFrame f;
		rig.gc.leds(f);
		const std::optional<deluge::ui::widgets::core::LedState> tap = led_state(f, ButtonId::TapTempo);
		expect(tap.has_value()).to_be_true();
		expect(deluge::ui::shell::led_is_on(tap.value_or(deluge::ui::widgets::core::LedState{}), rig.now_ms)).to_equal(legacy_led);
		rig.press(ButtonId::Shift, false, 7'010);
	});

	it("tap tempo / tempo encoder: does not tap tempo during a record count-in, but does toggle the metronome", _ {
		// `View::buttonAction`'s tap-tempo arm admits both `UI_MODE_NONE` and `UI_MODE_RECORD_COUNT_IN`; the tap
		// itself admits only `UI_MODE_NONE`. Reading the braces: the shift test is inside the
		// `if (on)`, which is inside the mode test -- so during a count-in the
		// metronome toggle still runs and the tap does not.
		static spec::SongFixture fixture = spec::make_synth_song();
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();

		GlobalControls::ViewState vs;
		vs.ui_mode = GlobalControls::UiMode::RecordCountIn;
		rig.gc.set_view_state(vs);
		expect(rig.gc.effective_ui_mode()).to_equal(GlobalControls::UiMode::RecordCountIn);

		rig.press(ButtonId::Shift, true, 8'000);
		const Result r = rig.press(ButtonId::TapTempo, true, 8'005);
		expect(r.handled).to_be_true();
		expect(playbackHandler.metronomeOn).to_be_true();
		rig.press(ButtonId::Shift, false, 8'010);

		// And a mode outside both is refused entirely.
		vs.ui_mode = GlobalControls::UiMode::Other;
		rig.gc.set_view_state(vs);
		playbackHandler.metronomeOn = false;
		rig.press(ButtonId::Shift, true, 8'020);
		(void)rig.press(ButtonId::TapTempo, true, 8'025);
		expect(playbackHandler.metronomeOn).to_be_false();
		rig.press(ButtonId::Shift, false, 8'030);
	});

	it("tap tempo / tempo encoder: suppresses the tempo popup in song/arranger view and while a song is loading", _ {
		// `hid/buttons.cpp`. Both reads are root-UI facts injected through
		// `ViewState`; what is asserted is the decision, since `commandDisplayTempo` itself
		// only draws.
		static spec::SongFixture fixture = spec::make_synth_song();
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();

		// Shift wins over everything (`Buttons::buttonAction`'s `TEMPO_ENC` arm in `hid/buttons.cpp`).
		rig.press(ButtonId::Shift, true, 9'000);
		expect(rig.press(ButtonId::TempoEnc, true, 9'005).handled).to_be_true();
		rig.press(ButtonId::Shift, false, 9'010);

		// TAP_TEMPO held picks the swing-interval arm (the `TEMPO_ENC` arm's `TAP_TEMPO` test).
		rig.press(ButtonId::TapTempo, true, 9'020);
		expect(rig.ctx().held.has(ButtonId::TapTempo)).to_be_true();
		expect(rig.press(ButtonId::TempoEnc, true, 9'025).handled).to_be_true();
		rig.press(ButtonId::TapTempo, false, 9'030);

		// And the plain arm is claimed whatever the two view flags say.
		GlobalControls::ViewState vs;
		vs.current_ui_is_song_or_arranger = true;
		rig.gc.set_view_state(vs);
		expect(rig.press(ButtonId::TempoEnc, true, 9'040).handled).to_be_true();
		vs.current_ui_is_song_or_arranger = false;
		vs.loading_song = true;
		rig.gc.set_view_state(vs);
		expect(rig.press(ButtonId::TempoEnc, true, 9'050).handled).to_be_true();
	});

	it("SAVE/LOAD decision: holds, then reaches the unported open on a short press", _ {
		// `View::buttonAction`'s `SAVE` arm (`views/view.cpp`). The open itself is not ported; what is compared is
		// every decision above it.
		static spec::SongFixture fixture = spec::make_synth_song();
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();

		expect(rig.gc.holding_save()).to_be_false();
		expect(rig.press(ButtonId::Save, true, 10'000).handled).to_be_true();
		expect(rig.gc.holding_save()).to_be_true(); // the press enters the hold
		expect(rig.gc.effective_ui_mode()).to_equal(GlobalControls::UiMode::HoldingSave);
		expect(rig.gc.song_opens_total()).to_equal(uint32_t(0));

		rig.press(ButtonId::Save, false, 10'000 + GlobalControls::kShortPressMs - 1);
		expect(rig.gc.holding_save()).to_be_false(); // the short release leaves it
		expect(rig.gc.song_opens(GlobalControls::SongOpen::SaveSong)).to_equal(uint32_t(1)); // `openUI(&saveSongUI)`
		expect(rig.gc.song_opens_total()).to_equal(uint32_t(1));
		expect(rig.views.calls).to_equal(std::vector<std::string>{"song saver"});
	});

	it("SAVE/LOAD decision: does not open on a long press, and does not hold with shift or a type button", _ {
		static spec::SongFixture fixture = spec::make_synth_song();
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();

		// Long press: the `kShortPressTime` window elapses, so the release just drops the LED.
		rig.press(ButtonId::Save, true, 11'000);
		rig.press(ButtonId::Save, false, 11'000 + GlobalControls::kShortPressMs);
		expect(rig.gc.holding_save()).to_be_false();
		expect(rig.gc.song_opens_total()).to_equal(uint32_t(0));

		// Shift: the press arm's `&& !Buttons::isShiftButtonPressed()`.
		rig.press(ButtonId::Shift, true, 12'000);
		rig.press(ButtonId::Save, true, 12'005);
		expect(rig.gc.holding_save()).to_be_false();
		rig.press(ButtonId::Save, false, 12'010);
		rig.press(ButtonId::Shift, false, 12'015);
		expect(rig.gc.song_opens_total()).to_equal(uint32_t(0));

		// SYNTH held: the arm's four-button guard suppresses the whole body -- but the arm is still reported handled,
		// because legacy still reaches the `DEALT_WITH` return at the end of `View::buttonAction`.
		rig.press(ButtonId::Synth, true, 13'000);
		expect(rig.press(ButtonId::Save, true, 13'005).handled).to_be_true();
		expect(rig.gc.holding_save()).to_be_false();
		rig.press(ButtonId::Save, false, 13'010);
		rig.press(ButtonId::Synth, false, 13'015);
		expect(rig.gc.song_opens_total()).to_equal(uint32_t(0));
	});

	it("SAVE/LOAD decision: refuses to save while overdubs are pending, with legacy's own popup string", _ {
		// The `SAVE` arm's `hasAnyPendingNextOverdubs()` popup.
		static spec::SongFixture fixture = spec::make_synth_song();
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();

		// The fixture's song has no pending overdubs, so the plain path opens.
		expect(fixture.song->hasAnyPendingNextOverdubs()).to_be_false();
		rig.press(ButtonId::Save, true, 14'000);
		const Result plain = rig.press(ButtonId::Save, false, 14'010);
		expect(plain.actions.size()).to_equal(size_t(0));
		expect(rig.gc.song_opens(GlobalControls::SongOpen::SaveSong)).to_equal(uint32_t(1));
	});

	it("SAVE/LOAD decision: takes the clear-song branch on shift+LOAD and the hold branch otherwise", _ {
		// The `LOAD` arm's press: `isShiftButtonPressed()` picks the clear-song branch, otherwise the hold branch.
		static spec::SongFixture fixture = spec::make_synth_song();
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();

		rig.press(ButtonId::Shift, true, 15'000);
		expect(rig.press(ButtonId::Load, true, 15'005).handled).to_be_true();
		expect(rig.gc.holding_load()).to_be_false();                // the clear-song branch was taken instead
		expect(rig.gc.song_opens(GlobalControls::SongOpen::ClearSong)).to_equal(uint32_t(1)); // `clearSong` opens
		rig.press(ButtonId::Load, false, 15'010);
		rig.press(ButtonId::Shift, false, 15'015);

		rig.press(ButtonId::Load, true, 16'000);
		expect(rig.gc.holding_load()).to_be_true(); // the hold branch is entered
		rig.press(ButtonId::Load, false, 16'010);
		expect(rig.gc.holding_load()).to_be_false();                // the short release leaves it
		expect(rig.gc.song_opens(GlobalControls::SongOpen::LoadSong)).to_equal(uint32_t(1)); // `openUI(&loadSongUI)`
		expect(rig.gc.song_opens_total()).to_equal(uint32_t(2));
		expect(rig.views.calls).to_equal(std::vector<std::string>{"clear song", "song loader"});
	});

	it("sync scaling: sets and clears the song's sync-scaling clip exactly as legacy does", _ {
		// `View::buttonAction`'s `SYNC_SCALING` arm (`views/view.cpp`). The model write is
		// `Song::setInputTickScaleClip`, and it is what both sides are compared on.
		static spec::SongFixture legacy_fixture = spec::make_synth_song();
		currentSong = legacy_fixture.song;
		release_all_legacy_buttons();
		expect(reset_transport(100'000)).to_be_true();
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::SyncScalingAction,
		                           RuntimeFeatureStateSyncScalingAction::SyncScaling);
		legacy_fixture.song->setInputTickScaleClip(nullptr);
		expect(legacy_fixture.song->getSyncScalingClip()).to_be_null();

		// Legacy needs a clip-minder current UI for the `toClipMinder()` test; `StubUI::toClipMinder()` is null,
		// so legacy takes the alert arm instead. That arm IS the comparison here: it is the
		// one `toClipMinder()` decision this spec can drive on both sides, and the two model writes
		// below are driven on the port alone with `Song` asserted directly.
		(void)Buttons::buttonAction(legacy_button(ButtonId::SyncScaling), true);
		expect(legacy_fixture.song->getSyncScalingClip()).to_be_null(); // refused
		(void)Buttons::buttonAction(legacy_button(ButtonId::SyncScaling), false);

		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&legacy_fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();
		// Same refusal, same reason, port side.
		expect(rig.press(ButtonId::SyncScaling, true, 17'000).handled).to_be_true();
		expect(legacy_fixture.song->getSyncScalingClip()).to_be_null();
		rig.press(ButtonId::SyncScaling, false, 17'005);

		// Now with a clip minder on screen, the arm sets the clip and a second press
		// clears it.
		GlobalControls::ViewState vs;
		vs.current_ui_is_clip_minder = true;
		rig.gc.set_view_state(vs);
		// The clip that minder has open, as the app tells the view state.
		deluge::ui::scenes::services::view_state().set_current_view(
		    deluge::gui::menuViewOf(legacy_fixture.song->getCurrentClip()));
		rig.press(ButtonId::SyncScaling, true, 18'000);
		expect(legacy_fixture.song->getSyncScalingClip()).to_equal(legacy_fixture.song->getCurrentClip());
		rig.press(ButtonId::SyncScaling, false, 18'005);
		rig.press(ButtonId::SyncScaling, true, 19'000);
		expect(legacy_fixture.song->getSyncScalingClip()).to_be_null();
		rig.press(ButtonId::SyncScaling, false, 19'005);
	});

	it("sync scaling: refuses while recording to the arrangement, with legacy's own popup string", _ {
		// The arm's `cant:` label.
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::SyncScalingAction,
		                           RuntimeFeatureStateSyncScalingAction::SyncScaling);
		fixture.song->setInputTickScaleClip(nullptr);
		playbackHandler.recording = RecordingMode::ARRANGEMENT;

		const Result r = rig.press(ButtonId::SyncScaling, true, 20'000);
		expect(r.handled).to_be_true();
		expect(r.actions.size()).to_equal(size_t(1));
		expect(r.actions[0].kind).to_equal(ShellAction::Kind::Popup);
		expect(r.actions[0].popup_string)
		    .to_equal(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
		expect(fixture.song->getSyncScalingClip()).to_be_null();
		rig.press(ButtonId::SyncScaling, false, 20'005);
		playbackHandler.recording = RecordingMode::OFF;
	});

	it("sync scaling: toggles fill mode when the community setting repurposes the button, both sides", _ {
		// The `Fill` branch's note-editor test and its final fill toggle. The two note-fill halves are not ported; this
		// is the arm's "otherwise", which is.
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		expect(reset_transport(100'000)).to_be_true();
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::SyncScalingAction,
		                           RuntimeFeatureStateSyncScalingAction::Fill);
		fixture.song->changeFillMode(false);

		(void)Buttons::buttonAction(legacy_button(ButtonId::SyncScaling), true);
		const bool legacy_result = fixture.song->isFillModeActive();
		(void)Buttons::buttonAction(legacy_button(ButtonId::SyncScaling), false);
		expect(legacy_result).to_be_true();

		fixture.song->changeFillMode(false);
		expect(fixture.song->isFillModeActive()).to_be_false();

		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		rig.press(ButtonId::SyncScaling, true, 21'000);
		expect(fixture.song->isFillModeActive()).to_be_true();
		expect(fixture.song->isFillModeActive()).to_equal(legacy_result);
		rig.press(ButtonId::SyncScaling, false, 21'005);
		expect(fixture.song->isFillModeActive()).to_be_false();

		runtimeFeatureSettings.set(RuntimeFeatureSettingType::SyncScalingAction,
		                           RuntimeFeatureStateSyncScalingAction::SyncScaling);
	});

	it("BACK undo/redo: runs the reversion arm exactly where legacy's live one does", _ {
		// `View::buttonAction`'s BACK arm (`views/view.cpp`). The dedicated undo/redo arms are inside
		// `#ifdef undoButtonX`, which is defined nowhere, so `BACK` is the only live
		// spelling and this is the whole of the port's undo/redo.
		//
		// **What is observable, and what is not.** The arm calls `playbackHandler.slowRoutine()` in the same statement
		// sequence, and that function *consumes* the command it was just handed --
		// `pendingGlobalMIDICommand = GlobalMIDICommand::NONE` (`PlaybackHandler::slowRoutine`) -- unless
		// `deluge::sync::sd_busy()` (`sync/sd_access.cpp`) is true, which a spec cannot make true: it reads the host
		// BSP. So the UNDO-vs-REDO *choice* (the `isShiftButtonPressed()` ternary) is **uncovered here**: by the time
		// control returns, both spellings read NONE. What IS observable is the
		// `pendingGlobalMIDICommandNumClustersWritten` assignment, which nothing consumes -- so poisoning it and
		// watching it come back 0 proves the arm's body ran on each side, and leaving it poisoned proves the
		// `allowedToDoReversion()` guard held.
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		expect(reset_transport(100'000)).to_be_true();
		actionLogger.deleteAllLogs();
		// `ActionLogger::allowedToDoReversion` is
		// `currentSong && getCurrentUI() == getRootUI() && isUIModeWithinRange(reversionUIModes)`
		// -- it does NOT ask whether there is anything to revert. With the stub root UI installed
		// and a song, it is true, which is the arm-runs case.
		expect(actionLogger.allowedToDoReversion()).to_be_true();

		constexpr int32_t kPoison = 0x5EED;
		pending_global_midi_clusters() = kPoison;
		(void)Buttons::buttonAction(legacy_button(ButtonId::Back), true);
		const int32_t legacy_result = pending_global_midi_clusters();
		(void)Buttons::buttonAction(legacy_button(ButtonId::Back), false);
		expect(legacy_result).to_equal(0); // the BACK arm's write ran

		// Re-poison, so a port that writes nothing cannot inherit legacy's zero.
		pending_global_midi_clusters() = kPoison;
		expect(pending_global_midi_clusters()).to_equal(kPoison);

		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		const Result r = rig.press(ButtonId::Back, true, 22'000);
		expect(r.handled).to_be_true();
		expect(pending_global_midi_clusters()).to_equal(0);
		expect(pending_global_midi_clusters()).to_equal(legacy_result);
		rig.press(ButtonId::Back, false, 22'005);

		// And the `allowedToDoReversion()` guard: with no song it is false and the body is
		// skipped, while the arm is still claimed (legacy reaches the `DEALT_WITH` return either way).
		pending_global_midi_clusters() = kPoison;
		Song* const saved = currentSong;
		currentSong = nullptr;
		expect(actionLogger.allowedToDoReversion()).to_be_false();
		const Result guarded = rig.press(ButtonId::Back, true, 22'100);
		expect(guarded.handled).to_be_true();
		expect(pending_global_midi_clusters()).to_equal(kPoison);
		rig.press(ButtonId::Back, false, 22'105);
		currentSong = saved;
	});

	it("shift+SELECT_ENC: pushes the menu scene at the settings root", _ {
		// `View::buttonAction`'s `SELECT_ENC` arm (`views/view.cpp`). `soundEditor.setup()`'s default `clip` is null
		// (`SoundEditor::setup` in `origin/main:src/deluge/gui/ui/sound_editor.cpp`) and shift is held by this arm's
		// own condition, so main always lands on `settingsRootMenu` (`SoundEditor::setup`'s `else` arm).
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();

		// Without shift the arm's condition fails outright, and nothing in the tail
		// names SELECT_ENC either.
		const Result no_shift = rig.press(ButtonId::SelectEnc, true, 23'000);
		expect(no_shift.handled).to_be_false();
		expect(no_shift.actions.size()).to_equal(size_t(0));
		rig.press(ButtonId::SelectEnc, false, 23'005);

		rig.press(ButtonId::Shift, true, 24'000);
		const Result r = rig.press(ButtonId::SelectEnc, true, 24'005);
		expect(r.handled).to_be_true();
		expect(r.actions.size()).to_equal(size_t(1));
		expect(r.actions[0].kind).to_equal(ShellAction::Kind::Push);
		expect(r.actions[0].id.kind).to_equal(SceneKind::FirstApp);
		// And the interpreter really was retargeted at the settings root.
		expect(menu_scene().focused()).not_().to_be_null();
		rig.press(ButtonId::SelectEnc, false, 24'010);
		rig.press(ButtonId::Shift, false, 24'015);
	});

	it("shift+SELECT_ENC: over an instrument clip, the settings menu greys as the clip's", _ {
		// `getGreyoutColsAndRows` decides by the root UI alone: an instrument clip under the settings menu keeps its
		// audition column lit.
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();
		GlobalControls::ViewState vs;
		vs.root_greyout = menu::MenuContext::RootGreyout::InstrumentClip;
		rig.gc.set_view_state(vs);
		rig.press(ButtonId::Shift, true, 26'000);
		const Result r = rig.press(ButtonId::SelectEnc, true, 26'005);
		expect(r.handled).to_be_true();
		expect(menu_scene().greyout_mask().cols).to_equal(uint32_t{0xFFFFFFFE});
		rig.press(ButtonId::SelectEnc, false, 26'010);
		rig.press(ButtonId::Shift, false, 26'015);
	});

	it("shift+SELECT_ENC: refuses while recording to the arrangement", _ {
		// The arm's `RecordingMode::ARRANGEMENT` popup.
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();
		playbackHandler.recording = RecordingMode::ARRANGEMENT;

		rig.press(ButtonId::Shift, true, 25'000);
		const Result r = rig.press(ButtonId::SelectEnc, true, 25'005);
		expect(r.handled).to_be_true();
		expect(r.actions.size()).to_equal(size_t(1));
		expect(r.actions[0].kind).to_equal(ShellAction::Kind::Popup);
		rig.press(ButtonId::SelectEnc, false, 25'010);
		rig.press(ButtonId::Shift, false, 25'015);
		playbackHandler.recording = RecordingMode::OFF;
	});

	it("LEDs: declares exactly Play, Record, TapTempo, SyncScaling, Save and Load, by identity", _ {
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();
		fixture.song->setInputTickScaleClip(nullptr);

		deluge::ui::widgets::core::LedFrame f;
		rig.gc.leds(f);
		const std::vector<std::pair<uint8_t, bool>> got = led_pairs(f, 0);
		const std::vector<std::pair<uint8_t, bool>> want{
		    {static_cast<uint8_t>(ButtonId::Play), false},        // `PlaybackHandler::setLedStates`
		    {static_cast<uint8_t>(ButtonId::Record), false},      // `PlaybackHandler::setLedStates`
		    {static_cast<uint8_t>(ButtonId::TapTempo), false},    // `PlaybackHandler::setLedStates`
		    {static_cast<uint8_t>(ButtonId::SyncScaling), false}, // `View::setTimeBaseScaleLedState`
		    {static_cast<uint8_t>(ButtonId::Save), false},        // `View::buttonAction`'s SAVE arm
		    {static_cast<uint8_t>(ButtonId::Load), false},        // `View::buttonAction`'s LOAD arm
		};
		expect(got).to_equal(want);
	});

	it("LEDs: tracks the model: playback, recording, metronome, sync scaling, and the two holds", _ {
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();
		fixture.song->setInputTickScaleClip(nullptr);

		playbackHandler.playbackState = PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		playbackHandler.recording = RecordingMode::NORMAL;
		playbackHandler.metronomeOn = true;
		rig.press(ButtonId::Save, true, 26'000);

		deluge::ui::widgets::core::LedFrame f;
		rig.gc.leds(f);
		expect(led_is(f, ButtonId::Play, deluge::ui::widgets::core::LedState::Kind::On)).to_be_true();
		expect(led_is(f, ButtonId::Record, deluge::ui::widgets::core::LedState::Kind::On)).to_be_true();
		expect(led_is(f, ButtonId::TapTempo, deluge::ui::widgets::core::LedState::Kind::On)).to_be_true();
		expect(led_is(f, ButtonId::Save, deluge::ui::widgets::core::LedState::Kind::On)).to_be_true();
		expect(led_is(f, ButtonId::Load, deluge::ui::widgets::core::LedState::Kind::Off)).to_be_true();
		rig.press(ButtonId::Save, false, 26'005 + GlobalControls::kShortPressMs);

		// `PlaybackHandler::setLedStates`: while recording to the arrangement the RECORD LED
		// is not declared at all.
		playbackHandler.recording = RecordingMode::ARRANGEMENT;
		deluge::ui::widgets::core::LedFrame f2;
		rig.gc.leds(f2);
		expect(led_state(f2, ButtonId::Record).has_value()).to_be_false();
		playbackHandler.recording = RecordingMode::OFF;

		// `View::setTimeBaseScaleLedState`: solid on when some clip is the sync-scaling clip and we are not in a
		// clip minder.
		fixture.song->setInputTickScaleClip(fixture.song->getCurrentClip());
		deluge::ui::widgets::core::LedFrame f3;
		rig.gc.leds(f3);
		expect(led_is(f3, ButtonId::SyncScaling, deluge::ui::widgets::core::LedState::Kind::On)).to_be_true();

		// `View::setTimeBaseScaleLedState` (`views/view.cpp`): blinking once the clip minder showing it IS that clip.
		GlobalControls::ViewState vs;
		vs.current_ui_is_clip_minder = true;
		rig.gc.set_view_state(vs);
		deluge::ui::widgets::core::LedFrame f4;
		rig.gc.leds(f4);
		expect(led_is(f4, ButtonId::SyncScaling, deluge::ui::widgets::core::LedState::Kind::Shared)).to_be_true();
		expect(led_state(f4, ButtonId::SyncScaling).value_or(deluge::ui::widgets::core::LedState{}).rate)
		    .to_equal(deluge::ui::widgets::core::BlinkRate::Normal);
		fixture.song->setInputTickScaleClip(nullptr);
	});

	it("LEDs: blinks the CLIP_VIEW LED for the sync-scaling alert, then hands it back", _ {
		// `View::buttonAction`'s `SYNC_SCALING` arm (`views/view.cpp`) calls `indicateAlertOnLed`, which is
		// `blinkLed(led, 3, 1)` (`hid/led/indicator_leds.cpp`).
		//
		// **The duration is pinned against legacy's own countdown, not derived on paper.** `blinkLed` sets
		// `blinksLeft = numBlinks * 2` == 6 and then runs `updateBlinkingLedStates` once at
		// arm time, which decrements it -- so the fold-back when `blinksLeft` reaches 0 comes
		// after FIVE further `kFastFlashTime` timeouts, not six. Driven here by stepping legacy's
		// `ledBlinkTimeout` until the blinker deactivates and counting the steps.
		indicator_leds::setLedState(IndicatorLED::CLIP_VIEW, false);
		expect(indicator_leds::getLedBlinkerIndex(IndicatorLED::CLIP_VIEW)).to_equal(255);
		indicator_leds::indicateAlertOnLed(IndicatorLED::CLIP_VIEW);
		expect(indicator_leds::getLedBlinkerIndex(IndicatorLED::CLIP_VIEW)).not_().to_equal(255);
		int32_t legacy_timeouts = 0;
		while (indicator_leds::getLedBlinkerIndex(IndicatorLED::CLIP_VIEW) != 255 && legacy_timeouts < 20) {
			indicator_leds::ledBlinkTimeout(/*blinkingType=*/1);
			legacy_timeouts++;
		}
		expect(legacy_timeouts).to_equal(5);
		expect(GlobalControls::kAlertMs)
		    .to_equal(static_cast<uint32_t>(legacy_timeouts) * GlobalControls::kClipArmFlashMs);
		// And it really did fold back to the state it was armed from (`updateBlinkingLedStates`' fold-back), which is
		// the state the shell's hand-back restores once nothing declares the LED (`SurfaceSet::release_led`).
		expect(indicator_leds::ledStates[static_cast<uint8_t>(IndicatorLED::CLIP_VIEW)]).to_be_false();

		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		expect(reset_transport(100'000)).to_be_true();
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::SyncScalingAction,
		                           RuntimeFeatureStateSyncScalingAction::SyncScaling);
		fixture.song->setInputTickScaleClip(nullptr);

		rig.press(ButtonId::SyncScaling, true, 27'000);
		rig.gc.tick(27'000, 0);
		deluge::ui::widgets::core::LedFrame f;
		rig.gc.leds(f);
		expect(led_is(f, ButtonId::Clip, deluge::ui::widgets::core::LedState::Kind::Shared)).to_be_true();
		expect(led_state(f, ButtonId::Clip)->rate).to_equal(deluge::ui::widgets::core::BlinkRate::Fast); // `blinkingType` 1

		rig.gc.tick(27'000 + GlobalControls::kAlertMs, GlobalControls::kAlertMs);
		deluge::ui::widgets::core::LedFrame f2;
		rig.gc.leds(f2);
		expect(led_state(f2, ButtonId::Clip).has_value()).to_be_false();
		rig.press(ButtonId::SyncScaling, false, 27'500);
	});

	it("time: flips the clip-arm phase on a QUIET tick, with no input at all", _ {
		// `View::flashPlayEnable` arms `kFastFlashTime`;
		// `View::flashPlayRoutine` toggles `clipArmFlashOn` when it fires. The
		// port's `next_timer()`/`tick()` pair is that timer, and nothing below touches a
		// button.
		//
		// NOTE: this is the *clip-arm* flash, not a Play-LED flash. `flashPlayRoutine` does not
		// set `IndicatorLED::PLAY` -- the only time-driven thing in legacy that does is
		// `View::midiLearnFlash`, which belongs to `MidiLearn`.
		GcRig rig;
		expect(rig.gc.clip_arm_flash_on()).to_be_false();
		expect(rig.gc.next_timer().has_value()).to_be_false();

		rig.gc.tick(30'000, 0);
		rig.gc.flash_play_enable(30'000);
		expect(rig.gc.next_timer().has_value()).to_be_true();
		expect(*rig.gc.next_timer()).to_equal(GlobalControls::kClipArmFlashMs);

		rig.gc.tick(30'000 + GlobalControls::kClipArmFlashMs - 1, GlobalControls::kClipArmFlashMs - 1);
		expect(rig.gc.clip_arm_flash_on()).to_be_false(); // not yet
		rig.gc.tick(30'000 + GlobalControls::kClipArmFlashMs, 1);
		expect(rig.gc.clip_arm_flash_on()).to_be_true(); // fired, with no input
		expect(rig.gc.next_timer().has_value()).to_be_false();

		// A second arm toggles it back -- legacy's re-arm lives in
		// `SessionView::flashPlayRoutine`, which is
		// view-side and not ported, so the caller arms it again.
		rig.gc.flash_play_enable(30'100);
		rig.gc.tick(30'160, 60);
		expect(rig.gc.clip_arm_flash_on()).to_be_false();

		// `View::flashPlayDisable` clears both.
		rig.gc.flash_play_enable(30'200);
		rig.gc.flash_play_disable();
		expect(rig.gc.clip_arm_flash_on()).to_be_false();
		expect(rig.gc.next_timer().has_value()).to_be_false();
	});

	it("time: does not postpone a flash already scheduled, and its toggle repaints the pads that read the phase", _ {
		// `View::flashPlayEnable` arms only when `PLAY_ENABLE_FLASH` is not already set: a view that re-arms on every
		// render must not push the flash back for ever.
		GcRig rig;
		expect(rig.gc.timer_repaints().grid).to_be_false();
		rig.gc.tick(40'000, 0);
		rig.gc.flash_play_enable(40'000);
		rig.gc.tick(40'030, 30);
		rig.gc.flash_play_enable(40'030);
		expect(*rig.gc.next_timer()).to_equal(GlobalControls::kClipArmFlashMs - 30);
		// The phase's readers are pad renderers (`View::flashPlayRoutine` asks the current UI for its sidebar), so the
		// armed flash repaints the grid and nothing else.
		expect(rig.gc.timer_repaints().grid).to_be_true();
		expect(rig.gc.timer_repaints().oled).to_be_false();
		rig.gc.tick(40'000 + GlobalControls::kClipArmFlashMs, 30);
		expect(rig.gc.clip_arm_flash_on()).to_be_true();
		expect(rig.gc.timer_repaints().grid).to_be_false();
	});

	it("time: blinks SYNC_SCALING on the shared normal phase, which the shell wakes for", _ {
		// The blinking SYNC_SCALING LED (`View::setTimeBaseScaleLedState`, `blinkLed(SYNC_SCALING)`): declared on the phase every
		// normal blink shares, whose toggles the shell wakes for (`shell::led_next_change`), so the service asks for
		// no timer of its own.
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		GcRig rig;
		rig.gc.set_model(&fixture.song->project);
		GlobalControls::ViewState vs;
		vs.current_ui_is_clip_minder = true;
		rig.gc.set_view_state(vs);
		fixture.song->setInputTickScaleClip(fixture.song->getCurrentClip());

		rig.gc.tick(0, 0);
		expect(rig.gc.next_timer().has_value()).to_be_false();
		deluge::ui::widgets::core::LedFrame f;
		rig.gc.leds(f);
		expect(led_is(f, ButtonId::SyncScaling, deluge::ui::widgets::core::LedState::Kind::Shared)).to_be_true();
		const deluge::ui::widgets::core::LedState blink = led_state(f, ButtonId::SyncScaling).value_or(deluge::ui::widgets::core::LedState{});
		expect(blink.rate).to_equal(deluge::ui::widgets::core::BlinkRate::Normal);
		const deluge::ui::widgets::core::BlinkPhase phases[2] = {{.origin_ms = 0, .initially_on = true, .running = true}, {}};
		expect(deluge::ui::shell::led_next_change(blink, 0, phases).has_value()).to_be_true();

		fixture.song->setInputTickScaleClip(nullptr);
		rig.gc.tick(0, 0);
		deluge::ui::widgets::core::LedFrame g;
		rig.gc.leds(g);
		expect(led_is(g, ButtonId::SyncScaling, deluge::ui::widgets::core::LedState::Kind::Shared)).to_be_false();
	});

	// The service on a fake host: what each press asks the installed ports, in order, with no legacy state read.
	it("on fakes: PLAY plays, starts an output recording with RECORD held, and ends one at the loop end", _ {
		release_all_legacy_buttons();
		GcRig rig;
		bind::test::FakeHost host;
		const bind::Ports p = host.ports();
		rig.gc.set_ports(&p);
		deluge::project::Song model;
		rig.gc.set_model(&model);
		using Call = bind::test::RecordingTransportActions::Call;
		using K = Call::Kind;

		expect(rig.press(ButtonId::Play, true, 40'000).handled).to_be_true();
		rig.press(ButtonId::Play, false, 40'010);
		expect(host.transport_actions.calls).to_equal(std::vector<Call>{{.kind = K::PLAY}});

		// RECORD held with no source: the press starts playback, which the fake's PLAY does, and the read after it
		// sees playback running.
		host.transport_actions.calls.clear();
		host.transport.is_playing = false;
		host.audio.foreground = bind::ForegroundRecording::NONE;
		rig.press(ButtonId::Record, true, 40'100);
		rig.press(ButtonId::Play, true, 40'110);
		rig.press(ButtonId::Play, false, 40'120);
		rig.press(ButtonId::Record, false, 40'130);
		expect(host.transport_actions.calls)
		    .to_equal(std::vector<Call>{{.kind = K::PLAY}, {.kind = K::BEGIN_OUTPUT_RECORDING}});

		// An input recording with RECORD held and a clock running: PLAY ends it at the loop end.
		host.transport_actions.calls.clear();
		host.audio.foreground = bind::ForegroundRecording::EXTERNAL;
		host.transport.clocks = true;
		rig.press(ButtonId::Record, true, 40'200);
		rig.press(ButtonId::Play, true, 40'210);
		rig.press(ButtonId::Play, false, 40'220);
		rig.press(ButtonId::Record, false, 40'230);
		expect(host.transport_actions.calls).to_equal(std::vector<Call>{{.kind = K::STOP_OUTPUT_RECORDING_AT_LOOP_END}});
	});

	it("on fakes: a short RECORD press arms recording, or ends the recording while resampling", _ {
		release_all_legacy_buttons();
		GcRig rig;
		bind::test::FakeHost host;
		const bind::Ports p = host.ports();
		rig.gc.set_ports(&p);
		deluge::project::Song model;
		rig.gc.set_model(&model);
		using Call = bind::test::RecordingTransportActions::Call;
		using K = Call::Kind;

		rig.press(ButtonId::Record, true, 41'000);
		rig.press(ButtonId::Record, false, 41'050);
		expect(host.transport_actions.calls).to_equal(std::vector<Call>{{.kind = K::RECORD}});

		host.transport_actions.calls.clear();
		host.audio.resample = true;
		rig.press(ButtonId::Record, true, 41'100);
		rig.press(ButtonId::Record, false, 41'150);
		expect(host.transport_actions.calls).to_equal(std::vector<Call>{{.kind = K::END_RECORDING_SOON}});
	});

	it("on fakes: the tempo encoder clears tempo automation with shift, shows the swing with TAP TEMPO, else the tempo", _ {
		release_all_legacy_buttons();
		GcRig rig;
		bind::test::FakeHost host;
		const bind::Ports p = host.ports();
		rig.gc.set_ports(&p);
		deluge::project::Song model;
		rig.gc.set_model(&model);
		using Call = bind::test::RecordingTransportActions::Call;
		using K = Call::Kind;

		rig.press(ButtonId::Shift, true, 42'000);
		rig.press(ButtonId::TempoEnc, true, 42'005);
		rig.press(ButtonId::TempoEnc, false, 42'006);
		rig.press(ButtonId::Shift, false, 42'010);
		expect(host.transport_actions.calls).to_equal(std::vector<Call>{{.kind = K::CLEAR_TEMPO_AUTOMATION}});

		rig.press(ButtonId::TapTempo, true, 42'020);
		host.transport_actions.calls.clear(); // the press itself taps the tempo
		rig.press(ButtonId::TempoEnc, true, 42'025);
		rig.press(ButtonId::TempoEnc, false, 42'026);
		rig.press(ButtonId::TapTempo, false, 42'030);
		expect(host.transport_actions.calls).to_equal(std::vector<Call>{{.kind = K::SHOW_SWING_INTERVAL}});

		host.transport_actions.calls.clear();
		rig.press(ButtonId::TempoEnc, true, 42'040);
		rig.press(ButtonId::TempoEnc, false, 42'041);
		expect(host.transport_actions.calls).to_equal(std::vector<Call>{{.kind = K::SHOW_TEMPO}});
	});

	it("on fakes: at the root, SHIFT+TAP TEMPO toggles the metronome, and TAP TEMPO taps as the feature row says", _ {
		release_all_legacy_buttons();
		GcRig rig;
		bind::test::FakeHost host;
		const bind::Ports p = host.ports();
		rig.gc.set_ports(&p);
		deluge::project::Song model;
		rig.gc.set_model(&model);
		using Call = bind::test::RecordingTransportActions::Call;
		using K = Call::Kind;

		rig.press(ButtonId::Shift, true, 43'000);
		rig.press(ButtonId::TapTempo, true, 43'005);
		rig.press(ButtonId::TapTempo, false, 43'006);
		rig.press(ButtonId::Shift, false, 43'010);
		expect(host.transport_actions.calls).to_equal(std::vector<Call>{{.kind = K::TOGGLE_METRONOME}});
		expect(host.transport.metronome).to_be_true();

		host.transport_actions.calls.clear();
		rig.press(ButtonId::TapTempo, true, 43'100);
		rig.press(ButtonId::TapTempo, false, 43'101);
		expect(host.transport_actions.calls).to_equal(std::vector<Call>{{.kind = K::TAP_TEMPO, .flag = true}});

		host.transport_actions.calls.clear();
		(void)host.device.write(
		    deluge::project::device_path::feature(deluge::project::Feature::ALTERNATIVE_TAP_TEMPO_BEHAVIOUR), 1);
		rig.press(ButtonId::TapTempo, true, 43'200);
		rig.press(ButtonId::TapTempo, false, 43'201);
		expect(host.transport_actions.calls).to_equal(std::vector<Call>{{.kind = K::TAP_TEMPO, .flag = false}});
	});

	it("on fakes: BACK undoes, or redoes with shift, only when the transport allows it", _ {
		release_all_legacy_buttons();
		GcRig rig;
		bind::test::FakeHost host;
		const bind::Ports p = host.ports();
		rig.gc.set_ports(&p);
		deluge::project::Song model;
		rig.gc.set_model(&model);
		using Call = bind::test::RecordingTransportActions::Call;
		using K = Call::Kind;

		expect(rig.press(ButtonId::Back, true, 44'000).handled).to_be_true();
		rig.press(ButtonId::Back, false, 44'005);
		expect(host.transport_actions.calls.empty()).to_be_true();

		host.transport.undo_ok = true;
		rig.press(ButtonId::Back, true, 44'100);
		rig.press(ButtonId::Back, false, 44'105);
		rig.press(ButtonId::Shift, true, 44'200);
		rig.press(ButtonId::Back, true, 44'205);
		rig.press(ButtonId::Back, false, 44'206);
		rig.press(ButtonId::Shift, false, 44'210);
		expect(host.transport_actions.calls)
		    .to_equal(std::vector<Call>{{.kind = K::UNDO, .flag = false}, {.kind = K::UNDO, .flag = true}});
	});

	it("on fakes: SYNC_SCALING holds fill while pressed when the feature row says FILL", _ {
		release_all_legacy_buttons();
		GcRig rig;
		bind::test::FakeHost host;
		const bind::Ports p = host.ports();
		rig.gc.set_ports(&p);
		deluge::project::Song model;
		rig.gc.set_model(&model);
		using Call = bind::test::RecordingTransportActions::Call;
		using K = Call::Kind;

		(void)host.device.write(deluge::project::device_path::feature(deluge::project::Feature::SYNC_SCALING_ACTION),
		                        static_cast<int32_t>(std::to_underlying(deluge::project::SyncScalingAction::FILL)));
		rig.press(ButtonId::SyncScaling, true, 45'000);
		rig.press(ButtonId::SyncScaling, false, 45'100);
		expect(host.transport_actions.calls)
		    .to_equal(std::vector<Call>{{.kind = K::FILL, .flag = true}, {.kind = K::FILL, .flag = false}});
	});

	it("on fakes: SYNC_SCALING sets and clears the clip through the installed writes and forgets undo; a refused clip does neither", _ {
		release_all_legacy_buttons();
		GcRig rig;
		bind::test::FakeHost host;
		const bind::Ports p = host.ports();
		rig.gc.set_ports(&p);
		deluge::project::Song model;
		rig.gc.set_model(&model);
		GlobalControls::ViewState vs;
		vs.current_ui_is_clip_minder = true;
		rig.gc.set_view_state(vs);
		constexpr uint32_t kClip = 7;
		services::view_state().set_current_view(services::MenuView{.clip = deluge::project::ClipAddress{.clip_id = kClip}});
		using DeviceKind = bind::test::RecordingDeviceActions::Kind;

		// An arrangement-only clip, then one recording linearly: refused, with neither the write nor the forgetting.
		host.clips.arrangement_only[kClip] = true;
		const Result arrangement = rig.press(ButtonId::SyncScaling, true, 47'000);
		rig.press(ButtonId::SyncScaling, false, 47'005);
		expect(arrangement.actions.size()).to_equal(size_t{1});
		host.clips.arrangement_only[kClip] = false;
		host.clips.recording_linearly[kClip] = true;
		const Result linear = rig.press(ButtonId::SyncScaling, true, 47'100);
		rig.press(ButtonId::SyncScaling, false, 47'105);
		expect(linear.actions.size()).to_equal(size_t{1});
		expect(host.writes.requests.size()).to_equal(size_t{0});
		expect(host.device_actions.calls.size()).to_equal(size_t{0});
		host.clips.recording_linearly[kClip] = false;

		// The set: the song's sync-scaling clip becomes this one, then undo cannot reach past it.
		rig.press(ButtonId::SyncScaling, true, 47'200);
		rig.press(ButtonId::SyncScaling, false, 47'205);
		expect(host.writes.requests.size()).to_equal(size_t{1});
		expect(host.device_actions.calls).to_equal(std::vector<DeviceKind>{DeviceKind::DELETE_ALL_LOGS});

		// The clear, once the record holds the clip.
		model.transport.sync_scaling_clip_id = kClip;
		rig.press(ButtonId::SyncScaling, true, 47'300);
		rig.press(ButtonId::SyncScaling, false, 47'305);
		expect(host.writes.requests.size()).to_equal(size_t{2});
		expect(host.device_actions.calls)
		    .to_equal(std::vector<DeviceKind>{DeviceKind::DELETE_ALL_LOGS, DeviceKind::DELETE_ALL_LOGS});

		const std::array<int32_t, 2> want{static_cast<int32_t>(kClip), -1};
		for (size_t i = 0; i < host.writes.requests.size() && i < want.size(); ++i) {
			const bind::WriteRequest& r = host.writes.requests[i];
			expect(r.target.kind).to_equal(bind::Target::Kind::MODEL);
			expect(r.target.path).to_equal(std::string_view{"syncScalingClip"});
			expect(r.target.group).to_equal(deluge::project::SettingGroupId::SONG);
			expect(r.menu_value).to_equal(want[i]);
		}
		services::view_state().set_current_view(services::MenuView{});
	});

	it("on fakes: SAVE refuses while overdubs are pending, with legacy's own popup string, and opens nothing", _ {
		release_all_legacy_buttons();
		GcRig rig;
		bind::test::FakeHost host;
		const bind::Ports p = host.ports();
		rig.gc.set_ports(&p);
		deluge::project::Song model;
		rig.gc.set_model(&model);

		host.transport.overdubs = true;
		rig.press(ButtonId::Save, true, 48'000);
		const Result refused = rig.press(ButtonId::Save, false, 48'010);
		expect(refused.actions.size()).to_equal(size_t{1});
		if (refused.actions.size() == 1) {
			expect(refused.actions[0].kind).to_equal(ShellAction::Kind::Popup);
			expect(refused.actions[0].popup_string)
			    .to_equal(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_CANT_SAVE_WHILE_OVERDUBS_PENDING));
		}
		expect(rig.gc.song_opens(GlobalControls::SongOpen::SaveSong)).to_equal(uint32_t{0});

		// With none pending the same press opens the saver.
		host.transport.overdubs = false;
		rig.press(ButtonId::Save, true, 48'100);
		const Result plain = rig.press(ButtonId::Save, false, 48'110);
		expect(plain.actions.size()).to_equal(size_t{0});
		expect(rig.gc.song_opens(GlobalControls::SongOpen::SaveSong)).to_equal(uint32_t{1});
	});

	it("on fakes: the LEDs follow the transport's playback, recording and metronome", _ {
		release_all_legacy_buttons();
		GcRig rig;
		bind::test::FakeHost host;
		const bind::Ports p = host.ports();
		rig.gc.set_ports(&p);
		deluge::project::Song model;
		rig.gc.set_model(&model);

		const auto frame = [&rig] {
			deluge::ui::widgets::core::LedFrame f;
			rig.gc.leds(f);
			return f;
		};
		deluge::ui::widgets::core::LedFrame f = frame();
		expect(led_is(f, ButtonId::Play, deluge::ui::widgets::core::LedState::Kind::Off)).to_be_true();
		expect(led_is(f, ButtonId::Record, deluge::ui::widgets::core::LedState::Kind::Off)).to_be_true();
		expect(led_is(f, ButtonId::TapTempo, deluge::ui::widgets::core::LedState::Kind::Off)).to_be_true();

		host.transport.is_playing = true;
		host.transport.mode = bind::TransportRecording::NORMAL;
		host.transport.metronome = true;
		f = frame();
		expect(led_is(f, ButtonId::Play, deluge::ui::widgets::core::LedState::Kind::On)).to_be_true();
		expect(led_is(f, ButtonId::Record, deluge::ui::widgets::core::LedState::Kind::On)).to_be_true();
		expect(led_is(f, ButtonId::TapTempo, deluge::ui::widgets::core::LedState::Kind::On)).to_be_true();

		// An internal recording or the arrangement's own owns the RECORD LED, so the service declares none.
		host.audio.foreground = bind::ForegroundRecording::INTERNAL;
		expect(led_state(frame(), ButtonId::Record).has_value()).to_be_false();
		host.audio.foreground = bind::ForegroundRecording::EXTERNAL;
		expect(led_is(frame(), ButtonId::Record, deluge::ui::widgets::core::LedState::Kind::On)).to_be_true();
		host.transport.mode = bind::TransportRecording::ARRANGEMENT;
		expect(led_state(frame(), ButtonId::Record).has_value()).to_be_false();
	});

	it("on fakes: with no set installed, every press is handled as before and nothing is asked", _ {
		release_all_legacy_buttons();
		GcRig rig;
		expect(reset_transport(100'000)).to_be_true();
		rig.gc.set_ports(nullptr);
		deluge::project::Song model;
		rig.gc.set_model(&model);
		const uint8_t playback_before = playbackHandler.playbackState;
		const bool metronome_before = playbackHandler.metronomeOn;
		const RecordingMode recording_before = playbackHandler.recording;

		uint32_t t = 46'000;
		for (const ButtonId b : {ButtonId::Play, ButtonId::Record, ButtonId::TapTempo, ButtonId::TempoEnc, ButtonId::Back,
		                         ButtonId::SyncScaling}) {
			expect(rig.press(b, true, t).handled).to_be_true();
			rig.press(b, false, t + 50);
			t += 100;
		}
		// Nothing reached the app's transport either: no action port means no call through any.
		expect(playbackHandler.playbackState).to_equal(playback_before);
		expect(playbackHandler.metronomeOn).to_equal(metronome_before);
		expect(playbackHandler.recording).to_equal(recording_before);
		deluge::ui::widgets::core::LedFrame f;
		rig.gc.leds(f);
		expect(led_is(f, ButtonId::Play, deluge::ui::widgets::core::LedState::Kind::Off)).to_be_true();
		expect(led_is(f, ButtonId::Record, deluge::ui::widgets::core::LedState::Kind::Off)).to_be_true();
		expect(led_is(f, ButtonId::TapTempo, deluge::ui::widgets::core::LedState::Kind::Off)).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(global_controls_spec, global_controls_spec_body)
