#pragma once

/// @file
/// @brief `MidiLearn` -- the MIDI-learn half of legacy's `View`, as a `shell::Service`.
///
/// The port of `View::startMIDILearn` (`gui/views/view.cpp:472-480`), `View::endMIDILearn`
/// (`:482-500`), the five `*MidiLearnPadPressed` overloads (`:518-530`, `:532-544`, `:546-558`,
/// `:560-577`, `:579-595`), `View::endMidiLearnPressSession` (`:597-621`),
/// `View::noteOnReceivedForMidiLearn` (`:623-743`), `View::ccReceivedForMIDILearn` (`:767-794`)
/// and `View::midiLearnFlash` (`:796-816`), together with the four `View::buttonAction` arms whose
/// whole body is learn work (`:146-182`'s learn branch, `:185-196`, `:199-212`, `:215-228`).
///
/// @par Why a service in `src/scenes/` and not in `src/shell/`
///
/// The same reason `ModKnobs` and `GlobalControls` give: `shell::Service` is a shell interface,
/// but `src/shell/**` may not include `model/**` (`scripts/check_layer_rules.py`'s
/// `shell-includes-outside` row) and learning *writes the model* -- a `LearnedMIDI` that lives on
/// a `Song`'s section, a `Clip`, a `NoteRow`'s `Drum`, an `Instrument`, or on
/// `midiEngine.globalMIDICommands`.
///
/// @par Where this sits in the chain, and the legacy evidence for it
///
/// `Router::run_chain` is **top scene -> `mod_knobs` -> `global_controls` -> `midi_learn`**
/// (`shell/router.cpp`), and this service is deliberately last. Three separate legacy facts say
/// so, and none of them is "it was the empty slot":
///
///   1. **`View` is the fallback after the current UI in every learn dispatcher.**
///      `PlaybackHandler::noteMessageReceived` asks `getCurrentUI()->noteOnReceivedForMidiLearn`
///      first and falls through to `view.noteOnReceivedForMidiLearn`
///      (`playback/playback_handler.cpp:2968-2971`); `PlaybackHandler::midiCCReceived` gives
///      `soundEditor` first dibs and falls through to `view.ccReceivedForMIDILearn`
///      (`:3128-3136`). So the scene gets first refusal, exactly as it does for the other two
///      services.
///   2. **The four button arms live in the same `if`/`else if` chain as `GlobalControls`'s**, so
///      their relative order is only observable if both services could claim the same event --
///      and they cannot. `GlobalControls::view_button_action` answers `ViewArm::LearnOwns` for
///      precisely these four and suppresses its own tail for them
///      (`services/global_controls.h`'s `ViewArm`), which reproduces `view.cpp:469`'s
///      `DEALT_WITH`. Placing this service last is what keeps that abstention live rather than
///      dead code, and it is the seam task 4 built.
///   3. **LED composition requires last.** `LedCompose::compose` runs services in `Shell`'s own
///      order (`shell/shell.cpp:190`: `mod_knobs`, `global_controls`, `midi_learn`, notices,
///      screensaver) and the last writer wins. `View::midiLearnFlash` (`view.cpp:804-815`)
///      *overwrites* the PLAY, RECORD and TAP_TEMPO LEDs that `PlaybackHandler::setLedStates`
///      (`playback/playback_handler.cpp:2499-2512`, i.e. `GlobalControls::leds`) had just set, and
///      `View::endMIDILearn` hands them back by calling `setLedStates` again (`view.cpp:498`).
///      A `MidiLearn` composed *before* `GlobalControls` could never flash them.
///
/// @par The reachability rule for the four button arms is `GlobalControls`'s, and it is repeated
///      here on purpose
///
/// `View::buttonAction` is the **root view's** fallback and only the root view's:
/// `UI::buttonAction`'s base implementation is `return ActionResult::NOT_DEALT_WITH`
/// (`gui/ui/ui.h:98-100`) and does not forward to `view`, and `View::buttonAction` has exactly two
/// callers -- `TimelineView::buttonAction`'s final `else` (`gui/views/timeline_view.cpp:119-121`)
/// and `KeyboardScreen::buttonAction` (`gui/ui/keyboard/keyboard_screen.cpp:591`) -- which are
/// exactly the two classes `grep "public RootUI"` finds (`gui/views/timeline_view.h:27`,
/// `gui/ui/keyboard/keyboard_screen.h:35`). So `handle()` gates all four arms on
/// `stack_.input_top().is_root()`.
///
/// @warning **The gate is load-bearing here in a way it is not in `GlobalControls`, and the
///          `LEARN` button is why.** `GlobalControls` declines `LEARN` for *any* scene, root or
///          not, and its `Buttons` tail has no `LEARN` arm, so an ungated `MidiLearn` routed last
///          would toggle learn mode from inside a menu, a browser or the marker editor. Legacy
///          does something quite different there: `SoundEditor::buttonAction` has its **own**
///          `LEARN` arm (`gui/ui/sound_editor.cpp:467-499`) which sets `UI_MODE_MIDI_LEARN` only
///          after `getCurrentMenuItem()->allowsLearnMode()` (`:473`) and otherwise raises
///          `CANT_LEARN`. That arm is `MenuScene`'s to port -- its `learn_*` hooks
///          (`scenes/menu/menu_scene.h`) are the delivery half of it and are documented as not yet
///          wired -- and it is emphatically not this service's. Removing the gate would put a
///          second, wrong owner on the same button.
///
/// @par What the scenes supply, and what this holds
///
/// Spec D's service table says *scenes report `learnable_at(pad) -> LearnTarget`*, and that is the
/// inversion this file is built on. Legacy asks the same question five times, once per model type
/// (`View::sectionMidiLearnPadPressed` and friends); the port asks it once, because the *scene*
/// already knows what is under the pad and the service only ever needed the `LearnedMIDI*` plus
/// the two model pointers two of the arms use afterwards. See `LearnTarget`.
///
/// @par The flash
///
/// `View::midiLearnFlash` (`view.cpp:796-816`) is a self-re-arming `kFastFlashTime` timer
/// (`TimerName::MIDI_LEARN_FLASH`, driven from `gui/ui_timer_manager.cpp:75`) that toggles
/// `midiLearnFlashOn`, asks the root UI to repaint its grid (`:800-802`) and writes three
/// indicator LEDs from the same phase. Here it is `tick()` + `next_timer()` + `flash_on()`, with
/// `timer_repaints()` claiming **the grid only**: every reader of `midiLearnFlashOn` in the tree
/// is a pad renderer (`gui/views/session_view.cpp:1530`, `:2532`, `:3219`,
/// `gui/views/instrument_clip_view.cpp:5964`, `:6008`, `gui/views/arranger_view.cpp:660`,
/// `gui/views/view.cpp:2619`), and arming an OLED surface that never changes is an unconditional
/// blit at 60 ms forever (`Scene::timer_repaints`'s own account; shell spec §15.1, §15.3).
///
/// @note The three flashing LEDs are declared from `flash_on()` rather than as
///       `LedState::Kind::Blink`, deliberately. `led_is_on` derives a blink's phase from `now_ms`
///       (`shell/leds.h`), which would let the LEDs and the pads fall out of step; legacy drives
///       both from the one `midiLearnFlashOn` bit (`view.cpp:797`, `:806`, `:810`, `:814`) and so
///       does this. The LEARN LED is the exception and *is* a `Blink`, because legacy drives that
///       one from a separate blinker with its own phase --
///       `indicator_leds::blinkLed(IndicatorLED::LEARN, 255, 1)` (`view.cpp:478`), whose
///       `blinkingType` 1 period is `kFastFlashTime` (`hid/led/indicator_leds.cpp:81-92`).
///
/// @warning **No wall clock.** `Service::tick(now_ms, dt_ms)` is the only place a clock arrives
///          (`shell-no-clock`), so the flash is a countdown against `dt_ms` rather than a deadline
///          computed from a `now`. `begin()` therefore takes no time argument, exactly as the
///          plan's interface spells it.
///
/// @par What is deliberately NOT ported, and why
///
///   * **`View::clearMelodicInstrumentMonoExpressionIfPossible`** (`view.cpp:745-765`), called
///     from the `INSTRUMENT_INPUT` learn commit (`:728`) and from that target's delete-on-release
///     (`:589`). Its body builds a `ModelStackWithParamCollection` through
///     `setupModelStackWithSong(...)->addTimelineCounter(...)->addOtherTwoThingsButNoNoteRow(...)`
///     `->addParamCollection(...)` and calls `ExpressionParamSet::clearValues(modelStack)`.
///     `model/model_stack.h` is confined to `menu/context.cpp` and `menu/bindings.cpp`
///     (`scenes-model-stack-outside-context`), and the one seam out of that confinement,
///     `menu::model_stack_for(const MenuContext&, void*)` (`scenes/menu/context.h:212`), builds a
///     `ModelStackWithThreeMainThings` from a *menu* context -- not this stack, and not from an
///     `Instrument` alone. Counted by `unported_calls(UnportedCall::ClearMonoExpression)` rather
///     than silently dropped, following `GlobalControls::unported_opens`'s precedent: the counter
///     exists so a spec can prove the arm reached the point legacy makes the call at, which is the
///     only part of it this layer can assert.
///   * **`iterateAndCallSpecificDeviceHook(MIDICableUSBHosted::Hook::HOOK_ON_MIDI_LEARN)`**
///     (`view.cpp:620`), the tail of `endMidiLearnPressSession`. Counted the same way
///     (`UnportedCall::SpecificDeviceHook`) -- it is a `io/midi/cable_types/usb_hosted.h` walk
///     over the hosted-device list whose only in-tree implementation is
///     `MIDICableUSBHosted::hookOnMIDILearn`, and nothing in this layer's model reach can observe
///     whether it ran.
///   * **`playbackHandler.setLedStates()`** (`view.cpp:498`, `:603`). Not a gap: in a declarative
///     LED model those calls *are* "stop declaring the flash and let `GlobalControls` win again",
///     which happens for free on the next `leds()` pass. Recorded here so the absence is not read
///     as an omission.

