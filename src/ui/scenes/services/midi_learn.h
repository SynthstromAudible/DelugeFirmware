#pragma once

/// @file
/// @brief `MidiLearn` -- the MIDI-learn half of legacy's `View`, as a `shell::Service`.
///
/// The port of `View::startMIDILearn` (`gui/views/view.cpp`), `View::endMIDILearn`,
/// the five `*MidiLearnPadPressed` overloads, `View::endMidiLearnPressSession`,
/// `View::noteOnReceivedForMidiLearn`, `View::ccReceivedForMIDILearn`
/// and `View::midiLearnFlash`, together with the four `View::buttonAction` arms whose
/// whole body is learn work (the TAP_TEMPO learn branch, LEARN, and PLAY and RECORD under learn).
///
/// @par Why a service in `src/ui/scenes/` and not in `src/ui/shell/`
///
/// `shell::Service` is a shell interface, but `src/ui/shell/**` may not include `ui/scenes/**` or `project/**`
/// (`scripts/layers.toml`'s `shell-includes-outside` rule), and learning writes through the scenes' ports and
/// reads the song's record: the song's mapping table binds a section's launch, a clip's or kit row's mute, and a kit
/// row's or track's input, and the device keeps the PLAY, RECORD and TAP TEMPO commands. Both are written through the
/// installed learn port (`bind::LearnSink`), and the song's record (`project::Song`) is what a learn reads back. So
/// the shell's interface is implemented from inside the scenes library, which may see both, as `ModKnobs` and
/// `GlobalControls` do.
///
/// @par Where this sits in the chain, and the legacy evidence for it
///
/// `Router::run_chain` is **top scene -> `mod_knobs` -> `global_controls` -> `midi_learn`**
/// (`ui/shell/router.cpp`), and this service is deliberately last. Three separate legacy facts say
/// so, and none of them is "it was the empty slot":
///
///   1. **`View` is the fallback after the current UI in every learn offer.** `UI`'s base learn answers are `View`'s
///      learn, and `SoundEditor` answers its own (`SoundEditor::pcReceivedForMidiLearn`,
///      `SoundEditor::noteOnReceivedForMidiLearn` in `origin/main:src/deluge/gui/ui/sound_editor.cpp`). So the scene
///      gets first refusal, exactly as it does for the other two services.
///   2. **The four button arms live in the same `if`/`else if` chain as `GlobalControls`'s**, so
///      their relative order is only observable if both services could claim the same event --
///      and they cannot. `GlobalControls::view_button_action` answers `ViewArm::LearnOwns` for
///      precisely these four and suppresses its own tail for them
///      (`services/global_controls.h`'s `ViewArm`), which reproduces `View::buttonAction`'s
///      `DEALT_WITH`. Placing this service last is what keeps that abstention live rather than
///      dead code.
///   3. **LED composition requires last.** `LedCompose::compose` runs services in `Shell`'s own
///      order (the `Shell` constructor: `mod_knobs`, `global_controls`, `midi_learn`, notices,
///      screensaver) and the last writer wins. `View::midiLearnFlash`
///      *overwrites* the PLAY, RECORD and TAP_TEMPO LEDs that `PlaybackHandler::setLedStates`
///      (i.e. `GlobalControls::leds`) had just set, and
///      `View::endMIDILearn` hands them back by calling `setLedStates` again.
///      A `MidiLearn` composed *before* `GlobalControls` could never flash them.
///
/// @par The reachability rule for the four button arms is `GlobalControls`'s, and it is repeated
///      here on purpose
///
/// `View::buttonAction` is the **root view's** fallback and only the root view's:
/// `UI::buttonAction`'s base implementation is `return ActionResult::NOT_DEALT_WITH`
/// (`gui/ui/ui.h`) and does not forward to `view`, and `View::buttonAction` has exactly two
/// callers -- `TimelineView::buttonAction`'s final `else` (`gui/views/timeline_view.cpp`)
/// and `KeyboardScreen::buttonAction` (`gui/ui/keyboard/keyboard_screen.cpp`) -- which are
/// exactly the two classes `grep "public RootUI"` finds (`gui/views/timeline_view.h`,
/// `gui/ui/keyboard/keyboard_screen.h`). So `handle()` gates all four arms on
/// `stack_.input_top().is_root()`.
///
/// @warning **The gate is load-bearing here in a way it is not in `GlobalControls`, and the
///          `LEARN` button is why.** `GlobalControls` declines `LEARN` for *any* scene, root or
///          not, and its `Buttons` tail has no `LEARN` arm, so an ungated `MidiLearn` routed last
///          would toggle learn mode from inside a menu, a browser or the marker editor. Legacy
///          does something quite different there: `SoundEditor::buttonAction` has its **own**
///          `LEARN` arm (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) which sets `UI_MODE_MIDI_LEARN` only
///          after `getCurrentMenuItem()->allowsLearnMode()` and otherwise raises
///          `CANT_LEARN`. That arm is `MenuScene`'s to port -- its `learn_*` hooks
///          (`ui/scenes/menu/menu_scene.h`) are the delivery half of it and are documented as not yet
///          wired -- and it is emphatically not this service's. Removing the gate would put a
///          second, wrong owner on the same button.
///
/// @par What the scenes supply, and what this holds
///
/// Scenes report `learnable_at(pad) -> LearnTarget`, and that is the inversion this file is built on. Legacy asks the
/// same question five times, once per model type
/// (`View::sectionMidiLearnPadPressed` and friends); the port asks it once, because the *scene*
/// already knows what is under the pad and the service only needs what the press binds. See `LearnTarget`.
///
/// @par Its ports
///
/// The host installs its port set (`set_ports`) and the song's record (`set_model`). The service reads the MIDI
/// devices (`bind::MidiDevices`: which devices it knows, how a song names one, and which global commands are learned),
/// learns through `bind::LearnSink`, and saves the settings through `bind::DeviceActions` unless
/// `bind::AudioFacts::audio_busy`. With no set installed it knows no device, so it leaves every MIDI event unhandled,
/// and learns and saves nothing.
///
/// @par The flash
///
/// `View::midiLearnFlash` is a self-re-arming `kFastFlashTime` timer
/// (`TimerName::MIDI_LEARN_FLASH`, driven from `gui/ui_timer_manager.cpp`) that toggles
/// `midiLearnFlashOn`, asks the root UI to repaint its grid and writes three
/// indicator LEDs from the same phase. Here it is `tick()` + `next_timer()` + `flash_on()`, with
/// `timer_repaints()` claiming **the grid only**: every reader of `midiLearnFlashOn` in the tree
/// is a pad renderer (in `gui/views/session_view.cpp`,
/// `gui/views/instrument_clip_view.cpp`, `gui/views/arranger_view.cpp` and
/// `gui/views/view.cpp`), and arming an OLED surface that never changes is an unconditional
/// blit at 60 ms forever (see `Scene::timer_repaints`).
///
/// @note The three flashing LEDs are declared from `flash_on()` rather than as `LedState::Kind::Blink`, deliberately.
///       `led_is_on` derives a blink's phase from `now_ms` (`ui/shell/leds.h`), which would let the LEDs and the pads
///       fall out of step; legacy drives both from the one `midiLearnFlashOn` bit (`View::midiLearnFlash`) and so does
///       this. The LEARN LED is the exception and *is* a `Blink`, because legacy drives that one from a separate
///       blinker with its own phase -- `indicator_leds::blinkLed(IndicatorLED::LEARN, 255, 1)` (`View::startMIDILearn`
///       in `gui/views/view.cpp`), whose `blinkingType` 1 period is `kFastFlashTime` (`hid/led/indicator_leds.cpp`).
///
/// @warning **No wall clock.** `Service::tick(now_ms, dt_ms)` is the only place a clock arrives
///          (`shell-no-clock`), so the flash is a countdown against `dt_ms` rather than a deadline
///          computed from a `now`. `begin()` therefore takes no time argument.
///
/// @par What is deliberately NOT ported, and why
///
///   * **`View::clearMelodicInstrumentMonoExpressionIfPossible`** (`gui/views/view.cpp`), called from the
///     `INSTRUMENT_INPUT` learn commit (`View::noteOnReceivedForMidiLearn`) and from that target's delete-on-release
///     (`View::instrumentMidiLearnPadPressed`). Its body builds a `ModelStackWithParamCollection` through
///     `setupModelStackWithSong(...)->addTimelineCounter(...)->addOtherTwoThingsButNoNoteRow(...)`
///     `->addParamCollection(...)` and calls `ExpressionParamSet::clearValues(modelStack)`. No scene includes
///     `model/model_stack.h` (`scenes-model-stack`), so no scene can build this stack. Counted by
///     `unported_calls(UnportedCall::ClearMonoExpression)` rather than silently dropped, following
///     `GlobalControls::song_opens`' counters: the counter exists so a spec can prove the arm reached the point
///     legacy makes the call at, which is the only part of it this layer can assert.
///   * **`iterateAndCallSpecificDeviceHook(MIDICableUSBHosted::Hook::HOOK_ON_MIDI_LEARN)`**
///     (`View::endMidiLearnPressSession`'s tail). Counted the same way
///     (`UnportedCall::SpecificDeviceHook`) -- it is a `io/midi/cable_types/usb_hosted.h` walk
///     over the hosted-device list whose only in-tree implementation is `MIDICableUSBHosted::hookOnMIDILearn`
///     (`origin/main:src/deluge/io/midi/cable_types/usb_hosted.h`), and nothing in this layer's model reach can observe
///     whether it ran.
///   * **`playbackHandler.setLedStates()`** (`View::endMIDILearn`, `View::endMidiLearnPressSession`). Not a gap: in a
///   declarative
///     LED model those calls *are* "stop declaring the flash and let `GlobalControls` win again",
///     which happens for free on the next `leds()` pass. Recorded here so the absence is not read
///     as an omission.

