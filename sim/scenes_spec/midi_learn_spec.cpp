/// @file
/// @brief `services::MidiLearn` -- driven against the real legacy it ports, `View`'s MIDI-learn
///        half (`gui/views/view.cpp:472-500`, `:518-621`, `:623-743`, `:767-816`) and the four
///        `View::buttonAction` arms that belong to it (`:148-166`, `:185-196`, `:199-212`,
///        `:215-228`), on real `Song`s built by `support/song_fixture.h`.
///
/// @par The oracle, and why it is a MODEL comparison
///
/// Learning writes the model: every one of the eight things a user can hold under MIDI learn ends
/// with three fields written into a `LearnedMIDI` that lives on a `Song`'s section, a `Clip`, a
/// `NoteRow`'s `Drum`, an `Instrument`, or on `midiEngine.globalMIDICommands`. So the comparison is
/// on **that object**, never on either side's report of it:
///
///   * **Legacy is driven from its own reset** -- `view.startMIDILearn()`, then the overload or the
///     button, then `view.noteOnReceivedForMidiLearn(...)` -- and its answer is captured into a
///     local.
///   * **The model is then poisoned** with a value neither side can produce (a `LearnedMIDI` whose
///     `channelOrZone` is `kPoisonChannel`), and the poison is asserted. A port that stores nothing
///     therefore sees the poison, not legacy's leftovers -- which is the exact blindness plan E2
///     shipped and the discipline migration spec §10.6 exists for.
///   * **The port is driven from its own sentinel** -- its own `MidiLearn`, its own `LearnTarget`,
///     its own note-on -- and the *model* is read back and compared.
///   * **Both sides are asserted to have MOVED**, so a run in which neither wrote anything cannot
///     pass on equality alone.
///
/// The `LearnedMIDI` objects themselves are per-fixture where they can be (sections, clips, note
/// rows, instruments) and process singletons where legacy makes them so
/// (`midiEngine.globalMIDICommands`); the poison step is what makes the singleton case honest.
///
/// @par What this spec counts as uncovered (oracle discipline)
///
///   * **`View::clearMelodicInstrumentMonoExpressionIfPossible`** (`view.cpp:745-765`), reached
///     from the `INSTRUMENT_INPUT` commit (`:728`) and its delete-on-release (`:589`). Not ported
///     (`midi_learn.h`'s "not ported" list -- it needs `model/model_stack.h`, which
///     `scenes-model-stack-outside-context` confines to two files, and the one seam out of that
///     confinement builds a *menu* stack). Legacy really does run it on the legacy side of the
///     `INSTRUMENT_INPUT` examples, so those examples compare the `LearnedMIDI` and **not** the
///     instrument's expression params; the port's *reaching* the call is asserted through
///     `MidiLearn::unported_calls(UnportedCall::ClearMonoExpression)`.
///   * **`iterateAndCallSpecificDeviceHook(HOOK_ON_MIDI_LEARN)`** (`view.cpp:620`). Same shape:
///     counted, not called, and nothing in this layer's model reach could observe it anyway.
///   * **`FlashStorage::writeSettings()`** (`view.cpp:485`). Both sides guard it on
///     `!AudioEngine::audioRoutineLocked`, and `spec::QuietAudio` holds that latch for the whole
///     process -- so on both sides the call is skipped and the `shouldSaveSettingsAfterMidiLearn`
///     decision is what the button examples assert, not the write. (Driving it would put a spec
///     process's settings on the fixture card.)
///   * **The grid repaint** `View::midiLearnFlash` asks for at `:800-802`
///     (`getRootUI()->midiLearnFlash()`). This layer has no `Shell`; what is asserted here is
///     `MidiLearn::timer_repaints()`'s claim, and the shell honouring a service's claim is
///     `tests/spec/shell/flush_spec.cpp`'s.
///
/// @par Why a stub ROOT view, and why it must really be a `RootUI`
///
/// Two of the ported functions call `getRootUI()->midiLearnFlash()` (`view.cpp:490-492`, `:800-802`),
/// and `getRootUI()` is a **C-style cast** of `uiNavigationHierarchy[0]` (`gui/ui/ui.cpp:145-150`).
/// `global_controls_spec.cpp`'s `StubUI` derives from `UI` and gets away with it because nothing on
/// its paths calls a `RootUI` virtual; here that would be a virtual call through a
/// wrong-type pointer. `StubRootUI` below derives from `RootUI`, whose `midiLearnFlash()` is an
/// empty virtual (`gui/ui/root_ui.h:38`), so the call is real and does nothing -- which is exactly
/// what a root view that renders no pads does.
///
/// It also forwards `buttonAction` to `view.buttonAction`, because `UI::buttonAction`'s base
/// implementation does not (`gui/ui/ui.h:98-100`) -- see `global_controls_spec.cpp`'s account of
/// the two callers of `View::buttonAction` and why "the current UI is the root UI" IS the
/// reachability rule.

#include "scenes/services/global_controls.h"
#include "scenes/services/midi_learn.h"

#include "shell/input_lowering.h"
#include "shell/leds.h"
#include "shell/profiles/deluge.h"
#include "shell/router.h"
#include "shell/scene_stack.h"

#include "definitions_cxx.hpp"
#include "gui/ui/root_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "io/midi/learned_midi.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/sound/sound_drum.h"

#include "support/card_access.h" // before <cppspec.hpp> -- see its @warning
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::services::GlobalControls;
using deluge::scenes::services::LearnTarget;
/// @brief `services::MidiLearn`, the class.
///
/// Aliased rather than `using`-declared because the global `enum class MidiLearn`
/// (`definitions_cxx.hpp:133-143`) is the SAME NAME, and the port deliberately reuses that enum for
/// `LearnTarget::kind` rather than mirroring it. A `using` of the class into this file's scope is
/// silently resolved to the enum by GCC, which turns every `rig.ml.foo()` into "request for member
/// in a non-class type". So: `LearnService` is the class, `::MidiLearn` is the enum, and neither
/// spelling can be mistaken for the other.
using LearnService = deluge::scenes::services::MidiLearn;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::deluge_profile;
using deluge::ui::shell::InputLowering;
using deluge::ui::shell::Result;
using deluge::ui::shell::Router;
using deluge::ui::shell::Scene;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::SceneRegistry;
using deluge::ui::shell::SceneStack;
using deluge::ui::shell::Services;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

namespace indicator_leds {
/// Legacy's LED shadow array (`hid/led/indicator_leds.cpp:27`), which `setLedState` writes
/// (`:49`). `indicator_leds.h` declares `ledBlinkState` but not this one, so a bare `extern` is the
/// only spelling available -- `global_controls_spec.cpp` needs the same one.
extern bool ledStates[];
} // namespace indicator_leds