#include "scenes/services/learn_mode.h"

#include "definitions_cxx.hpp"

#include "ui/core/led_frame.h"

#include "shell/scene.h"
#include "shell/scene_stack.h"
#include "shell/service.h"

#include <cstdint>
#include <optional>

class Clip;
class Drum;
class Instrument;
class Kit;
class LearnedMIDI;
class MIDICable;
class NoteRow;
class Song;

namespace deluge::scenes::services {

/// @brief What a press is offering MIDI learn: which `LearnedMIDI` to write, and what kind of
///        thing it belongs to.
///
/// **The five overloads are one question asked five times.** `View::sectionMidiLearnPadPressed`
/// (`view.cpp:518-530`), `clipStatusMidiLearnPadPressed` (`:532-544`),
/// `noteRowMuteMidiLearnPadPressed` (`:546-558`), `drumMidiLearnPadPressed` (`:560-577`) and
/// `instrumentMidiLearnPadPressed` (`:579-595`) have byte-for-byte the same shape -- press:
/// `endMidiLearnPressSession(K)`, `deleteMidiCommandOnRelease = true`, `learnedThing = &X`;
/// release, if `thingPressedForMidiLearn == K`: clear `X` if the flag is still set, then
/// `endMidiLearnPressSession()`. They differ in exactly three ways, and all three are fields here:
/// the `MidiLearn` enumerator (`kind`), the address of the `LearnedMIDI` (`thing`), and the two
/// model pointers the drum and instrument arms stash for the commit that may follow
/// (`view.cpp:565-567`, `:584`).
///
/// The three transport buttons take the same shape from `View::buttonAction`
/// (`view.cpp:152-156`, `:204-208`, `:220-224`) -- the same three statements over
/// `midiEngine.globalMIDICommands[...]` -- so they are `LearnTarget`s too, built by
/// `global_command()`.
///
/// @note A plain aggregate with named factories rather than a `std::variant`: every alternative
///       carries the same `LearnedMIDI*` and the extra pointers are used by exactly two arms, so a
///       variant would mean five `std::get` sites to say what four fields say directly. The
///       factories are what make an ill-formed target hard to build.
struct LearnTarget {
	/// @brief Legacy's `MidiLearn` enum (`definitions_cxx.hpp:133-143`), unchanged.
	///
	/// Reused rather than mirrored because `endMidiLearnPressSession`'s admin switch
	/// (`view.cpp:599-612`) and `noteOnReceivedForMidiLearn`'s dispatch (`:627-741`) are both
	/// written against these exact enumerators, and a parallel enum would be a table to drift.
	::MidiLearn kind = ::MidiLearn::NONE;
	/// @brief The `LearnedMIDI` a note-on or CC commits into -- legacy's `learnedThing`
	///        (`gui/views/view.h:113`). Null exactly when `kind` is `NONE`.
	::LearnedMIDI* thing = nullptr;
	/// @brief `instrumentPressedForMIDILearn` (`view.cpp:584`). Set only for `INSTRUMENT_INPUT`.
	::Instrument* instrument = nullptr;
	/// @brief `drumPressedForMIDILearn` (`view.cpp:565`). Set only for `DRUM_INPUT`.
	::Drum* drum = nullptr;
	/// @brief `kitPressedForMIDILearn` (`view.cpp:566`). Set only for `DRUM_INPUT`; legacy keeps it
	///        purely so `grabVelocityToLevel…ForDrum` can search faster (`:566-567`).
	::Kit* kit = nullptr;

