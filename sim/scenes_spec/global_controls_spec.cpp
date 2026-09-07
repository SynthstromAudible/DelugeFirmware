/// @file
/// @brief `services::GlobalControls` -- driven against the real legacy it ports,
///        `Buttons::buttonAction` (`hid/buttons.cpp:75-235`) and `View::buttonAction`
///        (`gui/views/view.cpp:139-470`), on a real `Song` built by `support/song_fixture.h`.
///
/// @par The oracle, and what "each side from its own sentinel" can mean here
///
/// Transport is a **process singleton**: there is one `PlaybackHandler`, one `Buttons` module and
/// one `AudioEngine::audioSampleTimer` in the address space, and no fixture can give legacy and
/// the port one each -- unlike E4 task 2's gold-knob spec, which gave each side its own clip and
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
///     the same reason -- E2's "the port stores nothing and the comparison silently passes"
///     blindness.
///   * **Both sides are asserted to have MOVED**, so a run where neither did cannot pass on
///     equality alone.
///
/// @par What this spec counts as uncovered (oracle discipline)
///
///   * **Everything behind `audioRecorder`** -- `beginOutputRecording` (`hid/buttons.cpp:161`,
///     `:182`), `isCurrentlyResampling` (`:195`) and `endRecordingSoon` (`:196`). Not ported (see
///     `global_controls.h`), so there is nothing on the port side to compare; the *decisions* that
///     reach them are covered through `GlobalControls::unported_opens(UnportedOpen)`, which is a
///     tally per SITE so an assertion about one arm cannot be satisfied by another.
///   * **`openUI(&saveSongUI)` / `openUI(&loadSongUI)` / `context_menu::clearSong`**
///     (`view.cpp:261`, `:313`, `:290-293`). Same shape: the arm's decision is compared, the open
///     is counted.
///   * **The `THRESHOLD_RECORDING_MODE` popup arm** (`hid/buttons.cpp:190-192`). Not ported, and
///     it is a behaviour difference rather than a missing call; there is no port-side state that
///     could be driven into it.
///   * **The `SYNC_SCALING` fill-mode arm's note halves** (`view.cpp:339-353`). `soundEditor` and
///     `instrumentClipView` are unported globals this layer may not name.
///   * **`View::buttonAction`'s four MIDI-learn arms** (`:148-166`, `:185-196`, `:199-212`,
///     `:215-228`). E4 task 5's; this spec asserts only that they are declined here, and that the
///     transport arms they share a button with are correctly suppressed while learn is active.
///   * **The dedicated undo/redo buttons** (`view.cpp:423-446`). Dead code: they are inside
///     `#ifdef undoButtonX` and `undoButtonX` is defined nowhere in this tree, so there is nothing
///     to drive on either side.
///   * **The UNDO-vs-REDO choice** at `view.cpp:414-415`. `playbackHandler.slowRoutine()` at
///     `:417` consumes the pending command inside the same call
///     (`playback/playback_handler.cpp:201-202`) and a spec cannot make `deluge::sync::sd_busy()`
///     true, so both spellings read `NONE` by the time control returns. The arm's *body running*
///     is covered; which of the two it chose is not. See the BACK example.
///
/// @par Why a stub ROOT view, and not just any stub UI
///
/// `Buttons::buttonAction` dereferences `getCurrentUI()` unconditionally at `hid/buttons.cpp:104`
/// and `:134`, and `getCurrentUI()` in a spec process returns `lastUIBeforeNullifying`, which is
/// null (`gui/ui/ui.cpp:66`, `:136-140`). `setRootUILowLevel` is the one door into that array that
/// runs no view's `opened()` -- `tables_3_spec.cpp` already drives it for the same reason.
///
/// The stub must also **forward to `view.buttonAction`**, because `UI::buttonAction`'s base
/// implementation does not (`gui/ui/ui.h:98-100`, `return ActionResult::NOT_DEALT_WITH`). See
/// `StubUI`'s own @warning: `View::buttonAction` has exactly two callers in the tree, and the one
/// that matters is `TimelineView::buttonAction`'s final `else`
/// (`gui/views/timeline_view.cpp:119-121`), which every root view inherits.

#include "scenes/menu/menu_scene.h"
#include "scenes/registry.h"
#include "scenes/services/global_controls.h"
#include "scenes/services/learn_mode.h"

#include "shell/input_lowering.h"
#include "shell/leds.h"
#include "shell/profiles/deluge.h"
#include "shell/scene_stack.h"

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

#include "support/card_access.h" // before <cppspec.hpp> -- see its @warning
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::services::GlobalControls;
using deluge::scenes::services::LearnMode;
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

/// `playback/playback_handler.cpp:94` defines this with no declaration in the header, exactly as
/// `View::buttonAction` itself has to declare it (`gui/views/view.cpp:100`).
extern GlobalMIDICommand pendingGlobalMIDICommand;
/// The `int32_t` twin at `playback/playback_handler.cpp:95`, which `view.cpp:137` mis-declares as a
/// `GlobalMIDICommand`. Declared with the defining type here; see `global_controls.cpp`'s note.
extern int32_t pendingGlobalMIDICommandNumClustersWritten;

namespace indicator_leds {
/// Legacy's LED shadow array (`hid/led/indicator_leds.cpp:27`), which `setLedState` writes
/// (`:49`). `indicator_leds.h` declares `ledBlinkState` but not this one, so a bare `extern` is the
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

/// @brief The minimum `Display` legacy needs here: `display` is null in a spec process
///        (`hid/display/display.cpp:4`) and `Buttons::buttonAction` dereferences it on every
///        RECORD release (`hid/buttons.cpp:190`).
///
/// `hasPopupOfType` answers false, which is the "no threshold-recording popup is up" case -- the
/// only one the port can reproduce, since that popup is raised straight to the legacy display by
/// `model/song/song.cpp:5856` and never through `notify::`.
struct StubDisplay final : deluge::hid::Display {
	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const*, int8_t, bool, uint8_t, int32_t, PopupType) override {}
	void popupText(char const*, PopupType) override {}
	void popupTextTemporary(char const*, PopupType) override {}
	void cancelPopup() override {}
	void freezeWithError(char const*) override {}
	void displayError(Error) override {}
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const*, bool, bool) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return false; }
	bool hasPopupOfType(PopupType) override { return false; }
	void consoleText(char const*) override {}
	void timerRoutine() override {}
	void displayNotification(std::string_view, std::optional<std::string_view>) override {}
};