#include "ui/scenes/bind/ports/learn_sink.h"
#include "ui/scenes/bind/ports/ports.h"
#include "ui/scenes/services/learn_mode.h"

#include "midi/device_id.h"
#include "project/clip_address.h"
#include "project/device_settings.h"
#include "project/mapping_table.h"
#include "project/midi_command.h"

#include "ui/widgets/core/led_frame.h"

#include "ui/shell/scene.h"
#include "ui/shell/scene_stack.h"
#include "ui/shell/service.h"

#include <cstdint>
#include <optional>

namespace deluge::project {
class Song;
} // namespace deluge::project

namespace deluge::ui::scenes::services {

/// @brief What kind of thing a press offers MIDI learn, as legacy's learn sessions tell them apart.
enum class LearnKind : uint8_t {
	NONE,             ///< nothing is offered
	CLIP,             ///< a clip's mute
	NOTEROW_MUTE,     ///< a kit row's mute
	PLAY_BUTTON,      ///< the PLAY button's global command
	RECORD_BUTTON,    ///< the RECORD button's global command
	TAP_TEMPO_BUTTON, ///< the TAP TEMPO button's global command
	SECTION,          ///< a section's launch
	INSTRUMENT_INPUT, ///< a track's input
	DRUM_INPUT,       ///< a kit row's input
};

/// @brief What a press is offering MIDI learn: what the learn binds, and what kind of thing it belongs to.
///
/// **The five overloads are one question asked five times.** `View::sectionMidiLearnPadPressed`
/// (`gui/views/view.cpp`), `clipStatusMidiLearnPadPressed`,
/// `noteRowMuteMidiLearnPadPressed`, `drumMidiLearnPadPressed` and
/// `instrumentMidiLearnPadPressed` have byte-for-byte the same shape -- press:
/// `endMidiLearnPressSession(K)`, `deleteMidiCommandOnRelease = true`, `learnedThing = &X`;
/// release, if `thingPressedForMidiLearn == K`: clear `X` if the flag is still set, then
/// `endMidiLearnPressSession()`. They differ in exactly two ways, and both are fields here: the
/// kind of thing (`kind`) and what the learn binds (`mapping`, a target in the song's mapping table).
///
/// The three transport buttons take the same shape from `View::buttonAction`
/// (`gui/views/view.cpp`) -- the same three statements over the device's global command -- so
/// they are `LearnTarget`s too, built by `global_command()`, and bind `global`.
///
/// @note A plain aggregate with named factories rather than a `std::variant`: every alternative
///       carries a kind and at most one binding, so a variant would mean five `std::get` sites to
///       say what four fields say directly. The factories are what make an ill-formed target hard
///       to build.
struct LearnTarget {
	/// @brief What kind of thing is held: the session kind `endMidiLearnPressSession`'s switch and
	///        `noteOnReceivedForMidiLearn`'s dispatch tell apart (`gui/views/view.cpp`).
	LearnKind kind = LearnKind::NONE;
	/// @brief The device's global command a note-on or CC is learned to. Set only for the three button kinds.
	std::optional<deluge::project::GlobalCommand> global;
	/// @brief What the song's mapping table binds the command to: a section, a clip's or kit row's mute, a kit
	///        row's or track's input. Nullopt for a global command and for `NONE`.
	std::optional<deluge::project::MappingTarget> mapping;
	/// @brief The section index. Set only for `SECTION`.
	uint8_t section_index = 0;