namespace {

/// @brief A `channelOrZone` neither side can produce, used to poison a `LearnedMIDI` between the
///        legacy run and the port run.
///
/// `LearnedMIDI::channelOrZone` is a `uint8_t` whose meaningful range tops out at
/// `MIDI_CHANNEL_MPE_UPPER_ZONE + IS_A_CC` (`io/midi/learned_midi.h:92-94`), and `MIDI_CHANNEL_NONE`
/// is the "nothing learned" value; 0xEE is neither, and no arm of either implementation writes a
/// constant.
constexpr uint8_t kPoisonChannel = 0xEE;

/// @brief The three fields a learn commit writes, as one comparable value.
///
/// Compared as a whole rather than field by field for the reason `midi_device_spec.cpp`'s
/// `CableState::describe` gives: a port that wrote the right note onto the wrong channel, or the
/// right pair with a null cable, must move this and not merely one third of it.
struct Learned {
	const ::MIDICable* cable = nullptr;
	uint8_t channel_or_zone = 0;
	uint8_t note_or_cc = 0;

	static Learned of(const ::LearnedMIDI& m) {
		return Learned{.cable = m.cable, .channel_or_zone = m.channelOrZone, .note_or_cc = m.noteOrCC};
	}
	bool operator==(const Learned&) const = default;

	std::string describe() const {
		return std::string{cable != nullptr ? "cable" : "null"} + ":" + std::to_string(channel_or_zone) + ":"
		       + std::to_string(note_or_cc);
	}
};

/// @brief Put @p m into the poison state and confirm it took.
[[nodiscard]] bool poison(::LearnedMIDI& m) {
	m.cable = nullptr;
	m.channelOrZone = kPoisonChannel;
	m.noteOrCC = 0;
	return m.channelOrZone == kPoisonChannel && m.cable == nullptr;
}

/// @brief A cable this spec owns outright.
///
/// `MIDICable` is abstract and none of the shipped cable types can be built in a host spec without
/// a USB or DIN backend behind it -- `midi_device_spec.cpp:139-162` and `tables_3_spec.cpp:793-802`
/// make the same observation. Two are built, so "the port stored *a* cable" cannot pass for "the
/// port stored the cable it was handed".
struct SpecCable final : ::MIDICable {
	explicit SpecCable(const char* n) : display{n} {}
	void writeToFlash(uint8_t*) override {}
	char const* getDisplayName() override { return display; }
	void sendMessage(MIDIMessage) override {}
	void sendSysex(const uint8_t*, int32_t) override {}
	[[nodiscard]] size_t sendBufferSpace() const override { return 0; }

	const char* display;

protected:
	void writeReferenceAttributesToFile(Serializer&) override {}
};

/// @brief The minimum `Display` legacy needs: `display` is null in a spec process
///        (`hid/display/display.cpp:4`) and `Buttons::buttonAction` dereferences it on every RECORD
///        release (`hid/buttons.cpp:190`).
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

/// @brief A real `RootUI` that claims nothing of its own and forwards to `View::buttonAction`.
///
/// See the file docstring: it must be a `RootUI` and not merely a `UI`, because `getRootUI()`
/// C-casts and two of the ported functions call `midiLearnFlash()` through the result.
class StubRootUI final : public RootUI {
public:
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override {
		return view.buttonAction(b, on, inCardRoutine);
	}
	void renderOLED(deluge::hid::display::oled_canvas::Canvas&) override {}
	UIType getUIType() override { return UIType::NONE; }
};

StubRootUI& stub_root_ui() {
	static StubRootUI instance;
	return instance;
}

StubDisplay& stub_display() {
	static StubDisplay instance;
	return instance;
}

/// @brief `spec::QuietAudio`, held for the whole process.
///
/// Two reasons here, not one. `PlaybackHandler::playButtonPressed` reaches
/// `AudioEngine::routineWithClusterLoading` (`playback/playback_handler.cpp:572`), which segfaults
/// a spec process on the host input ring (`processing/engines/audio_engine.cpp:183`) -- the reason
/// `global_controls_spec.cpp` holds it. And `View::endMIDILearn` writes the settings to flash
/// unless `AudioEngine::audioRoutineLocked` (`view.cpp:483-487`), which is a real card write; the
/// port guards on the same latch, so holding it keeps both sides on the same branch.
spec::QuietAudio& quiet_audio() {
	static spec::QuietAudio instance;
	return instance;
}

/// @brief `deluge_profile()`'s button matrix, inverted -- the `(x, y)` for @p b.
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
deluge::hid::Button legacy_button(ButtonId b) {
	const auto [x, y] = matrix_xy(b);
	return deluge::hid::button::fromXY(x, y);
}

/// @brief A scene that declares itself a root and claims nothing -- the port-side counterpart of
///        `StubRootUI`, and the thing `MidiLearn`'s reachability gate reads.
///
/// Spec-local rather than a registry scene for the reason `Scene::is_root()` is a virtual at all
/// (E4 task 4 §9.1): the registry's scenes are process singletons, so pushing one to satisfy a
/// stack-position rule would leak it into the next example. `root` is a plain member so one example
/// can flip it.
class RootScene final : public Scene {
public:
	SceneKind kind() const override { return SceneKind::FirstApp; }
	bool is_root() const override { return root; }
	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }

	bool root = true;
};

/// @brief The full chain a shell builds, minus the shell: a real `Router` over a real
///        `SceneStack`, a real `InputLowering`, and BOTH services in their shipped slots.
///
/// The whole rig is a `Router` and not a bare `MidiLearn::handle` call precisely because this
/// task's routing claim ("`midi_learn` last, after `global_controls`") is a claim about the chain.
/// `GlobalControls` is live in its own slot, so an example that presses PLAY under learn is
/// really proving that `GlobalControls` abstained and `MidiLearn` picked the event up -- not that
/// a lone service was handed it.
struct LearnRig {
	RootScene root;
	std::array<Scene*, 17> table{};
	SceneRegistry reg{};
	SceneStack stack{reg};
	InputLowering lowering{deluge_profile(), {}};
	LearnService ml{stack};
	GlobalControls gc{stack, lowering, ml};
	Router router{stack, Services{.global_controls = &gc, .midi_learn = &ml}, lowering};
	uint32_t now_ms = 0;

	LearnRig() {
		table[16] = &root;
		reg.by_kind = std::span<Scene* const>{table};
		(void)stack.push({SceneKind::FirstApp});
	}
	LearnRig(const LearnRig&) = delete;
	LearnRig& operator=(const LearnRig&) = delete;

	~LearnRig() {
		while (stack.depth() > 0) {
			stack.pop(SceneKind::Null);
		}
	}

	UiContext ctx() const {
		return UiContext{lowering.held(), lowering.held_pads(), lowering.shift(), lowering.sticky(),
		                 now_ms,          lowering.profile(),   &lowering};
	}

	/// @brief Press or release @p b through the real lowering at @p at_ms and route every event it
	///        emits through the real `Router`.
	void press(ButtonId b, bool on, uint32_t at_ms) {
		now_ms = at_ms;
		const auto [x, y] = matrix_xy(b);
		lowering.begin_tick(at_ms);
		lowering.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
		UiEvent e{UiEvent::Kind::AllReleased};
		while (lowering.next(e)) {
			router.route(e, ctx());
		}
		// `ButtonId::Back`/`SelectEnc` are held back for post-render routing
		// (`shell/input_lowering.cpp:32-34`); none of this spec's buttons is one, but draining is
		// what the shell does and a rig that skipped it would diverge silently later.
		for (const UiEvent& held : lowering.held_back()) {
			router.route_post_render(held, ctx(), lowering.ctx().claims);
		}
		lowering.end_tick(at_ms);
	}
};