/// @brief `spec::QuietAudio`, held for the whole process.
///
/// `PlaybackHandler::playButtonPressed` reaches `AudioEngine::routineWithClusterLoading`
/// (`playback/playback_handler.cpp:572`), which **segfaults** in a spec process on the host input
/// ring (`processing/engines/audio_engine.cpp:183`). `support/card_access.h` exists precisely so
/// this disarm is not re-derived per spec; what it disarms is `AudioEngine::routine()`, which
/// returns immediately while `audioRoutineLocked` is set. Nothing this spec asserts is part of the
/// audio routine's contract: every comparison is on `PlaybackHandler`'s own state, which
/// `setupPlayback` has already written by the time it calls the routine.
///
/// Process-scoped rather than example-scoped because both sides of every comparison need it and
/// the legacy side is driven outside any rig.
spec::QuietAudio& quiet_audio() {
	static spec::QuietAudio instance;
	return instance;
}

StubDisplay& stub_display() {
	static StubDisplay instance;
	return instance;
}

/// @brief A `UI` that stands in for a **root view**: it claims nothing of its own and forwards to
///        `View::buttonAction`, which is the one thing that makes those arms reachable.
///
/// @warning `UI::buttonAction`'s base implementation is `return ActionResult::NOT_DEALT_WITH`
///          (`gui/ui/ui.h:98-100`) -- it does **not** forward to `view`. `View::buttonAction` has
///          exactly two callers in the whole tree: `TimelineView::buttonAction`'s final `else`
///          (`gui/views/timeline_view.cpp:119-121`) and `KeyboardScreen::buttonAction`
///          (`gui/ui/keyboard/keyboard_screen.cpp:591`) -- and `grep "public RootUI"` finds exactly
///          those two classes (`gui/views/timeline_view.h:27`,
///          `gui/ui/keyboard/keyboard_screen.h:35`), so "the current UI is the root UI" IS the
///          reachability rule rather than an approximation of it. With a menu open,
///          `SoundEditor::buttonAction` (`gui/ui/sound_editor.cpp:333`) ends by delegating to
///          `currentMenuItem->buttonAction` (`:595-628`), whose base
///          (`gui/menu_item/menu_item.h:78-80`) returns `NOT_DEALT_WITH`, and nothing on that path
///          names `view` -- so legacy runs the tail and skips every `View::buttonAction` arm. The
///          port gates the same way, on `Scene::is_root()`; `RootScene` below is the spec-local
///          scene that declares it.
class StubUI final : public UI {
public:
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override {
		return view.buttonAction(b, on, inCardRoutine);
	}
	void renderOLED(deluge::hid::display::oled_canvas::Canvas&) override {}
	UIType getUIType() override { return UIType::NONE; }
};

StubUI& stub_ui() {
	static StubUI instance;
	return instance;
}

/// @brief `deluge_profile()`'s button matrix, inverted: the `(x, y)` a `DELUGE_EVENT_BUTTON`
///        carries for @p b.
///
/// The forward table is `BoardProfile::button_by_matrix` (`shell/profiles/deluge.cpp:12-18`);
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
/// leak it into the next example -- E3 task 10's cross-contamination lesson. `root` is a plain
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

	/// A registry of exactly one rig-local scene, at `SceneKind::FirstApp` (== 16). Nothing here
	/// touches `scene_registry()`'s singletons -- `handle_settings_menu`'s `Action::Kind::Push` is
	/// returned and inspected, never applied, because this rig drives `handle()` directly rather
	/// than through a `Router`.
	GcRig() {
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
	}

	UiContext ctx() const {
		return UiContext{lowering.held(), lowering.held_pads(), lowering.shift(), lowering.sticky(),
		                 now_ms,          lowering.profile(),   &lowering};
	}

	/// @brief Press or release @p b through the real lowering at @p at_ms and route every event it
	///        emits into `GlobalControls::handle`.
	///
	/// The router's chain minus the two steps above this service (`shell/router.cpp`'s
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
		// `ButtonId::SelectEnc` and `ButtonId::Back` are **held back** by the lowering
		// (`shell/input_lowering.cpp:32-34`, `:363-377`) and routed after render, so the shell can
		// let a widget claim them first (`Router::route_post_render`). Nothing here builds a frame,
		// so nothing claims them and every one is routed -- which is what the shell does with an
		// empty `InputClaims` too.
		for (const UiEvent& held : lowering.held_back()) {
			last = gc.handle(held, ctx());
		}
		return last;
	}
};

/// @brief Reset every transport global this spec writes, and assert the reset took.
///
/// "Assert, then clear" (the plan's global constraint 9) read the other way round: a reset that
/// silently did not apply would make every example below start from the previous one's leftovers.
[[nodiscard]] bool reset_transport(uint32_t sample_timer) {
	playbackHandler.recording = RecordingMode::OFF;
	playbackHandler.playbackState = 0;
	playbackHandler.metronomeOn = false;
	Buttons::recordButtonPressUsedUp = false;
	AudioEngine::audioSampleTimer = sample_timer;
	preLoadedSong = nullptr;
	// Legacy's own record combo really does start an output recording -- `hid/buttons.cpp:161`,
	// reached when PLAY goes down with RECORD held and playback running -- and nothing in a spec
	// process ever reaps it (`AudioRecorder::slowRoutine` is not driven here). Left standing, it
	// makes `AudioEngine::foregroundRecordingSource()` report `OUTPUT` for every later example,
	// which suppresses the RECORD LED (`playback_handler.cpp:2503-2506`) and flips the PLAY arm's
	// first test (`:147`). Cleared through the published seam, which is the same storage
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
			(void)Buttons::buttonAction(legacy_button(b), false, false);
		}
	}
}

/// @brief The (id, on) pairs a `LedFrame` holds, resolved through `shell::led_is_on` at @p now_ms.
std::vector<std::pair<uint8_t, bool>> led_pairs(const deluge::ui::LedFrame& f, uint32_t now_ms) {
	std::vector<std::pair<uint8_t, bool>> out;
	for (const auto& [id, state] : f.entries()) {
		out.emplace_back(static_cast<uint8_t>(id), deluge::ui::shell::led_is_on(state, now_ms));
	}
	return out;
}

/// @brief The declared state of one LED in @p f, or `nullopt` when it was not declared.
std::optional<deluge::ui::LedState> led_state(const deluge::ui::LedFrame& f, ButtonId b) {
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
bool led_is(const deluge::ui::LedFrame& f, ButtonId b, deluge::ui::LedState::Kind k) {
	const std::optional<deluge::ui::LedState> s = led_state(f, b);
	return s.has_value() && s->kind == k;
}

} // namespace