	/// @brief `View::sectionMidiLearnPadPressed`'s target (`gui/views/view.cpp`): the section's launch command, in
	///        the song's mapping table.
	/// @param section The section index.
	/// @return The target.
	static LearnTarget section(uint8_t section);
	/// @brief `View::clipStatusMidiLearnPadPressed`'s target (`gui/views/view.cpp`): the clip's mute.
	/// @param clip The clip whose status pad was pressed.
	/// @return The target.
	static LearnTarget clip(deluge::project::ClipAddress clip);
	/// @brief `View::noteRowMuteMidiLearnPadPressed`'s target (`gui/views/view.cpp`): the row's drum's mute.
	/// @param drum_id The row's drum's id, or nullopt for a row with no drum, which every call site has already
	///                refused (`gui/views/instrument_clip_view.cpp`).
	/// @return The target, or a `NONE` target for a row with no drum.
	static LearnTarget note_row_mute(std::optional<uint32_t> drum_id);
	/// @brief `View::drumMidiLearnPadPressed`'s target (`gui/views/view.cpp`): the kit row's input.
	/// @param drum_id The row's drum's id.
	/// @return The target.
	static LearnTarget drum_input(uint32_t drum_id);
	/// @brief `View::instrumentMidiLearnPadPressed`'s target (`gui/views/view.cpp`): the track's input.
	/// @param output_id The track's id.
	/// @return The target.
	static LearnTarget instrument_input(uint32_t output_id);
	/// @brief The `PLAY`/`RECORD`/`TAP_TEMPO` button targets (`View::buttonAction` in `gui/views/view.cpp`): the
	///        device's global command @p command.
	/// @param command Which global command; `PLAY`, `RECORD` or `TAP` are the three reachable here.
	/// @return The target, with `kind` the matching `PLAY_BUTTON`/`RECORD_BUTTON`/
	///         `TAP_TEMPO_BUTTON`, or a `NONE` target for any other command.
	static LearnTarget global_command(deluge::project::GlobalCommand command);
};

/// @brief MIDI learn: the mode, the pressed thing, the commit, and the flash phase.
class MidiLearn final : public deluge::ui::shell::Service, public LearnMode {
public:
	/// @brief A legacy call this port makes the decision for but cannot spell.
	///
	/// A per-site tally, exactly as `GlobalControls::SongOpen` is, and for the same reason: it
	/// is a **counter, not a mailbox**. Nothing consumes it and nothing may; it exists so a spec
	/// can assert that an arm reached the point legacy makes the call at, which is the only part
	/// of these two calls this layer can assert. See the class-doc's "not ported" list.
	///
	/// @note The doc comments are on their own lines, not trailing: `check_layer_rules.py`'s
	///       `is_comment_line` skips only a line that is *nothing but* comment, so a
	///       trailing `///<` naming a forbidden symbol would count as a real violation; do not reformat
	///       them back.
	enum class UnportedCall : uint8_t {
		/// `View::clearMelodicInstrumentMonoExpressionIfPossible` (`gui/views/view.cpp`), reached
		/// from the `INSTRUMENT_INPUT` commit and its delete-on-release.
		ClearMonoExpression,
		/// `iterateAndCallSpecificDeviceHook(HOOK_ON_MIDI_LEARN)` (`View::endMidiLearnPressSession`).
		SpecificDeviceHook,
		/// Not a site: the array bound.
		kCount,
	};