/// @brief Put legacy's MIDI-learn state back to "nothing pressed, mode off", and say whether it
///        took.
///
/// "Assert, then clear" applied to a god object: `View`'s learn state is eight members that outlive
/// any one example (`gui/views/view.h:108-118`), and an example that started from the previous
/// one's held thing would compare the wrong `LearnedMIDI`.
[[nodiscard]] bool reset_legacy_learn() {
	currentUIMode = UI_MODE_MIDI_LEARN;
	view.shouldSaveSettingsAfterMidiLearn = false;
	view.endMIDILearn();
	currentUIMode = UI_MODE_NONE;
	view.thingPressedForMidiLearn = ::MidiLearn::NONE;
	view.learnedThing = nullptr;
	view.deleteMidiCommandOnRelease = false;
	view.midiLearnFlashOn = false;
	return currentUIMode == UI_MODE_NONE && view.thingPressedForMidiLearn == ::MidiLearn::NONE
	       && !view.midiLearnFlashOn;
}

/// @brief Release every button legacy thinks is held, so one example's press cannot leak.
void release_all_legacy_buttons() {
	for (ButtonId b : {ButtonId::Play, ButtonId::Record, ButtonId::TapTempo, ButtonId::Learn, ButtonId::Shift}) {
		if (Buttons::isButtonPressed(legacy_button(b))) {
			(void)Buttons::buttonAction(legacy_button(b), false, false);
		}
	}
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

/// @brief Is @p b declared in @p f, and is it lit at @p now_ms?
///
/// A pair rather than a bool, so "nothing declared it" and "declared off" cannot be confused --
/// they are different facts and this spec asserts both.
std::pair<bool, bool> led_declared_on(const deluge::ui::LedFrame& f, ButtonId b, uint32_t now_ms) {
	const std::optional<deluge::ui::LedState> s = led_state(f, b);
	return {s.has_value(), s.has_value() && deluge::ui::shell::led_is_on(*s, now_ms)};
}

/// @brief The one `SpecCable` pair every example learns from, built once.
struct Cables {
	SpecCable a{"A"};
	SpecCable b{"B"};
};
Cables& cables() {
	static Cables instance;
	return instance;
}

} // namespace

// ------------------------------------------------------------------------------------------------