	/// @brief `View::sectionMidiLearnPadPressed`'s target (`view.cpp:520-522`):
	///        `&currentSong->sections[section].launchMIDICommand`.
	/// @param song    The song the section belongs to -- the scene's, since `scenes-currentSong`
	///                forbids the global.
	/// @param section The section index.
	/// @return The target.
	static LearnTarget section(::Song& song, uint8_t section);
	/// @brief `View::clipStatusMidiLearnPadPressed`'s target (`view.cpp:534-536`):
	///        `&whichClip->muteMIDICommand`.
	/// @param clip The clip whose status pad was pressed.
	/// @return The target.
	static LearnTarget clip(::Clip& clip);
	/// @brief `View::noteRowMuteMidiLearnPadPressed`'s target (`view.cpp:548-550`):
	///        `&whichNoteRow->drum->muteMIDICommand`.
	/// @param note_row The note row, whose `drum` legacy dereferences unguarded at `:550` -- every
	///                 call site has already checked it (`gui/views/instrument_clip_view.cpp:2253`).
	/// @return The target, or a `NONE` target if the row has no drum.
	static LearnTarget note_row_mute(::NoteRow& note_row);
	/// @brief `View::drumMidiLearnPadPressed`'s target (`view.cpp:562-567`): `&drum->midiInput`,
	///        plus the drum and its kit.
	/// @param drum The drum under the pad.
	/// @param kit  The kit it belongs to.
	/// @return The target.
	static LearnTarget drum_input(::Drum& drum, ::Kit& kit);
	/// @brief `View::instrumentMidiLearnPadPressed`'s target (`view.cpp:581-584`):
	///        `&instrument->midiInput`, plus the instrument.
	/// @param instrument The instrument under the pad.
	/// @return The target.
	static LearnTarget instrument_input(::Instrument& instrument);
	/// @brief The `PLAY`/`RECORD`/`TAP_TEMPO` button targets (`view.cpp:155`, `:207`, `:223`):
	///        `&midiEngine.globalMIDICommands[to_underlying(command)]`.
	/// @param command Which global command; `PLAY`, `RECORD` or `TAP` are the three reachable here.
	/// @return The target, with `kind` the matching `PLAY_BUTTON`/`RECORD_BUTTON`/
	///         `TAP_TEMPO_BUTTON`, or a `NONE` target for any other command.
	static LearnTarget global_command(GlobalMIDICommand command);
};

/// @brief MIDI learn: the mode, the pressed thing, the commit, and the flash phase.
class MidiLearn final : public deluge::ui::shell::Service, public LearnMode {
public:
	/// @brief A legacy call this port makes the decision for but cannot spell.
	///
	/// A per-site tally, exactly as `GlobalControls::UnportedOpen` is, and for the same reason: it
	/// is a **counter, not a mailbox**. Nothing consumes it and nothing may; it exists so a spec
	/// can assert that an arm reached the point legacy makes the call at, which is the only part
	/// of these two calls this layer can assert. See the class-doc's "not ported" list.
	///
	/// @note The doc comments are on their own lines, not trailing: `check_layer_rules.py`'s
	///       `is_comment_line` (`:490-511`) skips only a line that is *nothing but* comment, so a
	///       trailing `///<` naming a forbidden symbol would count as a real violation. Task 4 hit
	///       this exact thing; do not reformat them back.
	enum class UnportedCall : uint8_t {
		/// `View::clearMelodicInstrumentMonoExpressionIfPossible` (`view.cpp:745-765`), reached
		/// from the `INSTRUMENT_INPUT` commit (`:728`) and its delete-on-release (`:589`).
		ClearMonoExpression,
		/// `iterateAndCallSpecificDeviceHook(HOOK_ON_MIDI_LEARN)` (`view.cpp:620`).
		SpecificDeviceHook,
		/// Not a site: the array bound.
		kCount,
	};