	/// @brief `kFastFlashTime` (`ui/widgets/core/metrics.h`) -- the MIDI-learn flash's half-period.
	///
	/// `View::midiLearnFlash` re-arms `TimerName::MIDI_LEARN_FLASH` with it (`gui/views/view.cpp`), and
	/// `indicator_leds::blinkLed(IndicatorLED::LEARN, 255, 1)` (`View::startMIDILearn`) picks the same number by
	/// asking for `blinkingType` 1, whose period is `kFastFlashTime`
	/// (`hid/led/indicator_leds.cpp`). Public so a spec can drive the clock by it rather
	/// than by a literal 60 that would not move if this did.
	static constexpr uint32_t kFlashMs = 60;

	/// @param stack The scene stack, read for `input_top().is_root()` -- the reachability gate the
	///              four `View::buttonAction` arms share with `GlobalControls`.
	explicit MidiLearn(deluge::ui::shell::SceneStack& stack) : stack_(stack) {}

	/// @brief The ports the service reads and learns through: the MIDI devices, the learn port, the audio facts and
	///        the device actions. The set is not owned and must outlive the service's use of it.
	/// @param ports The set, or null for none: no device is known, and nothing is learned or saved.
	void set_ports(const deluge::ui::scenes::bind::Ports* ports) { ports_ = ports; }
	/// @brief The record of the song the learns bind into, which the `INSTRUMENT_INPUT` CC learn reads the track's
	///        held input from (`View::ccReceivedForMIDILearn`). Not owned.
	/// @param model The song's record, or null: the CC learn then reads the track as holding no input.
	void set_model(const deluge::project::Song* model) { model_ = model; }