// clang-format off
describe midi_learn_spec_body("scenes::services::MidiLearn", $ {
	before_each([] {
		setRootUILowLevel(&stub_root_ui());
		display = &stub_display();
		(void)quiet_audio();
		(void)cables();
	});

	// -- Step 1: the five `*MidiLearnPadPressed` overloads are one question --------------------
	//
	// One example per overload. Each drives LEGACY's own overload plus its note-on, captures the
	// `LearnedMIDI`, poisons it, then drives the port from `LearnTarget::<factory>` and compares
	// the same model object. The five together are the executable form of the report's mapping
	// table: if any one of them mapped onto the wrong `LearnedMIDI`, its example fails.

	it("SECTION: `sectionMidiLearnPadPressed` maps onto `LearnTarget::section`", _ {
		static spec::SongFixture fixture = spec::make_synth_song();
		::LearnedMIDI& slot = fixture.song->sections[2].launchMIDICommand;

		// --- Legacy, from its own reset (`view.cpp:518-530` + `:623-743`) ------------------
		expect(reset_legacy_learn()).to_be_true();
		slot.clear();
		view.startMIDILearn();
		view.sectionMidiLearnPadPressed(true, 2);
		view.noteOnReceivedForMidiLearn(cables().a, 3, 64, 127);
		const Learned legacy = Learned::of(slot);
		expect(legacy.cable == &cables().a).to_be_true();
		expect(legacy.channel_or_zone == 3).to_be_true();
		expect(legacy.note_or_cc == 64).to_be_true();
		view.sectionMidiLearnPadPressed(false, 2);
		expect(reset_legacy_learn()).to_be_true();

		// --- Poison ------------------------------------------------------------------------
		expect(poison(slot)).to_be_true();

		// --- The port, from its own sentinel ------------------------------------------------
		LearnRig rig;
		rig.ml.set_song(fixture.song);
		rig.ml.begin();
		rig.ml.thing_pressed(LearnTarget::section(*fixture.song, 2), true);
		rig.ml.note_on_received(cables().a, 3, 64, 127);
		const Learned port = Learned::of(slot);

		expect(port.channel_or_zone != kPoisonChannel).to_be_true();
		expect(port.describe()).to_equal(legacy.describe());
	});

	it("CLIP: `clipStatusMidiLearnPadPressed` maps onto `LearnTarget::clip`", _ {
		static spec::SongFixture fixture = spec::make_synth_song();
		::LearnedMIDI& slot = fixture.clip->muteMIDICommand;

		expect(reset_legacy_learn()).to_be_true();
		slot.clear();
		view.startMIDILearn();
		view.clipStatusMidiLearnPadPressed(true, fixture.clip); // `view.cpp:532-544`
		view.noteOnReceivedForMidiLearn(cables().b, 5, 70, 100);
		const Learned legacy = Learned::of(slot);
		expect(legacy.cable == &cables().b).to_be_true();
		view.clipStatusMidiLearnPadPressed(false, fixture.clip);
		expect(reset_legacy_learn()).to_be_true();

		expect(poison(slot)).to_be_true();

		LearnRig rig;
		rig.ml.set_song(fixture.song);
		rig.ml.begin();
		rig.ml.thing_pressed(LearnTarget::clip(*fixture.clip), true);
		rig.ml.note_on_received(cables().b, 5, 70, 100);

		expect(Learned::of(slot).channel_or_zone != kPoisonChannel).to_be_true();
		expect(Learned::of(slot).describe()).to_equal(legacy.describe());
	});

	it("NOTEROW_MUTE: `noteRowMuteMidiLearnPadPressed` maps onto `LearnTarget::note_row_mute`", _ {
		static spec::SongFixture fixture = spec::make_kit_song();
		static SoundDrum* drum = spec::add_drum<SoundDrum>(fixture);
		expect(drum != nullptr).to_be_true();
		NoteRow* const row = fixture.clip->getNoteRowForDrum(drum);
		expect(row != nullptr).to_be_true();
		expect(row->drum == drum).to_be_true();
		::LearnedMIDI& slot = drum->muteMIDICommand;

		expect(reset_legacy_learn()).to_be_true();
		slot.clear();
		view.startMIDILearn();
		view.noteRowMuteMidiLearnPadPressed(true, row); // `view.cpp:546-558`
		view.noteOnReceivedForMidiLearn(cables().a, 7, 41, 127);
		const Learned legacy = Learned::of(slot);
		expect(legacy.channel_or_zone == 7).to_be_true();
		view.noteRowMuteMidiLearnPadPressed(false, row);
		expect(reset_legacy_learn()).to_be_true();

		expect(poison(slot)).to_be_true();

		LearnRig rig;
		rig.ml.set_song(fixture.song);
		rig.ml.begin();
		rig.ml.thing_pressed(LearnTarget::note_row_mute(*row), true);
		rig.ml.note_on_received(cables().a, 7, 41, 127);

		expect(Learned::of(slot).channel_or_zone != kPoisonChannel).to_be_true();
		expect(Learned::of(slot).describe()).to_equal(legacy.describe());
	});

	it("DRUM_INPUT: `drumMidiLearnPadPressed` maps onto `LearnTarget::drum_input`", _ {
		// The one overload that takes TWO model objects (`view.cpp:560-577`): the drum and its
		// kit, the second only so `grabVelocityToLevel…ForDrum` can search faster (`:566-567`).
		static spec::SongFixture fixture = spec::make_kit_song();
		static SoundDrum* drum = spec::add_drum<SoundDrum>(fixture);
		auto* const kit = static_cast<Kit*>(fixture.instrument);
		::LearnedMIDI& slot = drum->midiInput;

		expect(reset_legacy_learn()).to_be_true();
		slot.clear();
		view.startMIDILearn();
		view.drumMidiLearnPadPressed(true, drum, kit);
		expect(view.drumPressedForMIDILearn == drum).to_be_true(); // `:565`
		expect(view.kitPressedForMIDILearn == kit).to_be_true();   // `:566`
		view.noteOnReceivedForMidiLearn(cables().b, 2, 36, 127);   // the `:629-660` arm
		const Learned legacy = Learned::of(slot);
		expect(legacy.note_or_cc == 36).to_be_true();
		view.drumMidiLearnPadPressed(false, drum, kit);
		expect(reset_legacy_learn()).to_be_true();

		expect(poison(slot)).to_be_true();

		LearnRig rig;
		rig.ml.set_song(fixture.song);
		rig.ml.begin();
		rig.ml.thing_pressed(LearnTarget::drum_input(*drum, *kit), true);
		expect(rig.ml.pressed().drum == drum).to_be_true();
		expect(rig.ml.pressed().kit == kit).to_be_true();
		rig.ml.note_on_received(cables().b, 2, 36, 127);

		expect(Learned::of(slot).channel_or_zone != kPoisonChannel).to_be_true();
		expect(Learned::of(slot).describe()).to_equal(legacy.describe());
	});

	it("INSTRUMENT_INPUT: `instrumentMidiLearnPadPressed` maps onto `LearnTarget::instrument_input`", _ {
		// The arm that does NOT jump to the shared write: `:730-732` repeats the three fields
		// itself, which is why the port runs the shared write for it after the extras rather than
		// inside them.
		static spec::SongFixture fixture = spec::make_synth_song();
		::LearnedMIDI& slot = fixture.instrument->midiInput;

		expect(reset_legacy_learn()).to_be_true();
		slot.clear();
		view.startMIDILearn();
		view.instrumentMidiLearnPadPressed(true, fixture.instrument); // `view.cpp:579-595`
		expect(view.instrumentPressedForMIDILearn == fixture.instrument).to_be_true();
		view.noteOnReceivedForMidiLearn(cables().a, 9, 55, 127); // the `:677-740` arm
		const Learned legacy = Learned::of(slot);
		expect(legacy.channel_or_zone == 9).to_be_true();
		view.instrumentMidiLearnPadPressed(false, fixture.instrument);
		expect(reset_legacy_learn()).to_be_true();

		expect(poison(slot)).to_be_true();

		LearnRig rig;
		rig.ml.set_song(fixture.song);
		rig.ml.begin();
		rig.ml.thing_pressed(LearnTarget::instrument_input(*fixture.instrument), true);
		expect(rig.ml.pressed().instrument == fixture.instrument).to_be_true();
		const uint32_t before = rig.ml.unported_calls(LearnService::UnportedCall::ClearMonoExpression);
		rig.ml.note_on_received(cables().a, 9, 55, 127);

		expect(Learned::of(slot).channel_or_zone != kPoisonChannel).to_be_true();
		expect(Learned::of(slot).describe()).to_equal(legacy.describe());
		// The one call this arm cannot spell was REACHED -- see the file docstring's uncovered
		// list. Counted, not made.
		expect(rig.ml.unported_calls(LearnService::UnportedCall::ClearMonoExpression) == before + 1).to_be_true();
	});

	// -- The three button targets ----------------------------------------------------------------

	it("LEARN: the button is the mode's on/off switch, on both sides", _ {
		static spec::SongFixture fixture = spec::make_synth_song();
		expect(fixture.song != nullptr).to_be_true();

		// --- Legacy (`view.cpp:185-196`) ----------------------------------------------------
		release_all_legacy_buttons();
		expect(reset_legacy_learn()).to_be_true();
		(void)Buttons::buttonAction(legacy_button(ButtonId::Learn), true, false);
		const bool legacy_on = (currentUIMode == UI_MODE_MIDI_LEARN);
		(void)Buttons::buttonAction(legacy_button(ButtonId::Learn), false, false);
		const bool legacy_off = (currentUIMode != UI_MODE_MIDI_LEARN);
		expect(legacy_on).to_be_true();
		expect(legacy_off).to_be_true();
		expect(reset_legacy_learn()).to_be_true();

		// --- The port, through the real chain -----------------------------------------------
		release_all_legacy_buttons();
		LearnRig rig;
		rig.ml.set_song(fixture.song);
		expect(rig.ml.active()).to_be_false();
		rig.press(ButtonId::Learn, true, 1'000);
		const bool port_on = rig.ml.active();
		rig.press(ButtonId::Learn, false, 1'010);
		const bool port_off = !rig.ml.active();

		expect(port_on == legacy_on).to_be_true();
		expect(port_off == legacy_off).to_be_true();
		// Nothing fell off the end of the chain: `GlobalControls` declined LEARN
		// (`ViewArm::LearnOwns`) and `MidiLearn` claimed it.
		expect(rig.router.dropped_events() == 0).to_be_true();
	});

	it("PLAY under learn: the button learns, and the transport tail does NOT run", _ {
		// The two halves of legacy's `:199-212` in one example. `midiEngine.globalMIDICommands` is
		// a process singleton, so the poison step is what makes this a real comparison.
		static spec::SongFixture fixture = spec::make_synth_song();
		::LearnedMIDI& slot = midiEngine.globalMIDICommands[static_cast<size_t>(GlobalMIDICommand::PLAY)];

		// --- Legacy -------------------------------------------------------------------------
		release_all_legacy_buttons();
		expect(reset_legacy_learn()).to_be_true();
		slot.clear();
		playbackHandler.playbackState = 0;
		view.startMIDILearn();
		(void)Buttons::buttonAction(legacy_button(ButtonId::Play), true, false);
		expect(view.thingPressedForMidiLearn == ::MidiLearn::PLAY_BUTTON).to_be_true();
		view.noteOnReceivedForMidiLearn(cables().a, 1, 60, 127);
		const Learned legacy = Learned::of(slot);
		const uint8_t legacy_playback = playbackHandler.playbackState;
		expect(legacy.channel_or_zone == 1).to_be_true();
		// The tail was skipped: `view.cpp:469`'s `DEALT_WITH` makes `hid/buttons.cpp:139-141`
		// jump past `:144-167`, so playback never started.
		expect(legacy_playback == 0).to_be_true();
		(void)Buttons::buttonAction(legacy_button(ButtonId::Play), false, false);
		expect(reset_legacy_learn()).to_be_true();

		expect(poison(slot)).to_be_true();
		playbackHandler.playbackState = 0;

		// --- The port, through the real chain -----------------------------------------------
		release_all_legacy_buttons();
		LearnRig rig;
		rig.ml.set_song(fixture.song);
		rig.gc.set_song(fixture.song);
		// LEARN is a HOLD, not a toggle: `view.cpp:190-195` starts the mode on the press and ends
		// it on the release, so the gesture legacy's own `startMIDILearn()` stands in for is
		// "LEARN still down". Releasing it here would call `end()` and there would be no mode left
		// for the next button to be learned under.
		rig.press(ButtonId::Learn, true, 2'000);
		expect(rig.ml.active()).to_be_true();
		rig.press(ButtonId::Play, true, 2'020);
		expect(rig.ml.pressed().kind == ::MidiLearn::PLAY_BUTTON).to_be_true();
		rig.ml.note_on_received(cables().a, 1, 60, 127);

		expect(Learned::of(slot).channel_or_zone != kPoisonChannel).to_be_true();
		expect(Learned::of(slot).describe()).to_equal(legacy.describe());
		// `GlobalControls` really did abstain: playback is still stopped.
		expect(playbackHandler.playbackState == legacy_playback).to_be_true();
	});

	it("RECORD under learn: the same shape, on the RECORD slot", _ {
		static spec::SongFixture fixture = spec::make_synth_song();
		::LearnedMIDI& slot = midiEngine.globalMIDICommands[static_cast<size_t>(GlobalMIDICommand::RECORD)];

		release_all_legacy_buttons();
		expect(reset_legacy_learn()).to_be_true();
		slot.clear();
		playbackHandler.recording = RecordingMode::OFF;
		view.startMIDILearn();
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), true, false); // `view.cpp:215-228`
		expect(view.thingPressedForMidiLearn == ::MidiLearn::RECORD_BUTTON).to_be_true();
		view.noteOnReceivedForMidiLearn(cables().b, 4, 62, 127);
		const Learned legacy = Learned::of(slot);
		expect(playbackHandler.recording == RecordingMode::OFF).to_be_true();
		(void)Buttons::buttonAction(legacy_button(ButtonId::Record), false, false);
		expect(reset_legacy_learn()).to_be_true();

		expect(poison(slot)).to_be_true();
		playbackHandler.recording = RecordingMode::OFF;

		release_all_legacy_buttons();
		LearnRig rig;
		rig.ml.set_song(fixture.song);
		rig.gc.set_song(fixture.song);
		// LEARN is a HOLD, not a toggle: `view.cpp:190-195` starts the mode on the press and ends
		// it on the release, so the gesture legacy's own `startMIDILearn()` stands in for is
		// "LEARN still down". Releasing it here would call `end()` and there would be no mode left
		// for the next button to be learned under.
		rig.press(ButtonId::Learn, true, 3'000);
		expect(rig.ml.active()).to_be_true();
		rig.press(ButtonId::Record, true, 3'020);
		expect(rig.ml.pressed().kind == ::MidiLearn::RECORD_BUTTON).to_be_true();
		rig.ml.note_on_received(cables().b, 4, 62, 127);

		expect(Learned::of(slot).channel_or_zone != kPoisonChannel).to_be_true();
		expect(Learned::of(slot).describe()).to_equal(legacy.describe());
		expect(playbackHandler.recording == RecordingMode::OFF).to_be_true();
	});

	it("TAP_TEMPO under learn: learns TAP, and the metronome/tap arm stays out of it", _ {
		// Legacy's two TAP_TEMPO branches are disjoint by its own `if`/`else if` (`view.cpp:148`
		// vs `:167`). The port splits them across two services, so the disjointness has to be
		// re-proved: `GlobalControls` asks `LearnMode::active()` and abstains.
		static spec::SongFixture fixture = spec::make_synth_song();
		::LearnedMIDI& slot = midiEngine.globalMIDICommands[static_cast<size_t>(GlobalMIDICommand::TAP)];

		release_all_legacy_buttons();
		expect(reset_legacy_learn()).to_be_true();
		slot.clear();
		playbackHandler.metronomeOn = false;
		view.startMIDILearn();
		(void)Buttons::buttonAction(legacy_button(ButtonId::TapTempo), true, false);
		expect(view.thingPressedForMidiLearn == ::MidiLearn::TAP_TEMPO_BUTTON).to_be_true();
		view.noteOnReceivedForMidiLearn(cables().a, 6, 48, 127);
		const Learned legacy = Learned::of(slot);
		expect(playbackHandler.metronomeOn).to_be_false();
		(void)Buttons::buttonAction(legacy_button(ButtonId::TapTempo), false, false);
		expect(reset_legacy_learn()).to_be_true();

		expect(poison(slot)).to_be_true();
		playbackHandler.metronomeOn = false;

		release_all_legacy_buttons();
		LearnRig rig;
		rig.ml.set_song(fixture.song);
		rig.gc.set_song(fixture.song);
		// LEARN is a HOLD, not a toggle: `view.cpp:190-195` starts the mode on the press and ends
		// it on the release, so the gesture legacy's own `startMIDILearn()` stands in for is
		// "LEARN still down". Releasing it here would call `end()` and there would be no mode left
		// for the next button to be learned under.
		rig.press(ButtonId::Learn, true, 4'000);
		expect(rig.ml.active()).to_be_true();
		rig.press(ButtonId::TapTempo, true, 4'020);
		expect(rig.ml.pressed().kind == ::MidiLearn::TAP_TEMPO_BUTTON).to_be_true();
		rig.ml.note_on_received(cables().a, 6, 48, 127);

		expect(Learned::of(slot).channel_or_zone != kPoisonChannel).to_be_true();
		expect(Learned::of(slot).describe()).to_equal(legacy.describe());
		expect(playbackHandler.metronomeOn).to_be_false();
	});

	// -- Step 5: `endMidiLearnPressSession`'s delete-on-release -----------------------------------

	it("delete on release: holding a learned thing and letting go CLEARS it, on both sides", _ {
		// `view.cpp:525-527` for the pads. The model is asserted before AND after on each side:
		// a spec that only checked "empty afterwards" would pass against a port that never wrote
		// anything in the first place.
		static spec::SongFixture fixture = spec::make_synth_song();
		::LearnedMIDI& slot = fixture.song->sections[1].launchMIDICommand;

		// --- Legacy -------------------------------------------------------------------------
		expect(reset_legacy_learn()).to_be_true();
		slot.clear();
		view.startMIDILearn();
		view.sectionMidiLearnPadPressed(true, 1);
		view.noteOnReceivedForMidiLearn(cables().a, 3, 64, 127);
		view.sectionMidiLearnPadPressed(false, 1);
		expect(slot.containsSomething()).to_be_true(); // learned, and the release did NOT wipe it
		// Now hold it again with no note-on in between, and let go.
		view.sectionMidiLearnPadPressed(true, 1);
		expect(view.deleteMidiCommandOnRelease).to_be_true(); // `:521`
		view.sectionMidiLearnPadPressed(false, 1);
		const bool legacy_cleared = !slot.containsSomething();
		expect(legacy_cleared).to_be_true();
		expect(reset_legacy_learn()).to_be_true();

		// --- Poison: make the slot LOOK learned, so "cleared" cannot be inherited -------------
		expect(poison(slot)).to_be_true();
		expect(slot.containsSomething()).to_be_true();

		// --- The port -----------------------------------------------------------------------
		LearnRig rig;
		rig.ml.set_song(fixture.song);
		rig.ml.begin();
		rig.ml.thing_pressed(LearnTarget::section(*fixture.song, 1), true);
		rig.ml.note_on_received(cables().a, 3, 64, 127);
		rig.ml.thing_pressed(LearnTarget::section(*fixture.song, 1), false);
		expect(slot.containsSomething()).to_be_true();
		expect(Learned::of(slot).channel_or_zone == 3).to_be_true();

		rig.ml.thing_pressed(LearnTarget::section(*fixture.song, 1), true);
		rig.ml.thing_pressed(LearnTarget::section(*fixture.song, 1), false);
		expect(slot.containsSomething()).to_be_false();
		expect(rig.ml.pressed().kind == ::MidiLearn::NONE).to_be_true();
	});

	it("delete on release: a release of a DIFFERENT kind of thing does nothing", _ {
		// `view.cpp:524`'s `else if (thingPressedForMidiLearn == MidiLearn::SECTION)`. Legacy has
		// one held thing, so a release whose kind does not match falls through -- which is what
		// stops a second finger's release from wiping the first finger's target.
		static spec::SongFixture fixture = spec::make_synth_song();
		::LearnedMIDI& section_slot = fixture.song->sections[3].launchMIDICommand;

		expect(reset_legacy_learn()).to_be_true();
		section_slot.clear();
		view.startMIDILearn();
		view.sectionMidiLearnPadPressed(true, 3);
		view.clipStatusMidiLearnPadPressed(false, fixture.clip); // wrong kind: legacy ignores it
		const bool legacy_still_held = (view.thingPressedForMidiLearn == ::MidiLearn::SECTION);
		expect(legacy_still_held).to_be_true();
		expect(reset_legacy_learn()).to_be_true();

		LearnRig rig;
		rig.ml.set_song(fixture.song);
		rig.ml.begin();
		rig.ml.thing_pressed(LearnTarget::section(*fixture.song, 3), true);
		rig.ml.thing_pressed(LearnTarget::clip(*fixture.clip), false);
		expect(rig.ml.pressed().kind == ::MidiLearn::SECTION).to_be_true();
		expect(rig.ml.pressed().thing == &section_slot).to_be_true();
	});

	// -- Step 4: the flash -----------------------------------------------------------------------

	it("the flash is time-driven: the phase moves on a QUIET tick, with no input at all", _ {
		// `View::midiLearnFlash` (`view.cpp:796-816`) re-arms itself at `kFastFlashTime` and is
		// driven from `gui/ui_timer_manager.cpp:75`. Here that is `next_timer()` + `tick()`, and
		// the point of the example is that NOTHING is pressed between the assertions.
		LearnRig rig;
		expect(rig.ml.next_timer().has_value()).to_be_false(); // nothing armed while learn is off

		rig.ml.begin();
		// `startMIDILearn` calls `midiLearnFlash()` (`:477`), which toggles the phase ON and arms
		// the timer (`:797-798`).
		expect(rig.ml.flash_on()).to_be_true();
		expect(rig.ml.next_timer().has_value()).to_be_true();
		expect(*rig.ml.next_timer() == LearnService::kFlashMs).to_be_true();

		// A quiet tick that does not reach the deadline: the phase must NOT move, and the
		// remaining time must come down by exactly what was spent.
		rig.ml.tick(1'020, 20);
		expect(rig.ml.flash_on()).to_be_true();
		expect(*rig.ml.next_timer() == LearnService::kFlashMs - 20).to_be_true();

		// A quiet tick that reaches it: the phase flips and the timer re-arms, with no input.
		rig.ml.tick(1'060, LearnService::kFlashMs - 20);
		expect(rig.ml.flash_on()).to_be_false();
		expect(*rig.ml.next_timer() == LearnService::kFlashMs).to_be_true();

		// And again, so "it flipped once" cannot pass for "it is flashing".
		rig.ml.tick(1'120, LearnService::kFlashMs);
		expect(rig.ml.flash_on()).to_be_true();
	});

	it("the flash arms the GRID and never the OLED", _ {
		// Shell spec §15.1/§15.3: the grid's flush diffs against `last_sent`, so an unneeded grid
		// arm costs a repaint; an OLED surface is blitted unconditionally once it has rendered, so
		// an unneeded OLED arm is a permanent blit at the timer's cadence. Every reader of
		// `midiLearnFlashOn` in legacy is a pad renderer.
		LearnRig rig;
		const Scene::RepaintSurfaces s = rig.ml.timer_repaints();
		expect(s.grid).to_be_true();
		expect(s.oled).to_be_false();
		expect(s.any()).to_be_true();
	});

	it("ending learn stops the flash and its timer", _ {
		// `view.cpp:488-489`: `unsetTimer(MIDI_LEARN_FLASH)` and `midiLearnFlashOn = false`.
		// Asserted set, then asserted cleared -- never defensively cleared.
		LearnRig rig;
		rig.ml.begin();
		expect(rig.ml.flash_on()).to_be_true();
		expect(rig.ml.next_timer().has_value()).to_be_true();

		rig.ml.end();
		expect(rig.ml.flash_on()).to_be_false();
		expect(rig.ml.next_timer().has_value()).to_be_false();

		// A tick after the end must not resurrect it.
		rig.ml.tick(9'000, 500);
		expect(rig.ml.flash_on()).to_be_false();
		expect(rig.ml.next_timer().has_value()).to_be_false();
	});

	// -- LEDs ------------------------------------------------------------------------------------

	it("the flash LEDs follow legacy's own three conditions, and are compared against legacy's shadow", _ {
		// `view.cpp:804-815`: PLAY, RECORD and TAP_TEMPO are written from `midiLearnFlashOn`, but
		// only when the command is already learned OR that button is the held thing. An unlearned,
		// unheld button is left to whoever set it -- which here is `GlobalControls`.
		static spec::SongFixture fixture = spec::make_synth_song();
		::LearnedMIDI& play_slot = midiEngine.globalMIDICommands[static_cast<size_t>(GlobalMIDICommand::PLAY)];
		::LearnedMIDI& record_slot = midiEngine.globalMIDICommands[static_cast<size_t>(GlobalMIDICommand::RECORD)];
		::LearnedMIDI& tap_slot = midiEngine.globalMIDICommands[static_cast<size_t>(GlobalMIDICommand::TAP)];

		// --- Legacy: PLAY learned, RECORD and TAP not -----------------------------------------
		expect(reset_legacy_learn()).to_be_true();
		play_slot.clear();
		record_slot.clear();
		tap_slot.clear();
		play_slot.cable = &cables().a;
		play_slot.channelOrZone = 1;
		play_slot.noteOrCC = 60;
		expect(play_slot.containsSomething()).to_be_true();
		expect(record_slot.containsSomething()).to_be_false();

		indicator_leds::setLedState(IndicatorLED::PLAY, false);
		indicator_leds::setLedState(IndicatorLED::RECORD, false);
		view.midiLearnFlashOn = false;
		view.midiLearnFlash(); // toggles ON, then writes only the conditions that hold
		const bool legacy_play = indicator_leds::ledStates[static_cast<int32_t>(IndicatorLED::PLAY)];
		const bool legacy_record = indicator_leds::ledStates[static_cast<int32_t>(IndicatorLED::RECORD)];
		expect(legacy_play).to_be_true();    // learned -> follows the phase
		expect(legacy_record).to_be_false(); // not learned, not held -> untouched
		expect(reset_legacy_learn()).to_be_true();

		// --- Poison legacy's shadow, so the port cannot inherit it ---------------------------
		indicator_leds::setLedState(IndicatorLED::PLAY, false);
		indicator_leds::setLedState(IndicatorLED::RECORD, true);
		expect(indicator_leds::ledStates[static_cast<int32_t>(IndicatorLED::PLAY)]).to_be_false();

		// --- The port, from its own sentinel ------------------------------------------------
		LearnRig rig;
		rig.ml.set_song(fixture.song);
		rig.ml.begin(); // phase ON, exactly as `startMIDILearn` -> `midiLearnFlash` leaves it
		expect(rig.ml.flash_on()).to_be_true();
		deluge::ui::LedFrame frame;
		rig.ml.leds(frame);

		const auto [play_declared, play_on] = led_declared_on(frame, ButtonId::Play, 0);
		const auto [record_declared, record_on] = led_declared_on(frame, ButtonId::Record, 0);
		expect(play_declared).to_be_true();
		expect(play_on == legacy_play).to_be_true();
		// The RECORD half is the interesting one: legacy did not WRITE it, and the port does not
		// DECLARE it, which is the declarative spelling of the same fact.
		expect(record_declared).to_be_false();

		// Hold RECORD, and it starts following the phase without ever being learned (`:809`).
		rig.ml.thing_pressed(LearnTarget::global_command(GlobalMIDICommand::RECORD), true);
		deluge::ui::LedFrame held_frame;
		rig.ml.leds(held_frame);
		const auto [held_declared, held_on] = led_declared_on(held_frame, ButtonId::Record, 0);
		expect(held_declared).to_be_true();
		expect(held_on).to_be_true();

		// And the declaration really is the PHASE, not a constant: one quiet half-period later,
		// both LEDs are declared the other way. This is what ties `leds()` to `flash_on()` --
		// legacy writes them from the one `midiLearnFlashOn` bit (`:797`, `:806`, `:810`), so a
		// port whose LEDs did not move with the phase would be a different UI.
		rig.ml.tick(1'000 + LearnService::kFlashMs, LearnService::kFlashMs);
		expect(rig.ml.flash_on()).to_be_false();
		deluge::ui::LedFrame flipped;
		rig.ml.leds(flipped);
		const auto [play2_declared, play2_on] = led_declared_on(flipped, ButtonId::Play, 0);
		const auto [rec2_declared, rec2_on] = led_declared_on(flipped, ButtonId::Record, 0);
		expect(play2_declared).to_be_true();
		expect(play2_on).to_be_false();
		expect(rec2_declared).to_be_true();
		expect(rec2_on).to_be_false();

		play_slot.clear();
	});

	it("the LEARN LED blinks at kFastFlashTime while learn is on, and is declared off when it is not", _ {
		// `indicator_leds::blinkLed(IndicatorLED::LEARN, 255, 1)` (`view.cpp:478`) -- forever, at
		// `blinkingType` 1's period -- and `setLedState(IndicatorLED::LEARN, false)` (`:499`).
		LearnRig rig;
		deluge::ui::LedFrame off_frame;
		rig.ml.leds(off_frame);
		const std::optional<deluge::ui::LedState> off = led_state(off_frame, ButtonId::Learn);
		expect(off.has_value()).to_be_true();
		expect(off->kind == deluge::ui::LedState::Kind::Off).to_be_true();

		rig.ml.begin();
		deluge::ui::LedFrame on_frame;
		rig.ml.leds(on_frame);
		const std::optional<deluge::ui::LedState> on = led_state(on_frame, ButtonId::Learn);
		expect(on.has_value()).to_be_true();
		expect(on->kind == deluge::ui::LedState::Kind::Blink).to_be_true();
		expect(on->period_ms == LearnService::kFlashMs).to_be_true();
		// A blink with no deadline: legacy's `numBlinks == 255` never folds back
		// (`hid/led/indicator_leds.cpp:69-71`, `:117-126`).
		expect(on->until_ms == 0).to_be_true();
		// And it really does change on its own, which is what makes the declaration a blink rather
		// than a name for one.
		expect(deluge::ui::shell::led_is_on(*on, 0) != deluge::ui::shell::led_is_on(*on, LearnService::kFlashMs))
		    .to_be_true();
	});

	// -- The chain, and the reachability gate ----------------------------------------------------

	it("the chain: outside learn, PLAY reaches the transport tail and MidiLearn declines", _ {
		// The other half of the routing claim. `MidiLearn` is routed LAST; outside learn its
		// `Play` arm reports unhandled and `GlobalControls`'s tail has already claimed the event,
		// so `dropped_events()` stays at zero and playback really starts.
		static spec::SongFixture fixture = spec::make_synth_song();
		release_all_legacy_buttons();
		expect(reset_legacy_learn()).to_be_true();
		playbackHandler.playbackState = 0;

		LearnRig rig;
		rig.ml.set_song(fixture.song);
		rig.gc.set_song(fixture.song);
		expect(rig.ml.active()).to_be_false();
		rig.press(ButtonId::Play, true, 5'000);

		expect(playbackHandler.playbackState != 0).to_be_true();
		expect(rig.ml.pressed().kind == ::MidiLearn::NONE).to_be_true();
		expect(rig.router.dropped_events() == 0).to_be_true();

		playbackHandler.playbackState = 0;
	});

	it("the root gate: with a NON-root scene on top, LEARN does nothing here", _ {
		// `View::buttonAction` is the ROOT view's fallback and only the root view's (see the
		// header's proof). Legacy's LEARN inside a menu is `SoundEditor::buttonAction`'s own arm
		// (`gui/ui/sound_editor.cpp:467-499`), which is `MenuScene`'s to port -- so an ungated
		// `MidiLearn` would put a second, wrong owner on the button.
		LearnRig rig;
		rig.root.root = false;

		rig.press(ButtonId::Learn, true, 6'000);
		expect(rig.ml.active()).to_be_false();
		// Not merely unhandled by this service: nothing in the chain claimed it, which is exactly
		// what legacy does when the current UI declines and no tail arm names the button.
		expect(rig.router.dropped_events() > 0).to_be_true();

		// And the gate is the only thing refusing: flip it back and the same press works.
		rig.root.root = true;
		rig.press(ButtonId::Learn, true, 6'010);
		expect(rig.ml.active()).to_be_true();
	});

	// -- `ccReceivedForMIDILearn` ------------------------------------------------------------------

	it("a CC on a non-instrument target is learned as a note-on with the IS_A_CC bias", _ {
		// `view.cpp:788-791`. The bias is what lets one `LearnedMIDI` hold either a note or a CC
		// (`io/midi/learned_midi.h:90-93`).
		static spec::SongFixture fixture = spec::make_synth_song();
		::LearnedMIDI& slot = fixture.song->sections[0].launchMIDICommand;

		expect(reset_legacy_learn()).to_be_true();
		slot.clear();
		view.startMIDILearn();
		view.sectionMidiLearnPadPressed(true, 0);
		view.ccReceivedForMIDILearn(cables().a, 2, 74, 100);
		const Learned legacy = Learned::of(slot);
		expect(legacy.channel_or_zone == 2 + IS_A_CC).to_be_true();
		expect(legacy.note_or_cc == 74).to_be_true();
		view.sectionMidiLearnPadPressed(false, 0);
		expect(reset_legacy_learn()).to_be_true();

		expect(poison(slot)).to_be_true();

		LearnRig rig;
		rig.ml.set_song(fixture.song);
		rig.ml.begin();
		rig.ml.thing_pressed(LearnTarget::section(*fixture.song, 0), true);
		rig.ml.cc_received(cables().a, 2, 74, 100);

		expect(Learned::of(slot).channel_or_zone != kPoisonChannel).to_be_true();
		expect(Learned::of(slot).describe()).to_equal(legacy.describe());
	});

	it("a CC with value 0 is ignored, on both sides", _ {
		// `view.cpp:789`'s `if (value)`. A separate example because it is the one arm of
		// `ccReceivedForMIDILearn` whose whole content is a refusal.
		static spec::SongFixture fixture = spec::make_synth_song();
		::LearnedMIDI& slot = fixture.song->sections[4].launchMIDICommand;

		expect(reset_legacy_learn()).to_be_true();
		slot.clear();
		view.startMIDILearn();
		view.sectionMidiLearnPadPressed(true, 4);
		view.ccReceivedForMIDILearn(cables().a, 2, 74, 0);
		expect(slot.containsSomething()).to_be_false();
		// …but `deleteMidiCommandOnRelease` was cleared anyway (`:769`, above the `if`), which is
		// legacy's own behaviour and worth pinning: a zero-valued CC cancels the delete.
		expect(view.deleteMidiCommandOnRelease).to_be_false();
		view.sectionMidiLearnPadPressed(false, 4);
		expect(reset_legacy_learn()).to_be_true();

		expect(poison(slot)).to_be_true();
		slot.clear();

		LearnRig rig;
		rig.ml.set_song(fixture.song);
		rig.ml.begin();
		rig.ml.thing_pressed(LearnTarget::section(*fixture.song, 4), true);
		rig.ml.cc_received(cables().a, 2, 74, 0);
		expect(slot.containsSomething()).to_be_false();
		// The release does not clear it either -- the same cancellation, port side.
		rig.ml.thing_pressed(LearnTarget::section(*fixture.song, 4), false);
		expect(slot.containsSomething()).to_be_false();
	});

	it("a CC on a MIDI-out instrument learns the input CHANNEL, not a note", _ {
		// `view.cpp:772-783`, the arm with three conjuncts: the held thing is an instrument, the
		// CURRENT output is a MIDI instrument, and nothing has been learned into the slot yet.
		static spec::SongFixture fixture = spec::make_midi_song();
		expect(fixture.instrument->type == OutputType::MIDI_OUT).to_be_true();
		::LearnedMIDI& slot = fixture.instrument->midiInput;

		expect(reset_legacy_learn()).to_be_true();
		slot.clear();
		view.startMIDILearn();
		view.instrumentMidiLearnPadPressed(true, fixture.instrument);
		view.ccReceivedForMIDILearn(cables().b, 8, 74, 100);
		const Learned legacy = Learned::of(slot);
		// The channel, with no CC bias, and `noteOrCC` untouched -- which is what distinguishes
		// this arm from the note-on fallback the previous example drives.
		expect(legacy.channel_or_zone == 8).to_be_true();
		expect(legacy.cable == &cables().b).to_be_true();
		view.instrumentMidiLearnPadPressed(false, fixture.instrument);
		expect(reset_legacy_learn()).to_be_true();

		expect(poison(slot)).to_be_true();
		slot.clear(); // the arm's third conjunct: it only fires on an EMPTY slot (`:778`)

		LearnRig rig;
		rig.ml.set_song(fixture.song);
		rig.ml.begin();
		rig.ml.thing_pressed(LearnTarget::instrument_input(*fixture.instrument), true);
		rig.ml.cc_received(cables().b, 8, 74, 100);

		expect(Learned::of(slot).describe()).to_equal(legacy.describe());
	});

	// -- `startMIDILearn`'s guard ------------------------------------------------------------------

	it("begin() refuses while another UI mode is active, exactly as `startMIDILearn` does", _ {
		// `view.cpp:473`'s `currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_MIDI_LEARN`.
		// The mode lives on the root scene, so the port takes it through `set_other_mode_active`
		// -- the `ViewState` injection pattern, reduced to its one field.
		expect(reset_legacy_learn()).to_be_true();
		currentUIMode = UI_MODE_AUDITIONING;
		view.startMIDILearn();
		const bool legacy_refused = (currentUIMode != UI_MODE_MIDI_LEARN);
		expect(legacy_refused).to_be_true();
		expect(reset_legacy_learn()).to_be_true();

		LearnRig rig;
		rig.ml.set_other_mode_active(true);
		rig.ml.begin();
		expect(rig.ml.active()).to_be_false();

		// And re-entering while ALREADY in learn is allowed and resets the held thing (`:474`),
		// which the guard's second disjunct is there for.
		rig.ml.set_other_mode_active(false);
		rig.ml.begin();
		rig.ml.thing_pressed(LearnTarget::global_command(GlobalMIDICommand::PLAY), true);
		expect(rig.ml.pressed().kind == ::MidiLearn::PLAY_BUTTON).to_be_true();
		rig.ml.set_other_mode_active(true);
		rig.ml.begin();
		expect(rig.ml.active()).to_be_true();
		expect(rig.ml.pressed().kind == ::MidiLearn::NONE).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(midi_learn_spec, midi_learn_spec_body)