	/// @brief `kFastFlashTime` (`definitions_cxx.hpp:160`) -- the MIDI-learn flash's half-period.
	///
	/// `View::midiLearnFlash` re-arms `TimerName::MIDI_LEARN_FLASH` with it (`view.cpp:798`), and
	/// `indicator_leds::blinkLed(IndicatorLED::LEARN, 255, 1)` (`:478`) picks the same number by
	/// asking for `blinkingType` 1, whose period is `kFastFlashTime`
	/// (`hid/led/indicator_leds.cpp:81-83`). Public so a spec can drive the clock by it rather
	/// than by a literal 60 that would not move if this did.
	static constexpr uint32_t kFlashMs = 60;

	/// @param stack The scene stack, read for `input_top().is_root()` -- the reachability gate the
	///              four `View::buttonAction` arms share with `GlobalControls`.
	explicit MidiLearn(deluge::ui::shell::SceneStack& stack) : stack_(stack) {}

	/// @brief Which song the section targets resolve against -- legacy's `currentSong`, which
	///        `scenes-currentSong` forbids this layer.
	///
	/// Read by the `DRUM_INPUT` and `INSTRUMENT_INPUT` commits
	/// (`view.cpp:644`, `:656-657`, `:681`, `:736-737`) and nothing else; a null song makes those
	/// arms skip the parts that need it, and the three-field write still happens.
	/// @param song The song, or null.
	void set_song(::Song* song) { song_ = song; }