	/// @name The mode
	/// @{

	/// @brief `currentUIMode == UI_MODE_MIDI_LEARN` -- the one question `GlobalControls` asks.
	/// @return True while learn mode is engaged.
	bool active() const override { return active_; }

	/// @brief `View::startMIDILearn`.
	///
	/// Legacy's guard is `currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_MIDI_LEARN`
	/// (`View::startMIDILearn`) -- i.e. it refuses to start while any *other* mode is active. That mode lives on
	/// the root scene, which this service reaches through `set_other_mode_active()`; the default
	/// is "no other mode", which is the state of every scene ported so far.
	void begin();
	/// @brief `View::endMIDILearn`.
	///
	/// Saves the settings through `bind::DeviceActions` when a global command was learned or cleared during the
	/// session, unless `bind::AudioFacts::audio_busy` (`View::endMIDILearn`); stops the flash, and leaves the mode. The
	/// two LED calls are declarative here -- see `leds()`.
	void end();
	/// @brief Legacy's "some *other* `currentUIMode` is active", the only input to
	///        `startMIDILearn`'s guard this service cannot answer for itself.
	///
	/// The `ViewState` injection pattern `ModKnobs` and `GlobalControls` use, reduced to its one
	/// field: there is exactly one root-UI fact on this path. **Nothing sets it yet** -- the root
	/// scenes that would do not exist yet -- so the default (`false`, meaning `UI_MODE_NONE`) holds and
	/// the spec is the only caller.
	/// @param active Whether a mode other than `UI_MODE_NONE`/`UI_MODE_MIDI_LEARN` is active.
	void set_other_mode_active(bool active) { other_mode_active_ = active; }
	/// @}