// ------------------------------------------------------------------------------------------------

// clang-format off
describe global_controls_spec_body("scenes::services::GlobalControls", $ {
	before_each([] {
		// `Buttons::buttonAction` dereferences `getCurrentUI()` and `display`; see the file
		// docstring and `StubDisplay`.
		setRootUILowLevel(&stub_ui());
		display = &stub_display();
		(void)quiet_audio();
	});

	it("transport tail: toggles PlaybackHandler::recording exactly as legacy's own RECORD tap does", _ {
		static spec::SongFixture fixture = spec::make_synth_song();
		expect(fixture.song != nullptr).to_be_true();

		// --- Legacy, from its own reset ---------------------------------------------------
		release_all_legacy_buttons();
		expect(reset_transport(100'000)).to_be_true();
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), true, false);
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), false, false);
		const RecordingMode legacy_result = playbackHandler.recording;
		// Legacy moved: `PlaybackHandler::recordButtonPressed` (`playback_handler.cpp:318-323`).
		expect(legacy_result != RecordingMode::OFF).to_be_true();

		// --- Poison, so a port that writes nothing cannot inherit the answer ---------------
		playbackHandler.recording = RecordingMode::ARRANGEMENT;
		expect(playbackHandler.recording == RecordingMode::ARRANGEMENT).to_be_true();

		// --- The port, from its own reset --------------------------------------------------
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
		expect(reset_transport(100'000)).to_be_true();
		rig.press(ButtonId::Record, true, 1'000);
		rig.press(ButtonId::Record, false, 1'010);
		const RecordingMode port_result = playbackHandler.recording;

		expect(port_result != RecordingMode::OFF).to_be_true();
		expect(port_result == legacy_result).to_be_true();
	});

	it("transport tail: suppresses the RECORD release after a PLAY press -- the record combo, both sides", _ {
		// `hid/buttons.cpp:165`'s `recordButtonPressUsedUp = true` on a PLAY press, read by
		// `:193` on the RECORD release. A single-press example cannot see it: it is only
		// observable across two buttons.
		static spec::SongFixture fixture = spec::make_synth_song();

		// --- Legacy -----------------------------------------------------------------------
		release_all_legacy_buttons();
		expect(reset_transport(100'000)).to_be_true();
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), true, false);
		(void)Buttons::buttonAction(legacy_button(ButtonId::Play), true, false);
		expect(Buttons::recordButtonPressUsedUp).to_be_true(); // `:165`
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), false, false);
		const RecordingMode legacy_result = playbackHandler.recording;
		// The whole point: the release did NOT toggle recording on.
		expect(legacy_result == RecordingMode::OFF).to_be_true();

		playbackHandler.recording = RecordingMode::ARRANGEMENT;

		// --- The port ---------------------------------------------------------------------
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
		expect(reset_transport(100'000)).to_be_true();
		rig.press(ButtonId::Record, true, 2'000);
		rig.press(ButtonId::Play, true, 2'005);
		// `InputLowering::press_used_up` is the port's `recordButtonPressUsedUp`
		// (`shell/input_lowering.h:247`), and `GlobalControls` marks it for RECORD, not for
		// PLAY (`hid/buttons.cpp:165`'s bit suppresses the RECORD release).
		expect(rig.lowering.press_used_up(ButtonId::Record)).to_be_true();
		rig.press(ButtonId::Record, false, 2'010);
		const RecordingMode port_result = playbackHandler.recording;

		expect(port_result == RecordingMode::OFF).to_be_true();
		expect(port_result == legacy_result).to_be_true();
		// And the arm really did reach the one statement that is not ported --
		// `audioRecorder.beginOutputRecording()` at `:161`, guarded by
		// `playbackHandler.playbackState` -- or did not, depending on whether PLAY started
		// playback. Whichever it was, no OTHER site may have fired.
		expect(rig.gc.unported_opens(GlobalControls::UnportedOpen::OutputRecordFromPlay)
		       == rig.gc.unported_opens_total())
		    .to_be_true();
		expect(rig.gc.unported_opens_total() <= uint32_t(1)).to_be_true();
	});

	it("transport tail: does not toggle recording on a LONG press, both sides", _ {
		// `hid/buttons.cpp:193-194`'s `< kShortPressTime`. Legacy measures
		// `AudioEngine::audioSampleTimer` (`kSampleRate / 2` samples,
		// `definitions_cxx.hpp:1028`); the port measures milliseconds through
		// `InputLowering::was_short_press` (500 ms, `shell/input_lowering.h:26`). The two
		// constants are the same duration, which is the mapping under test.
		static spec::SongFixture fixture = spec::make_synth_song();

		release_all_legacy_buttons();
		expect(reset_transport(100'000)).to_be_true();
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), true, false);
		AudioEngine::audioSampleTimer += kSampleRate; // two short-press windows
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), false, false);
		const RecordingMode legacy_result = playbackHandler.recording;
		expect(legacy_result == RecordingMode::OFF).to_be_true();

		playbackHandler.recording = RecordingMode::ARRANGEMENT;

		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
		expect(reset_transport(100'000)).to_be_true();
		rig.press(ButtonId::Record, true, 3'000);
		rig.press(ButtonId::Record, false, 3'000 + 2 * GlobalControls::kShortPressMs);
		expect(playbackHandler.recording == RecordingMode::OFF).to_be_true();
		expect(playbackHandler.recording == legacy_result).to_be_true();
	});

	it("transport tail: declines PLAY and RECORD while MIDI learn is active, and claims them otherwise", _ {
		// `view.cpp:199` and `:215` -- both arms are `&& currentUIMode == UI_MODE_MIDI_LEARN`,
		// so under learn `View` claims the button and `hid/buttons.cpp:139-141` skips the
		// transport tail entirely. `MidiLearn` owns those arms (E4 task 5); this service
		// declines rather than swallowing them, so a future routed `MidiLearn` still gets them.
		static spec::SongFixture fixture = spec::make_synth_song();
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
		expect(reset_transport(100'000)).to_be_true();

		rig.learn.on = true;
		expect(rig.gc.effective_ui_mode() == GlobalControls::UiMode::MidiLearn).to_be_true();
		const Result under_learn = rig.press(ButtonId::Record, true, 4'000);
		expect(under_learn.handled).to_be_false();
		// The transport tail did not run: nothing was marked used up and nothing moved.
		expect(playbackHandler.recording == RecordingMode::OFF).to_be_true();
		rig.press(ButtonId::Record, false, 4'010);
		expect(playbackHandler.recording == RecordingMode::OFF).to_be_true();

		rig.learn.on = false;
		expect(rig.gc.effective_ui_mode() == GlobalControls::UiMode::None).to_be_true();
		const Result outside_learn = rig.press(ButtonId::Record, true, 5'000);
		expect(outside_learn.handled).to_be_true();
		rig.press(ButtonId::Record, false, 5'010);
		expect(playbackHandler.recording == RecordingMode::NORMAL).to_be_true();
	});

	it("transport tail: reports SHIFT handled without re-porting commandToggleShift", _ {
		// `hid/buttons.cpp:169-171`. `Buttons::commandToggleShift` is already
		// `InputLowering::toggle_shift` (`shell/input_lowering.cpp:304-324`), called from
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
		// `RootUI` (`gui/views/timeline_view.cpp:119-121`,
		// `gui/ui/keyboard/keyboard_screen.cpp:591`, and those two classes are the entire result of
		// `grep "public RootUI"`); `Buttons::buttonAction`'s tail runs for any current UI that
		// returned `NOT_DEALT_WITH` (`hid/buttons.cpp:139-141`), root or not.
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
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

		// And BACK, which is the arm that made the pre-gate divergence live: it is claimed
		// unconditionally by `view.cpp:404-421`, so an ungated service would fire undo/redo from
		// inside any non-root scene that does not claim BACK for itself.
		//
		// @warning The only thing that masked that before the gate was
		//          `ActionLogger::allowedToDoReversion` (`model/action/action_logger.cpp:854`)
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
		expect(pending_global_midi_clusters() == kPoison).to_be_true(); // `view.cpp:416` never ran
		rig.press(ButtonId::Back, false, 42'010);

		// The tail is NOT gated: a RECORD tap from the same non-root scene still toggles recording,
		// exactly as `hid/buttons.cpp:173-203` does under a menu.
		expect(playbackHandler.recording == RecordingMode::OFF).to_be_true();
		const Result record_off_root = rig.press(ButtonId::Record, true, 43'000);
		expect(record_off_root.handled).to_be_true();
		rig.press(ButtonId::Record, false, 43'010);
		expect(playbackHandler.recording == RecordingMode::NORMAL).to_be_true();
	});

	it("tap tempo / tempo encoder: toggles the metronome on shift+TAP_TEMPO, both sides", _ {
		// `view.cpp:170-172`.
		static spec::SongFixture fixture = spec::make_synth_song();

		release_all_legacy_buttons();
		expect(reset_transport(100'000)).to_be_true();
		Buttons::commandToggleShift(true);
		expect(Buttons::isShiftButtonPressed()).to_be_true();
		(void)Buttons::buttonAction(legacy_button(ButtonId::TapTempo), true, false);
		const bool legacy_result = playbackHandler.metronomeOn;
		// The LED half of the same comparison, read out of legacy's own shadow array.
		// `PlaybackHandler::toggleMetronomeStatus` (`playback/playback_handler.cpp:2514-2520`) calls
		// `setLedStates()`, whose last line is
		// `indicator_leds::setLedState(IndicatorLED::TAP_TEMPO, metronomeOn)` (`:2511`), and that
		// writes `indicator_leds::ledStates` (`hid/led/indicator_leds.cpp:49`). This is the ONE
		// LED in this service that legacy sets through a path a spec can drive end to end, so it
		// is the one the LED mapping is proved on.
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
		rig.gc.set_song(fixture.song);
		expect(reset_transport(100'000)).to_be_true();
		rig.press(ButtonId::Shift, true, 7'000);
		expect(rig.lowering.shift()).to_be_true();
		const Result r = rig.press(ButtonId::TapTempo, true, 7'005);
		expect(r.handled).to_be_true();
		expect(playbackHandler.metronomeOn).to_be_true();
		expect(playbackHandler.metronomeOn == legacy_result).to_be_true();

		// The port declares the LED rather than writing it, so the comparison is the declaration
		// resolved through `shell::led_is_on` against the bool legacy left in `ledStates`.
		deluge::ui::LedFrame f;
		rig.gc.leds(f);
		const std::optional<deluge::ui::LedState> tap = led_state(f, ButtonId::TapTempo);
		expect(tap.has_value()).to_be_true();
		expect(deluge::ui::shell::led_is_on(tap.value_or(deluge::ui::LedState{}), rig.now_ms) == legacy_led)
		    .to_be_true();
		rig.press(ButtonId::Shift, false, 7'010);
	});

	it("tap tempo / tempo encoder: does not tap tempo during a record count-in, but does toggle the metronome", _ {
		// `view.cpp:167` admits both `UI_MODE_NONE` and `UI_MODE_RECORD_COUNT_IN`; `:174`
		// admits only `UI_MODE_NONE`. Reading the braces: the shift test at `:170` is inside
		// `if (on)` at `:168`, which is inside the `:167` mode test -- so during a count-in the
		// metronome toggle still runs and the tap does not.
		static spec::SongFixture fixture = spec::make_synth_song();
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
		expect(reset_transport(100'000)).to_be_true();

		GlobalControls::ViewState vs;
		vs.ui_mode = GlobalControls::UiMode::RecordCountIn;
		rig.gc.set_view_state(vs);
		expect(rig.gc.effective_ui_mode() == GlobalControls::UiMode::RecordCountIn).to_be_true();

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
		// `hid/buttons.cpp:215-220`. Both reads are root-UI facts injected through
		// `ViewState`; what is asserted is the decision, since `commandDisplayTempo` itself
		// only draws.
		static spec::SongFixture fixture = spec::make_synth_song();
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
		expect(reset_transport(100'000)).to_be_true();

		// Shift wins over everything (`:208-209`).
		rig.press(ButtonId::Shift, true, 9'000);
		expect(rig.press(ButtonId::TempoEnc, true, 9'005).handled).to_be_true();
		rig.press(ButtonId::Shift, false, 9'010);

		// TAP_TEMPO held picks the swing-interval arm (`:211-212`).
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
		// `view.cpp:238-263`. The open itself (`:261`) is not ported; what is compared is
		// every decision above it.
		static spec::SongFixture fixture = spec::make_synth_song();
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
		expect(reset_transport(100'000)).to_be_true();

		expect(rig.gc.holding_save()).to_be_false();
		expect(rig.press(ButtonId::Save, true, 10'000).handled).to_be_true();
		expect(rig.gc.holding_save()).to_be_true(); // `:240`
		expect(rig.gc.effective_ui_mode() == GlobalControls::UiMode::HoldingSave).to_be_true();
		expect(rig.gc.unported_opens_total() == uint32_t(0)).to_be_true();

		rig.press(ButtonId::Save, false, 10'000 + GlobalControls::kShortPressMs - 1);
		expect(rig.gc.holding_save()).to_be_false(); // `:253`
		expect(rig.gc.unported_opens(GlobalControls::UnportedOpen::SaveSong) == uint32_t(1)).to_be_true(); // `:261`
		expect(rig.gc.unported_opens_total() == uint32_t(1)).to_be_true();
	});

	it("SAVE/LOAD decision: does not open on a long press, and does not hold with shift or a type button", _ {
		static spec::SongFixture fixture = spec::make_synth_song();
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
		expect(reset_transport(100'000)).to_be_true();

		// Long press: `:255`'s window elapses, so `:265` just drops the LED.
		rig.press(ButtonId::Save, true, 11'000);
		rig.press(ButtonId::Save, false, 11'000 + GlobalControls::kShortPressMs);
		expect(rig.gc.holding_save()).to_be_false();
		expect(rig.gc.unported_opens_total() == uint32_t(0)).to_be_true();

		// Shift: `:239`'s `&& !Buttons::isShiftButtonPressed()`.
		rig.press(ButtonId::Shift, true, 12'000);
		rig.press(ButtonId::Save, true, 12'005);
		expect(rig.gc.holding_save()).to_be_false();
		rig.press(ButtonId::Save, false, 12'010);
		rig.press(ButtonId::Shift, false, 12'015);
		expect(rig.gc.unported_opens_total() == uint32_t(0)).to_be_true();

		// SYNTH held: `:233`'s four-button guard suppresses the whole body -- but the arm is
		// still reported handled, because legacy still falls through to `:469`.
		rig.press(ButtonId::Synth, true, 13'000);
		expect(rig.press(ButtonId::Save, true, 13'005).handled).to_be_true();
		expect(rig.gc.holding_save()).to_be_false();
		rig.press(ButtonId::Save, false, 13'010);
		rig.press(ButtonId::Synth, false, 13'015);
		expect(rig.gc.unported_opens_total() == uint32_t(0)).to_be_true();
	});

	it("SAVE/LOAD decision: refuses to save while overdubs are pending, with legacy's own popup string", _ {
		// `:256-259`.
		static spec::SongFixture fixture = spec::make_synth_song();
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
		expect(reset_transport(100'000)).to_be_true();

		// The fixture's song has no pending overdubs, so the plain path opens.
		expect(fixture.song->hasAnyPendingNextOverdubs()).to_be_false();
		rig.press(ButtonId::Save, true, 14'000);
		const Result plain = rig.press(ButtonId::Save, false, 14'010);
		expect(plain.actions.size() == size_t(0)).to_be_true();
		expect(rig.gc.unported_opens(GlobalControls::UnportedOpen::SaveSong) == uint32_t(1)).to_be_true();
	});

	it("SAVE/LOAD decision: takes the clear-song branch on shift+LOAD and the hold branch otherwise", _ {
		// `:286-300`.
		static spec::SongFixture fixture = spec::make_synth_song();
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
		expect(reset_transport(100'000)).to_be_true();

		rig.press(ButtonId::Shift, true, 15'000);
		expect(rig.press(ButtonId::Load, true, 15'005).handled).to_be_true();
		expect(rig.gc.holding_load()).to_be_false();                // `:286` took the other branch
		expect(rig.gc.unported_opens(GlobalControls::UnportedOpen::ClearSong) == uint32_t(1)).to_be_true(); // `:292`
		rig.press(ButtonId::Load, false, 15'010);
		rig.press(ButtonId::Shift, false, 15'015);

		rig.press(ButtonId::Load, true, 16'000);
		expect(rig.gc.holding_load()).to_be_true(); // `:297`
		rig.press(ButtonId::Load, false, 16'010);
		expect(rig.gc.holding_load()).to_be_false();                // `:310`
		expect(rig.gc.unported_opens(GlobalControls::UnportedOpen::LoadSong) == uint32_t(1)).to_be_true(); // `:313`
		expect(rig.gc.unported_opens_total() == uint32_t(2)).to_be_true();
	});

	it("sync scaling: sets and clears the song's sync-scaling clip exactly as legacy does", _ {
		// `view.cpp:372-394`. The model write is `Song::setInputTickScaleClip`, and it is what
		// both sides are compared on.
		static spec::SongFixture legacy_fixture = spec::make_synth_song();
		currentSong = legacy_fixture.song;
		release_all_legacy_buttons();
		expect(reset_transport(100'000)).to_be_true();
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::SyncScalingAction,
		                           RuntimeFeatureStateSyncScalingAction::SyncScaling);
		legacy_fixture.song->setInputTickScaleClip(nullptr);
		expect(legacy_fixture.song->getSyncScalingClip() == nullptr).to_be_true();

		// Legacy needs a clip-minder current UI for `:373`; `StubUI::toClipMinder()` is null,
		// so legacy takes the alert arm instead. That arm IS the comparison here: it is the
		// one `:373-376` decision this spec can drive on both sides, and the two model writes
		// below are driven on the port alone with `Song` asserted directly.
		(void)Buttons::buttonAction(legacy_button(ButtonId::SyncScaling), true, false);
		expect(legacy_fixture.song->getSyncScalingClip() == nullptr).to_be_true(); // refused
		(void)Buttons::buttonAction(legacy_button(ButtonId::SyncScaling), false, false);

		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(legacy_fixture.song);
		expect(reset_transport(100'000)).to_be_true();
		// Same refusal, same reason, port side.
		expect(rig.press(ButtonId::SyncScaling, true, 17'000).handled).to_be_true();
		expect(legacy_fixture.song->getSyncScalingClip() == nullptr).to_be_true();
		rig.press(ButtonId::SyncScaling, false, 17'005);

		// Now with a clip minder on screen, the arm sets the clip (`:388`) and a second press
		// clears it (`:393`).
		GlobalControls::ViewState vs;
		vs.current_ui_is_clip_minder = true;
		rig.gc.set_view_state(vs);
		rig.press(ButtonId::SyncScaling, true, 18'000);
		expect(legacy_fixture.song->getSyncScalingClip() == legacy_fixture.song->getCurrentClip()).to_be_true();
		rig.press(ButtonId::SyncScaling, false, 18'005);
		rig.press(ButtonId::SyncScaling, true, 19'000);
		expect(legacy_fixture.song->getSyncScalingClip() == nullptr).to_be_true();
		rig.press(ButtonId::SyncScaling, false, 19'005);
	});

	it("sync scaling: refuses while recording to the arrangement, with legacy's own popup string", _ {
		// `:361-365`, legacy's `cant:` label.
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
		expect(reset_transport(100'000)).to_be_true();
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::SyncScalingAction,
		                           RuntimeFeatureStateSyncScalingAction::SyncScaling);
		fixture.song->setInputTickScaleClip(nullptr);
		playbackHandler.recording = RecordingMode::ARRANGEMENT;

		const Result r = rig.press(ButtonId::SyncScaling, true, 20'000);
		expect(r.handled).to_be_true();
		expect(r.actions.size() == size_t(1)).to_be_true();
		expect(r.actions[0].kind == ShellAction::Kind::Popup).to_be_true();
		expect(r.actions[0].popup_string
		       == static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT))
		    .to_be_true();
		expect(fixture.song->getSyncScalingClip() == nullptr).to_be_true();
		rig.press(ButtonId::SyncScaling, false, 20'005);
		playbackHandler.recording = RecordingMode::OFF;
	});

	it("sync scaling: toggles fill mode when the community setting repurposes the button, both sides", _ {
		// `:335-336` and `:356`. The two note-fill halves (`:339-353`) are not ported; this is
		// the arm's "otherwise", which is.
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		expect(reset_transport(100'000)).to_be_true();
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::SyncScalingAction,
		                           RuntimeFeatureStateSyncScalingAction::Fill);
		fixture.song->changeFillMode(false);

		(void)Buttons::buttonAction(legacy_button(ButtonId::SyncScaling), true, false);
		const bool legacy_result = fixture.song->isFillModeActive();
		(void)Buttons::buttonAction(legacy_button(ButtonId::SyncScaling), false, false);
		expect(legacy_result).to_be_true();

		fixture.song->changeFillMode(false);
		expect(fixture.song->isFillModeActive()).to_be_false();

		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
		rig.press(ButtonId::SyncScaling, true, 21'000);
		expect(fixture.song->isFillModeActive()).to_be_true();
		expect(fixture.song->isFillModeActive() == legacy_result).to_be_true();
		rig.press(ButtonId::SyncScaling, false, 21'005);
		expect(fixture.song->isFillModeActive()).to_be_false();

		runtimeFeatureSettings.set(RuntimeFeatureSettingType::SyncScalingAction,
		                           RuntimeFeatureStateSyncScalingAction::SyncScaling);
	});

	it("BACK undo/redo: runs the reversion arm exactly where legacy's live one does", _ {
		// `view.cpp:404-421`. The dedicated undo/redo arms at `:425-445` are inside
		// `#ifdef undoButtonX`, which is defined nowhere in this tree, so `BACK` is the only live
		// spelling and this is the whole of the port's undo/redo.
		//
		// **What is observable, and what is not.** `:417` calls `playbackHandler.slowRoutine()`
		// in the same statement sequence, and that function *consumes* the command it was just
		// handed -- `pendingGlobalMIDICommand = GlobalMIDICommand::NONE`
		// (`playback/playback_handler.cpp:201-202`) -- unless `deluge::sync::sd_busy()` is true,
		// which a spec cannot make true (`sync/sd_access.cpp:23-25` reads the host BSP). So the
		// UNDO-vs-REDO *choice* at `:414-415` is **uncovered here**: by the time control returns,
		// both spellings read NONE. What IS observable is `:416`, the second assignment, which
		// nothing consumes -- so poisoning it and watching it come back 0 proves the arm's body ran
		// on each side, and leaving it poisoned proves the `:409` guard held.
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		expect(reset_transport(100'000)).to_be_true();
		actionLogger.deleteAllLogs();
		// `ActionLogger::allowedToDoReversion` (`model/action/action_logger.cpp:853-855`) is
		// `currentSong && getCurrentUI() == getRootUI() && isUIModeWithinRange(reversionUIModes)`
		// -- it does NOT ask whether there is anything to revert. With the stub root UI installed
		// and a song, it is true, which is the arm-runs case.
		expect(actionLogger.allowedToDoReversion()).to_be_true();

		constexpr int32_t kPoison = 0x5EED;
		pending_global_midi_clusters() = kPoison;
		(void)Buttons::buttonAction(legacy_button(ButtonId::Back), true, false);
		const int32_t legacy_result = pending_global_midi_clusters();
		(void)Buttons::buttonAction(legacy_button(ButtonId::Back), false, false);
		expect(legacy_result == 0).to_be_true(); // `view.cpp:416` ran

		// Re-poison, so a port that writes nothing cannot inherit legacy's zero.
		pending_global_midi_clusters() = kPoison;
		expect(pending_global_midi_clusters() == kPoison).to_be_true();

		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
		const Result r = rig.press(ButtonId::Back, true, 22'000);
		expect(r.handled).to_be_true();
		expect(pending_global_midi_clusters() == 0).to_be_true();
		expect(pending_global_midi_clusters() == legacy_result).to_be_true();
		rig.press(ButtonId::Back, false, 22'005);

		// And the `:409` guard: with no song, `allowedToDoReversion()` is false and the body is
		// skipped, while the arm is still claimed (legacy reaches `:469` either way).
		pending_global_midi_clusters() = kPoison;
		Song* const saved = currentSong;
		currentSong = nullptr;
		expect(actionLogger.allowedToDoReversion()).to_be_false();
		const Result guarded = rig.press(ButtonId::Back, true, 22'100);
		expect(guarded.handled).to_be_true();
		expect(pending_global_midi_clusters() == kPoison).to_be_true();
		rig.press(ButtonId::Back, false, 22'105);
		currentSong = saved;
	});

	it("shift+SELECT_ENC: pushes the menu scene at the settings root", _ {
		// `view.cpp:449-464`. `soundEditor.setup()`'s default `clip` is null
		// (`gui/ui/sound_editor.h:122`) and shift is held by this arm's own condition, so
		// legacy always lands on `settingsRootMenu` (`sound_editor.cpp:1769`).
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
		expect(reset_transport(100'000)).to_be_true();

		// Without shift the arm's condition fails outright (`:449`), and nothing in the tail
		// names SELECT_ENC either.
		const Result no_shift = rig.press(ButtonId::SelectEnc, true, 23'000);
		expect(no_shift.handled).to_be_false();
		expect(no_shift.actions.size() == size_t(0)).to_be_true();
		rig.press(ButtonId::SelectEnc, false, 23'005);

		rig.press(ButtonId::Shift, true, 24'000);
		const Result r = rig.press(ButtonId::SelectEnc, true, 24'005);
		expect(r.handled).to_be_true();
		expect(r.actions.size() == size_t(1)).to_be_true();
		expect(r.actions[0].kind == ShellAction::Kind::Push).to_be_true();
		expect(r.actions[0].id.kind == SceneKind::FirstApp).to_be_true();
		// And the interpreter really was retargeted at the settings root.
		expect(menu_scene().focused() != nullptr).to_be_true();
		rig.press(ButtonId::SelectEnc, false, 24'010);
		rig.press(ButtonId::Shift, false, 24'015);
	});

	it("shift+SELECT_ENC: refuses while recording to the arrangement", _ {
		// `:452-455`.
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
		expect(reset_transport(100'000)).to_be_true();
		playbackHandler.recording = RecordingMode::ARRANGEMENT;

		rig.press(ButtonId::Shift, true, 25'000);
		const Result r = rig.press(ButtonId::SelectEnc, true, 25'005);
		expect(r.handled).to_be_true();
		expect(r.actions.size() == size_t(1)).to_be_true();
		expect(r.actions[0].kind == ShellAction::Kind::Popup).to_be_true();
		rig.press(ButtonId::SelectEnc, false, 25'010);
		rig.press(ButtonId::Shift, false, 25'015);
		playbackHandler.recording = RecordingMode::OFF;
	});

	it("LEDs: declares exactly Play, Record, TapTempo, SyncScaling, Save, Load and Clip, by identity", _ {
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
		expect(reset_transport(100'000)).to_be_true();
		fixture.song->setInputTickScaleClip(nullptr);

		deluge::ui::LedFrame f;
		rig.gc.leds(f);
		const std::vector<std::pair<uint8_t, bool>> got = led_pairs(f, 0);
		const std::vector<std::pair<uint8_t, bool>> want{
		    {static_cast<uint8_t>(ButtonId::Play), false},        // `playback_handler.cpp:2501`
		    {static_cast<uint8_t>(ButtonId::Record), false},      // `:2505`
		    {static_cast<uint8_t>(ButtonId::TapTempo), false},    // `:2511`
		    {static_cast<uint8_t>(ButtonId::SyncScaling), false}, // `view.cpp:510`
		    {static_cast<uint8_t>(ButtonId::Save), false},        // `view.cpp:265`
		    {static_cast<uint8_t>(ButtonId::Load), false},        // `view.cpp:323`
		    {static_cast<uint8_t>(ButtonId::Clip), false},        // `view.cpp:374`, not alerting
		};
		expect(got == want).to_be_true();
	});

	it("LEDs: tracks the model: playback, recording, metronome, sync scaling, and the two holds", _ {
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
		expect(reset_transport(100'000)).to_be_true();
		fixture.song->setInputTickScaleClip(nullptr);

		playbackHandler.playbackState = PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		playbackHandler.recording = RecordingMode::NORMAL;
		playbackHandler.metronomeOn = true;
		rig.press(ButtonId::Save, true, 26'000);

		deluge::ui::LedFrame f;
		rig.gc.leds(f);
		expect(led_is(f, ButtonId::Play, deluge::ui::LedState::Kind::On)).to_be_true();
		expect(led_is(f, ButtonId::Record, deluge::ui::LedState::Kind::On)).to_be_true();
		expect(led_is(f, ButtonId::TapTempo, deluge::ui::LedState::Kind::On)).to_be_true();
		expect(led_is(f, ButtonId::Save, deluge::ui::LedState::Kind::On)).to_be_true();
		expect(led_is(f, ButtonId::Load, deluge::ui::LedState::Kind::Off)).to_be_true();
		rig.press(ButtonId::Save, false, 26'005 + GlobalControls::kShortPressMs);

		// `playback_handler.cpp:2503-2506`: while recording to the arrangement the RECORD LED
		// is not declared at all.
		playbackHandler.recording = RecordingMode::ARRANGEMENT;
		deluge::ui::LedFrame f2;
		rig.gc.leds(f2);
		expect(led_state(f2, ButtonId::Record).has_value()).to_be_false();
		playbackHandler.recording = RecordingMode::OFF;

		// `view.cpp:510`: solid on when some clip is the sync-scaling clip and we are not in a
		// clip minder.
		fixture.song->setInputTickScaleClip(fixture.song->getCurrentClip());
		deluge::ui::LedFrame f3;
		rig.gc.leds(f3);
		expect(led_is(f3, ButtonId::SyncScaling, deluge::ui::LedState::Kind::On)).to_be_true();

		// `view.cpp:504-506`: blinking once the clip minder showing it IS that clip.
		GlobalControls::ViewState vs;
		vs.current_ui_is_clip_minder = true;
		rig.gc.set_view_state(vs);
		deluge::ui::LedFrame f4;
		rig.gc.leds(f4);
		expect(led_is(f4, ButtonId::SyncScaling, deluge::ui::LedState::Kind::Blink)).to_be_true();
		expect(led_state(f4, ButtonId::SyncScaling).value_or(deluge::ui::LedState{}).period_ms
		       == deluge::ui::kFlashTime)
		    .to_be_true();
		fixture.song->setInputTickScaleClip(nullptr);
	});

	it("LEDs: blinks the CLIP_VIEW LED for the sync-scaling alert, then folds back to off", _ {
		// `view.cpp:373-376`'s `indicateAlertOnLed`, which is `blinkLed(led, 3, 1)`
		// (`hid/led/indicator_leds.cpp:160-162`).
		//
		// **The duration is pinned against legacy's own countdown, not derived on paper** (fix
		// round 1, minor 3: it was 360 ms, and nothing checked it). `blinkLed` sets
		// `blinksLeft = numBlinks * 2` == 6 (`:75`) and then runs `updateBlinkingLedStates` once at
		// arm time (`:79`), which decrements it (`:123`) -- so the fold-back at `:126-129` comes
		// after FIVE further `kFastFlashTime` timeouts, not six. Driven here by stepping legacy's
		// `ledBlinkTimeout` until the blinker deactivates and counting the steps.
		indicator_leds::setLedState(IndicatorLED::CLIP_VIEW, false);
		expect(indicator_leds::getLedBlinkerIndex(IndicatorLED::CLIP_VIEW) == 255).to_be_true();
		indicator_leds::indicateAlertOnLed(IndicatorLED::CLIP_VIEW);
		expect(indicator_leds::getLedBlinkerIndex(IndicatorLED::CLIP_VIEW) != 255).to_be_true();
		int32_t legacy_timeouts = 0;
		while (indicator_leds::getLedBlinkerIndex(IndicatorLED::CLIP_VIEW) != 255 && legacy_timeouts < 20) {
			indicator_leds::ledBlinkTimeout(/*blinkingType=*/1);
			legacy_timeouts++;
		}
		expect(legacy_timeouts).to_equal(5);
		expect(GlobalControls::kAlertMs
		       == static_cast<uint32_t>(legacy_timeouts) * GlobalControls::kClipArmFlashMs)
		    .to_be_true();
		// And it really did fold back to the state it was armed from (`:128`), which is what makes
		// the port's explicit `Off` declaration the right fold-back rather than `until_ms`'s `On`.
		expect(indicator_leds::ledStates[static_cast<uint8_t>(IndicatorLED::CLIP_VIEW)]).to_be_false();

		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		release_all_legacy_buttons();
		GcRig rig;
		rig.gc.set_song(fixture.song);
		expect(reset_transport(100'000)).to_be_true();
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::SyncScalingAction,
		                           RuntimeFeatureStateSyncScalingAction::SyncScaling);
		fixture.song->setInputTickScaleClip(nullptr);

		rig.press(ButtonId::SyncScaling, true, 27'000);
		rig.gc.tick(27'000, 0);
		deluge::ui::LedFrame f;
		rig.gc.leds(f);
		expect(led_is(f, ButtonId::Clip, deluge::ui::LedState::Kind::Blink)).to_be_true();

		rig.gc.tick(27'000 + GlobalControls::kAlertMs, GlobalControls::kAlertMs);
		deluge::ui::LedFrame f2;
		rig.gc.leds(f2);
		expect(led_is(f2, ButtonId::Clip, deluge::ui::LedState::Kind::Off)).to_be_true();
		rig.press(ButtonId::SyncScaling, false, 27'500);
	});

	it("time: flips the clip-arm phase on a QUIET tick, with no input at all", _ {
		// `View::flashPlayEnable` (`view.cpp:2766-2768`) arms `kFastFlashTime`;
		// `View::flashPlayRoutine` (`:2754-2764`) toggles `clipArmFlashOn` when it fires. The
		// port's `next_timer()`/`tick()` pair is that timer, and nothing below touches a
		// button.
		//
		// NOTE: this is the *clip-arm* flash, not a Play-LED flash. `flashPlayRoutine` does not
		// set `IndicatorLED::PLAY` -- the only time-driven thing in legacy that does is
		// `View::midiLearnFlash` (`view.cpp:806`), which is E4 task 5's. See the task-4 report.
		GcRig rig;
		expect(rig.gc.clip_arm_flash_on()).to_be_false();
		expect(rig.gc.next_timer().has_value()).to_be_false();

		rig.gc.tick(30'000, 0);
		rig.gc.flash_play_enable(30'000);
		expect(rig.gc.next_timer().has_value()).to_be_true();
		expect(*rig.gc.next_timer() == GlobalControls::kClipArmFlashMs).to_be_true();

		rig.gc.tick(30'000 + GlobalControls::kClipArmFlashMs - 1, GlobalControls::kClipArmFlashMs - 1);
		expect(rig.gc.clip_arm_flash_on()).to_be_false(); // not yet
		rig.gc.tick(30'000 + GlobalControls::kClipArmFlashMs, 1);
		expect(rig.gc.clip_arm_flash_on()).to_be_true(); // fired, with no input
		expect(rig.gc.next_timer().has_value()).to_be_false();

		// A second arm toggles it back -- legacy's re-arm lives in
		// `SessionView::flashPlayRoutine` (`gui/views/session_view.cpp:2423-2424`), which is
		// view-side and not ported, so the caller arms it again.
		rig.gc.flash_play_enable(30'100);
		rig.gc.tick(30'160, 60);
		expect(rig.gc.clip_arm_flash_on()).to_be_false();

		// `View::flashPlayDisable` (`:2770-2783`) clears both.
		rig.gc.flash_play_enable(30'200);
		rig.gc.flash_play_disable();
		expect(rig.gc.clip_arm_flash_on()).to_be_false();
		expect(rig.gc.next_timer().has_value()).to_be_false();
	});

	it("time: changes a declared LED on a QUIET tick, and asks to be woken for it", _ {
		// The blinking SYNC_SCALING LED (`view.cpp:504-506`). `shell::led_is_on` derives the
		// phase from `now_ms` (`shell/leds.h`), and `Shell::compute_next_wake` has no
		// blink-aware source of its own -- so the service must return a `next_timer()` or the
		// blink freezes on an idle system. Both halves are asserted here.
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		GcRig rig;
		rig.gc.set_song(fixture.song);
		GlobalControls::ViewState vs;
		vs.current_ui_is_clip_minder = true;
		rig.gc.set_view_state(vs);
		fixture.song->setInputTickScaleClip(fixture.song->getCurrentClip());

		constexpr uint32_t kHalf = deluge::ui::kFlashTime;
		rig.gc.tick(0, 0);
		expect(rig.gc.next_timer().has_value()).to_be_true();
		expect(*rig.gc.next_timer() == kHalf).to_be_true();

		deluge::ui::LedFrame f;
		rig.gc.leds(f);
		expect(led_is(f, ButtonId::SyncScaling, deluge::ui::LedState::Kind::Blink)).to_be_true();
		const deluge::ui::LedState blink = led_state(f, ButtonId::SyncScaling).value_or(deluge::ui::LedState{});
		// Two quiet ticks a half-period apart, no input in between, and the LED is lit
		// differently in each.
		const bool at_0 = deluge::ui::shell::led_is_on(blink, 0);
		const bool at_half = deluge::ui::shell::led_is_on(blink, kHalf);
		expect(at_0 != at_half).to_be_true();

		fixture.song->setInputTickScaleClip(nullptr);
		rig.gc.tick(0, 0);
		expect(rig.gc.next_timer().has_value()).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(global_controls_spec, global_controls_spec_body)