	/// @name The mode
	/// @{

	/// @brief `currentUIMode == UI_MODE_MIDI_LEARN` -- the one question `GlobalControls` asks.
	/// @return True while learn mode is engaged.
	bool active() const override { return active_; }

	/// @brief `View::startMIDILearn` (`view.cpp:472-480`).
	///
	/// Legacy's guard is `currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_MIDI_LEARN`
	/// (`:473`) -- i.e. it refuses to start while any *other* mode is active. That mode lives on
	/// the root scene, which this service reaches through `set_other_mode_active()`; the default
	/// is "no other mode", which is the state of every scene ported so far.
	void begin();
	/// @brief `View::endMIDILearn` (`view.cpp:482-500`).
	///
	/// Writes settings to flash when a global command was learned or cleared during the session
	/// (`:483-487`), stops the flash (`:488-489`), and leaves the mode
	/// (`:494-496`). The two LED calls (`:498-499`) are declarative here -- see `leds()`.
	void end();
	/// @brief Legacy's "some *other* `currentUIMode` is active", the only input to
	///        `startMIDILearn`'s guard this service cannot answer for itself.
	///
	/// The `ViewState` injection pattern `ModKnobs` and `GlobalControls` use, reduced to its one
	/// field: there is exactly one root-UI fact on this path. **Nothing sets it yet** -- the root
	/// scenes that would are E5/E6's -- so the default (`false`, meaning `UI_MODE_NONE`) holds and
	/// the spec is the only caller.
	/// @param active Whether a mode other than `UI_MODE_NONE`/`UI_MODE_MIDI_LEARN` is active.
	void set_other_mode_active(bool active) { other_mode_active_ = active; }
	/// @}