	/// @name The pressed thing
	/// @{

	/// @brief The five `*MidiLearnPadPressed` overloads and the three button arms, as one call.
	///
	/// Press (@p on true) is `endMidiLearnPressSession(target.kind)`,
	/// `deleteMidiCommandOnRelease = true`, and @p target becomes the held one (legacy's `learnedThing`). Release is
	/// legacy's `else if (thingPressedForMidiLearn == K)` branch: it does nothing unless @p target names the thing
	/// currently pressed, and otherwise clears the command if nothing was learned in between and closes the session.
	///
	/// @note The two orderings legacy uses are equivalent and both are reproduced by this one
	///       body. The pad overloads run `endMidiLearnPressSession` *before* setting the flag
	///       (`gui/views/view.cpp`) and the button arms run it *after*; the function
	///       does not touch `deleteMidiCommandOnRelease`, and it replaces the held target
	///       (`View::endMidiLearnPressSession`) before either caller assigns it, so the end state is identical.
	///
	/// @param target What is under the press.
	/// @param on     Press or release.
	void thing_pressed(LearnTarget target, bool on);
	/// @brief What is currently held for learn -- legacy's `thingPressedForMidiLearn` plus
	///        `learnedThing` (`gui/views/view.h`).
	///
	/// Read by the scenes that paint a held pad brighter (`SessionView::renderRow` in `gui/views/session_view.cpp`,
	/// `InstrumentClipView::drawMuteSquare` and `InstrumentClipView::drawAuditionSquare` in
	/// `gui/views/instrument_clip_view.cpp`), which compare both the kind and what it binds.
	/// @return The target, `kind == NONE` when nothing is held.
	const LearnTarget& pressed() const { return pressed_; }
	/// @}

	/// @name The commit
	/// @{

	/// @brief `View::noteOnReceivedForMidiLearn` (`gui/views/view.cpp`).
	///
	/// Does nothing unless something is held. Clears the delete-on-release flag
	/// -- learning something is what stops the release from wiping it -- and then learns the device,
	/// channel and note as the held target's command, with the per-kind extras the arms carry: a global command's
	/// binding through `bind::LearnSink::learn_global`, naming the device only when `bind::MidiDevices` knows it; a
	/// mapping's command through `bind::LearnSink::learn`, naming the device by its song reference
	/// (`bind::MidiDevices::reference`). See the implementation for the four arms, one comment each.
	/// @param device         The device the note arrived on.
	/// @param channel_or_zone The channel, MPE zone, or `channel + project::kFirstCcChannel` when re-entered from
	///                        `cc_received`.
	/// @param note           The note number, or the CC number on that same re-entry.
	/// @param velocity       The velocity. Legacy takes it and never reads it; kept so the
	///                       signature is legacy's and a future arm has it.
	///
	/// @note **Routed, not called.** Its transport is `UiEvent::Kind::MidiNoteOn`, routed by
	///       `ShellHostUI::noteOnReceivedForMidiLearn` after the input-top scene declines it; `handle()` is its
	///       caller. Public because the spec drives it directly against legacy, and `cc_received` re-enters it.
	void note_on_received(deluge::midi::DeviceId device, int32_t channel_or_zone, int32_t note, int32_t velocity);
	/// @brief `View::ccReceivedForMIDILearn`.
	///
	/// Two arms: a MIDI-output instrument learns its input *channel* from a CC, and
	/// everything else treats a non-zero CC as a note-on with `channel + project::kFirstCcChannel`.
	/// @param device  The device the CC arrived on.
	/// @param channel The channel.
	/// @param cc      The CC number.
	/// @param value   The CC value; zero is ignored by the second arm.
	///
	/// @note Routed like `note_on_received`, as `UiEvent::Kind::MidiCc`.
	void cc_received(deluge::midi::DeviceId device, int32_t channel, int32_t cc, int32_t value);
	/// @}

	/// @name The flash
	/// @{

	/// @brief `midiLearnFlashOn` (`gui/views/view.h`) -- the phase every learnable pad reads.
	/// @return Whether the flash is currently on. Always false while `active()` is false.
	bool flash_on() const { return flash_on_; }
	/// @}

	/// @brief How many times this port reached a call it cannot spell. See `UnportedCall`.
	/// @param which The site.
	/// @return That site's count.
	uint32_t unported_calls(UnportedCall which) const;

	// -- `shell::Service` --------------------------------------------------------------------

	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	void leds(deluge::ui::widgets::core::LedFrame& frame) override;
	void tick(uint32_t now_ms, uint32_t dt_ms) override;
	std::optional<uint32_t> next_timer() const override;
	/// @brief The grid, and never the OLED. See the class doc's flash section.
	/// @return `{.oled = false, .grid = true}`.
	deluge::ui::shell::Scene::RepaintSurfaces timer_repaints() const override { return {.oled = false, .grid = true}; }

private:
	/// @brief `View::endMidiLearnPressSession` (`gui/views/view.cpp`).
	/// @param new_thing The target to become the held one; a `NONE` target closes the session.
	void end_press_session(LearnTarget new_thing);
	/// @brief The `else if (thingPressedForMidiLearn == K)` release body, shared by all eight
	///        callers (the TAP_TEMPO arm of `View::buttonAction` and each `View::*MidiLearnPadPressed`).
	void release_pressed_thing();
	/// @brief `View::midiLearnFlash`'s toggle-and-re-arm (`gui/views/view.cpp`).
	void flash_step();
	/// @brief `View::commitLearnedThing`: make @p command the held mapping target's command, through the learn port
	///        (`bind::LearnSink::learn`). Nothing for a target with no mapping.
	/// @param command The command, or nullopt to unlearn; a command that holds nothing unlearns too.
	/// @param input   A note learned on an input: what the learn takes from its device.
	void commit(std::optional<deluge::project::MidiCommand> command,
	            std::optional<deluge::ui::scenes::bind::LearnInput> input = std::nullopt);
	/// @brief Does @p command already carry a learned message? (`View::midiLearnFlash`'s `containsSomething()`
	///        test, through `bind::MidiDevices::global_command`.)
	/// @param command The command.
	/// @return Whether something is learned to it; false with no MIDI devices port.
	[[nodiscard]] bool global_command_learned(deluge::project::GlobalCommand command) const;

	deluge::ui::shell::SceneStack& stack_;
	const deluge::ui::scenes::bind::Ports* ports_ = nullptr; ///< the installed ports, or null for none
	const deluge::project::Song* model_ = nullptr;           ///< the song's record, or null for none

	/// `currentUIMode == UI_MODE_MIDI_LEARN`, and the only gate on the MIDI kinds. The guest's close does not reset
	/// it: a root scene left learning with LEARN held would swallow note-ons after a reopen until LEARN is pressed
	/// again. No root scene opens under the guest yet, so nothing reaches that.
	bool active_ = false;
	/// The injected half of `View::startMIDILearn`'s guard.
	bool other_mode_active_ = false;
	/// `thingPressedForMidiLearn` and what it binds (`learnedThing`).
	LearnTarget pressed_{};
	/// `deleteMidiCommandOnRelease` (`gui/views/view.h`).
	bool delete_on_release_ = false;
	/// `shouldSaveSettingsAfterMidiLearn` (`gui/views/view.h`).
	bool should_save_settings_ = false;
	/// `midiLearnFlashOn` (`gui/views/view.h`).
	bool flash_on_ = false;
	/// Milliseconds left of the current `kFastFlashTime` half-period, or `nullopt` when the flash
	/// timer is unset (`uiTimerManager.unsetTimer(TimerName::MIDI_LEARN_FLASH)`, `View::endMIDILearn`).
	std::optional<uint32_t> flash_remaining_ms_{};
	/// `UnportedCall`'s tallies.
	uint32_t unported_[static_cast<size_t>(UnportedCall::kCount)]{};
};

} // namespace deluge::ui::scenes::services