	/// @name The pressed thing
	/// @{

	/// @brief The five `*MidiLearnPadPressed` overloads and the three button arms, as one call.
	///
	/// Press (@p on true) is `endMidiLearnPressSession(target.kind)`,
	/// `deleteMidiCommandOnRelease = true`, `learnedThing = target.thing`. Release is legacy's
	/// `else if (thingPressedForMidiLearn == K)` branch: it does nothing unless @p target names
	/// the thing currently pressed, and otherwise clears the command if nothing was learned in
	/// between and closes the session.
	///
	/// @note The two orderings legacy uses are equivalent and both are reproduced by this one
	///       body. The pad overloads run `endMidiLearnPressSession` *before* setting the flag
	///       (`view.cpp:520-521`) and the button arms run it *after* (`:153-154`); the function
	///       does not touch `deleteMidiCommandOnRelease`, and it nulls `learnedThing`
	///       (`:614`) before either caller assigns it, so the end state is identical.
	///
	/// @param target What is under the press.
	/// @param on     Press or release.
	void thing_pressed(LearnTarget target, bool on);
	/// @brief What is currently held for learn -- legacy's `thingPressedForMidiLearn` plus
	///        `learnedThing` (`gui/views/view.h:108`, `:113`).
	///
	/// Read by the scenes that paint a held pad brighter (`gui/views/session_view.cpp:2913-2925`,
	/// `gui/views/instrument_clip_view.cpp:6027-6031`), which compare both the kind and the
	/// `LearnedMIDI` address.
	/// @return The target, `kind == NONE` when nothing is held.
	const LearnTarget& pressed() const { return pressed_; }
	/// @}

	/// @name The commit
	/// @{

	/// @brief `View::noteOnReceivedForMidiLearn` (`view.cpp:623-743`).
	///
	/// Does nothing unless something is held (`:624`). Clears the delete-on-release flag
	/// (`:625`) -- learning something is what stops the release from wiping it -- and then writes
	/// `cable`/`channelOrZone`/`noteOrCC` into the held `LearnedMIDI`, with the per-kind extras
	/// the arms carry. See the implementation for the four arms, one comment each.
	/// @param cable          The cable the note arrived on.
	/// @param channel_or_zone The channel, MPE zone, or `channel + IS_A_CC` when re-entered from
	///                        `cc_received`.
	/// @param note           The note number, or the CC number on that same re-entry.
	/// @param velocity       The velocity. Legacy takes it and never reads it; kept so the
	///                       signature is legacy's and a future arm has it.
	void note_on_received(::MIDICable& cable, int32_t channel_or_zone, int32_t note, int32_t velocity);
	/// @brief `View::ccReceivedForMIDILearn` (`view.cpp:767-794`).
	///
	/// Two arms: a MIDI-output instrument learns its input *channel* from a CC (`:772-783`), and
	/// everything else treats a non-zero CC as a note-on with `channel + IS_A_CC`
	/// (`:788-791`).
	/// @param cable   The cable.
	/// @param channel The channel.
	/// @param cc      The CC number.
	/// @param value   The CC value; zero is ignored by the second arm (`:789`).
	void cc_received(::MIDICable& cable, int32_t channel, int32_t cc, int32_t value);
	/// @}

	/// @name The flash
	/// @{

	/// @brief `midiLearnFlashOn` (`gui/views/view.h:110`) -- the phase every learnable pad reads.
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
	void leds(deluge::ui::LedFrame& frame) override;
	void tick(uint32_t now_ms, uint32_t dt_ms) override;
	std::optional<uint32_t> next_timer() const override;
	/// @brief The grid, and never the OLED. See the class doc's flash section.
	/// @return `{.oled = false, .grid = true}`.
	deluge::ui::shell::Scene::RepaintSurfaces timer_repaints() const override { return {.oled = false, .grid = true}; }

private:
	/// @brief `View::endMidiLearnPressSession` (`view.cpp:597-621`).
	/// @param new_thing The target to become the held one; a `NONE` target closes the session.
	void end_press_session(LearnTarget new_thing);
	/// @brief The `else if (thingPressedForMidiLearn == K)` release body, shared by all eight
	///        callers (`view.cpp:160-164`, `:525-528`, `:539-542`, `:553-556`, `:571-575`,
	///        `:588-593`).
	void release_pressed_thing();
	/// @brief `View::midiLearnFlash`'s toggle-and-re-arm (`view.cpp:797-798`).
	void flash_step();
	/// @brief The port of `noteOnReceivedForMidiLearn`'s `DRUM_INPUT` arm (`view.cpp:629-661`).
	void commit_drum_extras(::MIDICable& cable, int32_t channel_or_zone);
	/// @brief The port of its `INSTRUMENT_INPUT` arm (`view.cpp:677-740`).
	void commit_instrument_extras(::MIDICable& cable, int32_t channel_or_zone);
	/// @brief `getCurrentInstrument()` (`model/song/song.cpp:120-133`), resolved from `song_`.
	///
	/// Every `getCurrent*()` free function in `model/song/song.h` is
	/// `currentSong->getCurrentClip()->…` and nothing else, so resolving them from this service's
	/// own `song_` is the same read spelled without the global `scenes-currentSong` forbids -- not
	/// a substitution. Null when there is no song, no current clip, or an audio/none output.
	/// @return The current clip's instrument, or null.
	::Instrument* current_instrument() const;

	deluge::ui::shell::SceneStack& stack_;
	::Song* song_ = nullptr;

	/// `currentUIMode == UI_MODE_MIDI_LEARN`.
	bool active_ = false;
	/// The injected half of `startMIDILearn`'s guard (`view.cpp:473`).
	bool other_mode_active_ = false;
	/// `thingPressedForMidiLearn` + `learnedThing` + the two stashed model pointers.
	LearnTarget pressed_{};
	/// `deleteMidiCommandOnRelease` (`gui/views/view.h:109`).
	bool delete_on_release_ = false;
	/// `shouldSaveSettingsAfterMidiLearn` (`gui/views/view.h:111`).
	bool should_save_settings_ = false;
	/// `midiLearnFlashOn` (`gui/views/view.h:110`).
	bool flash_on_ = false;
	/// Milliseconds left of the current `kFastFlashTime` half-period, or `nullopt` when the flash
	/// timer is unset (`uiTimerManager.unsetTimer(TimerName::MIDI_LEARN_FLASH)`, `view.cpp:488`).
	std::optional<uint32_t> flash_remaining_ms_{};
	/// `UnportedCall`'s tallies.
	uint32_t unported_[static_cast<size_t>(UnportedCall::kCount)]{};
};

} // namespace deluge::scenes::services
