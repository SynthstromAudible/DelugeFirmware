#pragma once
#include "notify/dirty_set.h"
#include "shell/fixed_vector.h"
#include "shell/profile.h"
#include "ui/core/frame_context.h"
#include "ui/core/input.h"
#include "ui/core/led_frame.h"
#include "ui/grid/effects.h"
#include "ui/grid/frame.h"
#include "ui/oled/frame.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

/// @brief The MIDI cable a `UiEvent::Kind::MidiNoteOn`/`MidiCc` arrived on -- declared, never
///        defined, in this layer.
///
/// `MIDICable` is `io/midi/midi_device.h`'s, and `check_layer_rules.py`'s `shell-includes-outside`
/// row keeps `io/` (like `model/`, `gui/` and `hid/`) out of `src/shell/**`. A forward declaration
/// is not an include and asks nothing of the type: `UiEvent` stores the pointer and the shell hands
/// it on, and only `scenes::services::MidiLearn` -- which is in `src/scenes/`, the first layer
/// allowed to reach the model -- ever dereferences it. Declaring the real type rather than an
/// opaque `void*` is what keeps the hand-off type-checked across the seam.
class MIDICable;

namespace deluge::ui::shell {

/// @brief Lowers hardware input into the shell's own vocabulary and tracks per-button press
///        bookkeeping.
///
/// Only forward-declared here: `UiContext` holds a pointer to one so a scene can ask whether a
/// press was already consumed, and nothing in this header needs the definition.
class InputLowering;

/// @brief Which scene a `SceneId` names -- the registry index, and the `SceneState` cache key.
///
/// `Null` through `Screensaver` are reserved for the shell's own scenes; application scenes number
/// from `FirstApp` upwards, leaving room for shell scenes to be added without renumbering anything
/// an application already registered. Every value must stay below `kMaxSceneKinds`.
enum class SceneKind : uint8_t {
	Null = 0,      ///< The scene `SceneStack::top()` reports when the stack is empty; swallows input.
	Popup,         ///< A transient message over whatever is beneath.
	Console,       ///< The scrolling log surface.
	Working,       ///< The "busy, please wait" indicator.
	Loading,       ///< Shown while a song is being read.
	Screensaver,   ///< The idle animation.
	FirstApp = 16, ///< The first kind an application may register; application kinds run upwards.
};

/// @brief How many `SceneKind` values exist: the size of the registry and of the state-slot array.
inline constexpr uint8_t kMaxSceneKinds = 48;
/// @brief The deepest the scene stack can go.
///
/// The legacy UI's deepest path is five (root -> clip minder -> sound editor -> context menu ->
/// rename); eight leaves headroom without making the state-slot array any larger.
inline constexpr uint8_t kStackDepth = 8;
/// @brief Bytes of saved state per `SceneKind`.
inline constexpr size_t kSceneStateBytes = 256;

/// @brief What to navigate to: a `SceneKind` plus the one argument that distinguishes instances of
///        it.
///
/// POD, comparable, and small enough to pass by value. `arg` is interpreted by the scene the
/// `kind` names -- a clip index, an output index, a browser kind -- and is handed to the scene as
/// `on_enter`'s parameter. It is deliberately *not* part of the `SceneState` cache key: state is
/// saved per `SceneKind`, so re-entering the same kind with a different `arg` restores the kind's
/// last saved state and then lets `on_enter` re-target it.
struct SceneId {
	SceneKind kind = SceneKind::Null;
	uint16_t arg = 0;
	bool operator==(const SceneId&) const = default;
};

/// @brief One scene's saved state: an opaque, fixed-size, 8-aligned byte block.
///
/// The shell never looks inside. A scene writes whatever POD it wants to survive a `replace_root`
/// in `save_state` and reads it back in `restore_state`; the alignment lets a scene `memcpy` a
/// struct containing 64-bit members in and out without a misaligned access.
///
/// @warning There is one slot per `SceneKind`, not one per `SceneId`, and the block is not
///          versioned -- a scene that changes its state layout must not expect to read an older
///          layout back. Slots live only as long as the `SceneStack`, so nothing persists across a
///          reboot.
struct SceneState {
	alignas(8) uint8_t bytes[kSceneStateBytes];
};
static_assert(sizeof(SceneState) == kSceneStateBytes);
static_assert(alignof(SceneState) == 8);

/// @brief One lowered input event, as a scene sees it.
///
/// A tagged struct rather than a variant: it is copied into every `handle()` call on the way down
/// the stack, so it stays POD, trivially copyable and small. `kind` says which fields carry
/// meaning; the rest keep their zero values and must not be read.
///
/// | `kind` | valid fields |
/// |---|---|
/// | `Pad` | `x`, `y` (absolute pad coordinates, sidebar included), `velocity` (0 on release), `on` |
/// | `Button` | `button`, `on` |
/// | `Encoder` | `encoder`, `delta` (signed detents, or an accelerated magnitude per `EncoderKind`) |
/// | `EncoderButton` | `button` (the encoder's own `*Enc` id), `on` |
/// | `AllReleased` | none -- the edge where the last held button and pad went up |
/// | `MidiNoteOn` | `cable`, `channel` (channel or MPE zone), `data1` (note), `data2` (velocity) |
/// | `MidiCc` | `cable`, `channel`, `data1` (CC number), `data2` (CC value) |
///
/// @note `AllReleased` is an edge, not a level: it fires once when the held set empties, and
///       `UiContext::held`/`held_pads` are the level a scene should test instead.
///
/// @par The two MIDI kinds, and why they are `UiEvent`s at all
///
/// They are the transport migration §10.2 item 8 was opened for: `services::MidiLearn`'s
/// `note_on_received` and `cc_received` are ports of `View::noteOnReceivedForMidiLearn`
/// (`gui/views/view.cpp:640`) and `View::ccReceivedForMIDILearn` (`:784`), and until F1 task 10
/// there was no event shape a `Router` could carry them in, so neither had a caller anywhere in
/// `src/`. Routing them rather than calling the service directly is what puts the **scene** first:
/// legacy's note-on dispatcher asks `getCurrentUI()->noteOnReceivedForMidiLearn` before it falls
/// through to `view` (`playback/playback_handler.cpp:2968-2971`), and `Router::run_chain`'s
/// scene-then-`midi_learn` order is the same shape -- which is what a future `SessionScene` /
/// `NoteClipScene` needs when it answers `learnable_at(pad)` and claims the message itself.
///
/// Unlike every other kind these are **not** produced by `InputLowering`: they arrive from
/// legacy's MIDI dispatchers, synchronously, through `Shell::route_midi`.
///
/// @note `cable` is the one pointer in this struct and the only thing here the shell layer cannot
///       name a definition for -- `MIDICable` lives in `io/midi/`, which
///       `check_layer_rules.py`'s `shell-includes-outside` row keeps out of `src/shell/**`. It is
///       forward-declared above and **never dereferenced in this layer**: only
///       `services::MidiLearn`, which is in `src/scenes/` and may reach the model, ever reads
///       through it. A null `cable` is what a `MidiNoteOn`/`MidiCc` built without one carries, and
///       every consumer must treat it as "no message" rather than dereferencing it.
struct UiEvent {
	/// @brief Which shape this event has; see the table above.
	enum class Kind : uint8_t { Pad, Button, Encoder, EncoderButton, AllReleased, MidiNoteOn, MidiCc };

	Kind kind;
	uint8_t x = 0;                    ///< `Pad`: column, absolute.
	uint8_t y = 0;                    ///< `Pad`: row, absolute.
	uint8_t velocity = 0;             ///< `Pad`: strike velocity; 0 means release.
	bool on = false;                  ///< `Pad`/`Button`/`EncoderButton`: true on press, false on release.
	ButtonId button = ButtonId::None; ///< `Button`/`EncoderButton`: which button.
	EncoderRole encoder{};            ///< `Encoder`: which encoder turned.
	int8_t delta = 0;                 ///< `Encoder`: signed turn magnitude.
	/// `MidiNoteOn`/`MidiCc`: channel, or the MPE zone `MIDIPort::channelToZone` mapped it to.
	/// Never a `+ IS_A_CC`/`+ IS_A_PC` composite -- that encoding is `LearnedMIDI`'s and the
	/// service applies it (`services::MidiLearn::cc_received`), so this field stays the raw
	/// dispatcher argument.
	uint8_t channel = 0;
	/// `MidiNoteOn`: the note number. `MidiCc`: the CC number.
	uint8_t data1 = 0;
	/// `MidiNoteOn`: the velocity. `MidiCc`: the CC value.
	uint8_t data2 = 0;
	/// `MidiNoteOn`/`MidiCc`: the cable the message arrived on; null means "no message". Opaque
	/// here -- see the note above.
	::MIDICable* cable = nullptr;
};

/// @brief Everything a scene may ask about the world *around* one `UiEvent`.
///
/// An aggregate of borrowed references built on the stack once per delivered event. It owns
/// nothing and outlives nothing.
struct UiContext {
	const ButtonSet& held;       ///< Buttons down right now, this event included.
	const PadMask& held_pads;    ///< Pads down right now, absolute coordinates.
	bool shift;                  ///< Is shift held (latched or physical)?
	bool sticky;                 ///< Is the shift latch engaged rather than the physical hold?
	uint32_t now_ms;             ///< The tick's wall clock.
	const BoardProfile& profile; ///< The board this event came from.
	/// The lowering that produced the event, or null in a context built without one (a spec, or a
	/// synthesised event). Null makes `press_used_up` false and `held_since` zero.
	const InputLowering* lowering = nullptr;

	/// @brief Has \p b's current press already been consumed by something else?
	///
	/// The legacy `Buttons::ignoreCurrentPress` bit: a scene that acted on a *hold* marks the
	/// press used up so the release does not also act.
	/// @param b The button to ask about.
	/// @return True if the press was consumed; false when `lowering` is null.
	/// @note Defined alongside `InputLowering`, not in this header.
	bool press_used_up(ButtonId b) const;
	/// @brief When did \p b's current press begin?
	/// @param b The button to ask about.
	/// @return The `now_ms` of the press, or 0 if `b` is not held or `lowering` is null.
	/// @note Defined alongside `InputLowering`, not in this header.
	uint32_t held_since(ButtonId b) const;
};

/// @brief One thing the shell should do as a consequence of a scene's `handle()` or
///        `after_render()`.
///
/// A scene never touches the stack itself: it describes what should happen and the shell's router
/// applies it, so navigation is testable without a shell and one scene cannot re-enter another
/// mid-event. `kind` says which fields carry meaning.
///
/// | `kind` | valid fields | effect |
/// |---|---|---|
/// | `Push` | `id` | `SceneStack::push(id)` |
/// | `Replace` | `id` | `SceneStack::replace_top(id)` -- the sideways move, **not** the root |
/// | `ReplaceRoot` | `id` | `SceneStack::replace_root(id)` |
/// | `Pop` | `pop_to` | `SceneStack::pop(pop_to)`; `SceneKind::Null` means "just the top" |
/// | `Remove` | `pop_to` | `SceneStack::remove(pop_to)` -- excise that one scene, wherever it sits, and leave
/// everything else (above and below it) exactly where it was |
/// | `Popup` | `popup_string`, `popup_type`, `surface` | show a message, resolved through `BoardProfile::text` |
/// | `Repaint` | `surface` | invalidate that surface for the next tick; `SurfaceRole::None` is a no-op ("no surface,
/// never matches", `surfaces.h`) | | `RepaintAll` | none | invalidate every surface for the next tick, **the grid
/// included** (`Router::grid_repaint_requested()` -- and that is the only way to reach the grid, since `SurfaceRole`
/// names OLED panels only) | | `LockInput` |
/// `lock` | take or release the input lock (the stem-export / song-load gate -- read the warning below
/// before using it) |
///
/// @warning **`LockInput` is not scene-scoped, and a scene cannot release it from `on_exit`.**
///          It sets one bare global bool (`InputLowering::set_input_lock`, `input_lowering.h`) with
///          no owner and no refcount, and while it is set every event except
///          `DELUGE_EVENT_NO_PRESSES` is **dropped** -- encoder edges included
///          (`input_lowering.cpp:159`). `on_exit()` returns `void`, so a scene that took the lock
///          and is then removed by anything other than its own `Result` -- an external `Pop`, a
///          `replace_root`, a song load -- physically cannot give it back, and the whole device
///          goes deaf until something else happens to clear it.
///
///          So a scene may only take it across a window it *itself* closes, and even then it is
///          rarely the right tool: it is the stem-export / song-load bracket
///          (`Shell::song_loading` is its only other raiser, and `Shell::describe` records it as
///          "one lock, one reason"), not legacy's `waitingForSDRoutineToEnd`. That gate **defers
///          and retries** where this one drops, and its port is `InputLowering::set_sd_busy`,
///          which deliberately has no `Action` behind it. A scene that merely wants to ignore
///          input for a while should just ignore it: `SceneStack::input_top()` routes every event
///          to the topmost non-input-transparent scene's own `handle()`, which is free to swallow
///          them (`scenes/browser/browser_scene.cpp`'s `after_render` note records that choice and
///          why). E3 task 3 fix round 1: `BrowserScene` took this lock while a directory listing
///          was in flight and leaked it on exactly the external-removal path above.
///
/// @note `Replace` is deliberately the short name for the common case (swap the top) and
///       `ReplaceRoot` the long one for the rarer whole-stack move, matching the pair of
///       `SceneStack` methods rather than the pair of legacy function names.
///
/// @note **`Pop` with `pop_to == SceneKind::Null` is a blind "remove whatever is currently on
///       top"** -- it does not know or care which scene asked for it. That is exactly right for a
///       scene that really does expect to be the top (the ordinary case), but it is a structural
///       hazard for any scene that wants to close *itself specifically*: a `Notices`-driven overlay
///       (`Popup`/`Working`/`Console`) can legitimately be pushed above it between the render that
///       produced this `Result` and the router applying it, and a blind `Pop` would then remove the
///       overlay instead. `Remove` (with `pop_to` naming the caller's own kind) is what "close me,
///       leave whatever else is stacked alone" actually means -- see `MenuScene::handle`'s
///       root-`Back` case for the ported example (task 11 review round 1: it originally used
///       `Pop{.pop_to = FirstApp}`, which is wrong for the same reason `Pop{.pop_to = Null}` is --
///       `pop(kind)` still takes everything ABOVE that kind down with it, tearing down a live
///       `Working`/`Popup` along with the menu it was shown over).
struct Action {
	/// @brief Which action this is; see the table above.
	enum class Kind : uint8_t { Push, Replace, ReplaceRoot, Pop, Remove, Popup, Repaint, RepaintAll, LockInput };

	Kind kind;
	SceneId id{}; ///< `Push`/`Replace`/`ReplaceRoot`: the target.
	SceneKind pop_to =
	    SceneKind::Null; ///< `Pop`: pop down to and including this kind. `Remove`: the one kind to excise.
	/// `Popup`/`Repaint`: which surface. For `Repaint`, `SurfaceRole::None` is a no-op --
	/// it keeps the "no surface, never matches" meaning `surfaces.h` gives that enumerator
	/// everywhere else in the shell, rather than being overloaded here as an "all surfaces"
	/// sentinel; use `RepaintAll` for that.
	SurfaceRole surface = SurfaceRole::Main;
	bool lock = false;         ///< `LockInput`: true to take the lock, false to release.
	uint16_t popup_string = 0; ///< `Popup`: the string id to show.
	/// `Popup`: which flavour of popup, as `notify::Notice::popup_type` spells it -- the legacy
	/// `PopupType` value as a bare `uint8_t`, so this header does not have to reach into
	/// `hid/display`. 0 means "unspecified" and is mapped to the general-purpose popup, never to
	/// "no popup"; the popup service is what interprets the rest (how long it lingers, and what
	/// `hasPopupOfType`-style gating it participates in).
	uint8_t popup_type = 0;
};

/// @brief What one `handle()` or `after_render()` call decided.
///
/// `handled` stops the event travelling further down the stack; `actions` is what the shell should
/// do about it, applied in order. The two are independent: a scene may handle an event and ask for
/// nothing, or ask for a repaint while letting the event fall through.
///
/// Eight actions is well past what any single event produces (the busiest legacy path is a pop
/// plus a push plus a popup); a ninth is dropped by `FixedVector::push_back` rather than growing.
struct Result {
	bool handled = false;
	FixedVector<Action, 8> actions;

	/// @brief The "I did nothing with this" result: not handled, no actions.
	static Result unhandled() { return {}; }
};

/// @brief How much of the grid a scene consumes while it is the input top -- `Scene::grid_pad_claim()`'s
///        return type.
///
/// Declared at namespace scope, not nested in `Scene`, so a scene in another namespace (every
/// `MenuScene` override included) can name it as `deluge::ui::shell::GridPadClaim` rather than
/// reaching through `Scene::`.
///
/// The three values are legacy's three behaviours, not a design: `SoundEditor::padAction`
/// (`gui/ui/sound_editor.cpp:1460`) tries its shortcut arm only when the menu is not a settings or
/// note-editor menu (`:1467`), and forwards everything it did not claim to the root view by
/// identity (`:1474-1517`).
enum class GridPadClaim : uint8_t {
	/// The scene owns the grid; nothing is forwarded. Every root scene, every browser.
	All,
	/// The scene owns only the shortcut gesture -- a pressed main-grid pad with shift held.
	/// Everything else forwards. The port of `:1467` passing.
	ShortcutsOnly,
	/// The scene owns no grid pad. The port of `:1467` failing.
	None,
};

/// @brief One screen's worth of behaviour: what it draws, what it does with input, and what it
///        remembers.
///
/// Scenes are long-lived singletons owned by the application and registered once in a
/// `SceneRegistry`; the `SceneStack` only ever holds pointers to them, so a scene is entered and
/// exited many times over its life and must keep its own state across that. Every hook except
/// `kind()` and `handle()` has a do-nothing default, so a minimal scene is two overrides.
///
/// @warning A scene must never mutate the stack. It returns `Action`s and the shell applies them
///          after the call returns, which is what keeps a scene from being destroyed or re-entered
///          in the middle of its own method.
class Scene {
public:
	virtual ~Scene() = default;

	/// @brief The kind this scene is registered under.
	/// @return The registry index and state-slot key; must match the slot the registry holds it in.
	virtual SceneKind kind() const = 0;

	/// @name Rendering
	/// Called once per surface per tick, for each surface the scene is not transparent for. A
	/// scene that paints only one surface kind leaves the other default.
	/// @{
	/// @brief Draw this scene's contents on one OLED surface.
	/// @param role  Which surface is being drawn (`Main` on a single-panel board).
	/// @param frame The root frame for that surface, already clipped to its visible region.
	virtual void render_oled(SurfaceRole role, ui::oled::OledFrame& frame) {}
	/// @brief Draw this scene's contents on the pad grid.
	/// @param frame The root grid frame, spanning the main pads and the sidebar in one coordinate
	///              space.
	virtual void render_grid(ui::grid::GridFrame& frame) {}
	/// @brief Declare this scene's LED and knob-indicator states for the tick.
	/// @param frame The tick's LED declarations, to be composed and flushed by the shell.
	virtual void leds(LedFrame& frame) {}
	/// @}

	/// @brief Navigation derived from what this tick's render observed.
	///
	/// A widget claims its inputs while it is being drawn and hands back a `Response`; a scene
	/// stashes that in its own state during `render_*` and turns it into navigation here, rather
	/// than re-deriving it from the raw event in `handle()`. Runs once per tick per scene that was
	/// rendered, after every surface for that tick; its `Result` is applied exactly as a
	/// `handle()` result is.
	/// @return What the shell should do about this tick's render.
	virtual Result after_render() { return {}; }

	/// @brief React to one lowered input event.
	///
	/// The only pure-virtual hook besides `kind()`: a scene that wants nothing from input returns
	/// `Result::unhandled()` explicitly, so "swallows everything" is never the accident of a
	/// missing override.
	///
	/// @note This does not see an input a widget in this scene's frame claimed while rendering --
	///       the shell delivers only what nothing owned.
	/// @param event The event.
	/// @param ctx   The world around it.
	/// @return Whether the event was handled, and what should happen next.
	virtual Result handle(const UiEvent& event, const UiContext& ctx) = 0;

	/// @brief Advance any time-based state.
	/// @param dt_ms Milliseconds since this scene was last ticked.
	virtual void tick(uint32_t dt_ms) {}

	/// @name Lifecycle
	/// @{
	/// @brief Called when this scene is pushed or otherwise becomes the top.
	/// @param id The id it was entered with; `id.arg` is the scene's instance argument.
	/// @return False to refuse the entry, which rolls the push back and resumes the previous top.
	virtual bool on_enter(const SceneId& id) { return true; }
	/// @brief Called when this scene leaves the stack.
	virtual void on_exit() {}
	/// @brief Called when this scene becomes the **input** top again because something above it
	///        left.
	///
	/// The port of `closeUI`'s `newUI->focusRegained()` (`gui/ui/ui.cpp:194-199`), and it means the
	/// same thing: input reaches this scene again after reaching something else. The stack fires it
	/// only when `SceneStack::input_top()` genuinely changed hands, so a passive
	/// `input_transparent()` layer -- a popup, a console line, the working animation -- appearing
	/// over this scene and going away again is **not** an edge, in either direction. Legacy agrees:
	/// its popup is an OLED overlay (`OLED::popupText`), never a `UI` on `uiNavigationHierarchy`,
	/// so it never reaches `closeUI` either.
	///
	/// @note There is deliberately no `on_pause` dual. Nothing needs one yet -- `on_enter` already
	///       runs on the scene taking the top -- and adding one would put a hook on all thirty-odd
	///       scenes to serve no caller. If one is ever added it must be fired on exactly the same
	///       condition as this, or the pair stops being a pair; see design spec §15.7.
	virtual void on_resume() {}
	/// @}

	/// @brief React to model changes published since the last tick.
	/// @param dirty What changed.
	virtual void on_model_changed(const notify::DirtySet& dirty) {}

	/// @name Composition
	/// How the shell composes this scene with the ones beneath it.
	/// @{
	/// @brief Should what is beneath be dimmed and this scene composited over it?
	virtual bool is_grid_overlay() const { return false; }
	/// @brief Does this scene cover the grid completely, so scenes beneath need not render?
	virtual bool occludes_grid() const { return false; }
	/// @brief Should what is beneath this scene be dimmed for it?
	///
	/// Spec D §7's rule is "dim by right-shift if any overlay is above", and for an overlay that
	/// actually composites pads -- a sidebar menu, a context menu -- that is right: the dim is what
	/// says the layer beneath is no longer the live one. **An overlay that composites nothing may
	/// opt out.** A popup, a console line and the working animation are OLED-only in legacy
	/// (`OLED::sendMainImage`, `hid/display/oled.cpp:364-376`) and have no grid effect whatever;
	/// halving every pad's brightness to announce a "SAVED" popup would be a visible regression,
	/// not a port. They are still `is_grid_overlay()` -- they must not be mistaken for the grid
	/// base -- so the two questions are separate predicates.
	///
	/// @return True (the default, for any grid overlay) to dim what is beneath; false to leave it
	///         exactly as the base painted it.
	virtual bool dims_grid() const { return is_grid_overlay(); }
	/// @brief Should the scene beneath still draw \p role, with this scene drawn over it?
	/// @param role The surface in question.
	virtual bool oled_transparent(SurfaceRole role) const { return false; }
	/// @brief Is this scene passive: drawn over whatever is beneath, but never taking its input?
	///
	/// A popup, a console line and the working animation are the shipped cases: legacy draws all
	/// three straight into the OLED image on their way out (`OLED::sendMainImage`,
	/// `hid/display/oled.cpp:364-376`) and no key press, pad or encoder ever reaches them -- the
	/// UI underneath keeps running exactly as it was. Without this flag, a scene that merely
	/// *sits* on the stack would take the input focus away from the scene it is drawn over, which
	/// would make a console message deafen the view beneath it for as long as the line is up.
	///
	/// The shell reads it in two places, and they must agree:
	/// - **Routing.** `SceneStack::input_top()` -- the topmost scene that is *not*
	///   input-transparent -- is the scene `Router::route`/`route_post_render` call `handle()` on,
	///   rather than `top()`.
	/// - **Focus.** That same scene is the one whose render frames are built with
	///   `InputMask::all()`; every other layer in the pass, above or below, gets
	///   `InputMask::none()` and therefore paints normally but reads 0/false and claims nothing.
	///
	/// @return True to pass input through to the scene beneath; false (the default) to take it.
	virtual bool input_transparent() const { return false; }
	/// @brief Which grid pads this scene consumes.
	///
	/// The rest are forwarded to the legacy root view by `gui/ui/shell_pad_forward.cpp`, as
	/// `SoundEditor::padAction` forwards them. **`All` is the default on purpose**: every scene
	/// that existed before this virtual keeps exactly today's behaviour, and a scene that forgets
	/// to answer forwards nothing rather than forwarding something it meant to keep.
	///
	/// @note The exact predicate `ShortcutsOnly` stands for -- pressed, main grid, shift held --
	///       is applied by the reader, not here: it needs `kDisplayWidth` and the button state,
	///       neither of which this layer may read. See `ShellHostUI::claimsGridPad`.
	virtual GridPadClaim grid_pad_claim() const { return GridPadClaim::All; }
	/// @brief Which pad rows and columns this scene wants greyed out.
	/// @return The mask; all-zero (the default) greys nothing.
	virtual grid::GreyoutMask greyout_mask() const { return {}; }
	/// @brief Is this scene a **root view** -- one of the things legacy calls a `RootUI`?
	///
	/// Not a composition question like the rest of this group, but it lives here because it is a
	/// property of what the scene *is* rather than of what it does.
	///
	/// @par Who reads it, and what each one gates
	///
	/// Two services, and they gate different amounts:
	///
	///   * `services::GlobalControls` gates **one group** of arms -- its port of
	///     `View::buttonAction` (`global_controls.cpp:580`, the `view_button_action` call). Its
	///     other group, the port of `Buttons::buttonAction`'s tail, is deliberately *ungated*,
	///     because legacy runs that tail for any current UI. The two reachability rules below are
	///     why one call is gated and the other is not.
	///   * `services::MidiLearn` gates **all four of its `View::buttonAction` arms** and only those
	///     (`midi_learn.cpp:491`, the same sentence `services/midi_learn.h:58` makes). It is the
	///     consumer that made the gate load-bearing, because `MidiLearn` is routed last and therefore
	///     sees a `LEARN` press under a non-root scene where legacy hands it to
	///     `SoundEditor::buttonAction`'s own arm.
	///
	///     **It is no longer the whole of `handle()`, and this bullet said it was until F1 task 10's
	///     fix round.** That task put the two MIDI arms (`UiEvent::Kind::MidiNoteOn`/`MidiCc`)
	///     deliberately *above* the gate, and `midi_learn.cpp:457-462` argues why: legacy reaches
	///     `view.noteOnReceivedForMidiLearn` from `PlaybackHandler` with no root-UI test anywhere on
	///     the path, so gating them would invent a restriction legacy does not have. The claim was
	///     falsified by the same commit that then swept this file's citations -- which bumped the
	///     number and left the sentence. See `scripts/check_citations.py`'s sweeping section: a shift
	///     map keeps a citation pointing where it pointed, so a freshly-correct line number can sit
	///     on a sentence the same commit made false, and read as evidence the text was checked.
	///
	/// @warning **This list is owned by whoever adds the next consumer.** It said "there is
	///          exactly one consumer today" and named only `GlobalControls` for the whole of E4,
	///          having been written one task before `MidiLearn` became the second — the same way
	///          `Router`'s mod-knob ordering note stayed false for three plans because nobody
	///          re-read it when the consumer changed. If you add a third reader, add it here, with
	///          what it gates.
	///
	/// @par Why the shell needs this at all
	///
	/// Legacy has two dispatch fallbacks with **two different reachability rules**, and a service
	/// routed at the end of the chain has to honour both:
	///
	///   * `Buttons::buttonAction`'s tail (`hid/buttons.cpp:143-231`) runs for **any** current UI
	///     that returned `NOT_DEALT_WITH` from `getCurrentUI()->buttonAction` at `:134`, root or
	///     not. That group needs no gate.
	///   * `View::buttonAction` (`gui/views/view.cpp:140-487`) is reached only from a **root view**.
	///     `UI::buttonAction`'s base implementation is `return ActionResult::NOT_DEALT_WITH`
	///     (`gui/ui/ui.h:98-100`) -- it does *not* forward to `view`, unlike the three mod hooks
	///     (`gui/ui/ui.cpp:35-45`). `grep "public RootUI"` finds exactly two classes in the tree,
	///     `TimelineView` (`gui/views/timeline_view.h:27`) and `KeyboardScreen`
	///     (`gui/ui/keyboard/keyboard_screen.h:35`), and **both** forward to `View::buttonAction`
	///     (`gui/views/timeline_view.cpp:119-121`, `gui/ui/keyboard/keyboard_screen.cpp:591`). So
	///     "is the current UI the root UI" is not an approximation of that reachability rule -- it
	///     **is** the rule, exactly.
	///
	/// `services::GlobalControls` ports both groups and gates only the second on
	/// `SceneStack::input_top().is_root()`. `services::MidiLearn` ports only arms of the second
	/// kind, which is why its gate covers everything it does.
	///
	/// @note A `Scene` virtual rather than a stack-position rule (`input_top() == scenes()[0]`)
	///       deliberately. A position rule cannot be driven from a spec that holds no real scene:
	///       registry scenes are process singletons, so a rig would have to push one and leak it
	///       into the next example. A predicate a spec-local scene can override keeps the gated
	///       arms live and testable with no shared state.
	///
	/// @return True for a root view; false (the default) for every overlay, menu, browser and
	///         editor.
	virtual bool is_root() const { return false; }
	/// @}

	/// @name Saved state
	/// `replace_root` saves and restores; `SceneStack::remove` also saves (the excised scene, alone)
	/// but never restores -- there is no argument-carrying re-entry to restore into, the way
	/// `replace_root` has. `pop` and `replace_top` do neither.
	/// @{
	/// @brief Write whatever should survive being replaced as root into \p state.
	/// @param state The scene's slot, to be filled in.
	virtual void save_state(SceneState& state) const {}
	/// @brief Read back what a previous `save_state` wrote.
	/// @param state A slot this scene previously saved to.
	virtual void restore_state(const SceneState& state) {}
	/// @}

	/// @brief When this scene next needs a `tick()` even with no input.
	///
	/// **"Wake me later" means "repaint me later".** `Shell::tick`'s step 8b arms the surfaces
	/// `timer_repaints()` names at exactly this deadline, so a timer *is* an animation request and a
	/// scene that overrides this hook gets its blink, scroll or countdown without arming anything
	/// itself.
	///
	/// It did not always mean that, and the history is why the two were folded together. A timer
	/// used only to feed `Shell::compute_next_wake`, and whether the woken tick drew anything was
	/// decided separately by `RepaintGate::needs_frame` -- so every animated scene had to *also*
	/// call `f.gate().request_repaint_after(period_ms, f.now_ms())` from inside its own `render_*`,
	/// and **four scenes in a row shipped or nearly shipped without it**: E2's menu cursor blink,
	/// E3 task 3's `BrowserScene` (whose first quiet-tick example passed with the arm deleted,
	/// because an unrelated repaint covered for it), E3 task 4's `MarkerScene` (whose four blink
	/// phases all rendered byte-identically), and E3 task 5's `RecorderScene` -- where the cost was
	/// not cosmetic at all, because without the arm the completion poll never ran and a finished
	/// recording was never noticed. A defect shape that recurs at that rate is a missing default,
	/// not four mistakes.
	///
	/// A scene may still arm a gate from its own `render_*`, and several do: an arm computed from
	/// the phase actually being drawn (`MenuScene::render_grid`'s `ms_to_phase_change`) is tighter
	/// than this hook's single per-scene answer, and a *widget* has no other way to ask. Those arms
	/// are refinements on top of step 8b, not a substitute for it.
	///
	/// **Zero is a legal answer, and it means "next tick", not "never".** It reaches
	/// `RepaintGate::request_repaint_after(0, now_ms)`, which sets the deadline to `now_ms` --
	/// due on the following tick, since `needs_frame` tests `now_ms - deadline >= 0` -- and it is
	/// the same idiom `Shell::tick`'s own step 8a already uses to turn a router `Repaint` into a
	/// wake (`shell.cpp:397-411`). It is not a busy loop: the shell still only ticks when its host
	/// asks it to, and the grid's flush diffs against `last_sent`, so a repaint that changed no
	/// pixel costs no blit. It IS a per-tick blit on an OLED surface, which has no such diff, so a
	/// scene that returns zero should claim `{.oled = false}` in `timer_repaints()` unless its OLED
	/// content really does change every tick.
	///
	/// The case it exists for is `AudioClipView::graphicsRoutine`'s play cursor, ported by
	/// `scenes::audio_clip::AudioClipScene` (E4 task 11) -- a *frame* animation rather than a
	/// scheduled event, which is what "every tick" spells.
	///
	/// @warning **Corrected (F1 task 7, 2026-09-07): legacy's `graphicsRoutine` is NOT driven from
	///          the main loop, and it is not per-iteration.** This paragraph said "a legacy routine
	///          driven from the main loop rather than from `uiTimerManager`", and both halves are
	///          wrong. `grep -rn "graphicsRoutine()" src/deluge` gives exactly one call site outside
	///          the `UI` hierarchy: `UITimerManager::routine`'s `TimerName::GRAPHICS_ROUTINE` arm,
	///          which calls `getCurrentUI()->graphicsRoutine()` and then re-arms itself at **15 ms**
	///          (`gui/ui_timer_manager.cpp:213-218`), behind a pad-output-space gate. So legacy's
	///          per-frame cadence is ~66 Hz on `uiTimerManager`, not the main loop's rate. Zero here
	///          still means "next tick" -- what it costs is a question for the *host*, which decides
	///          the tick rate, and `ShellHostUI` (`gui/ui/shell_host_ui.h`, `@par Why the floor is
	///          1 ms`) is the first one that has had to answer it.
	///
	/// @return Milliseconds from now, or `std::nullopt` for "no timer".
	/// @see timer_repaints, for which surfaces (if any) that deadline arms.
	virtual std::optional<uint32_t> next_timer() const { return std::nullopt; }

	/// @brief Which of this scene's own surfaces `next_timer()`'s deadline should repaint.
	///
	/// **A `Scene` that does not override this arms both**, which is what makes `next_timer()` mean
	/// "repaint me later" (see it). That default lives in *this function's body*
	/// (`return {true, true};`) and deliberately **not** in `RepaintSurfaces`' member initialisers,
	/// which are `false`. The distinction is the whole safety property of the type, and E4's final
	/// review is what forced it out: while the members defaulted to `true`, the natural override
	///
	/// ```cpp
	/// RepaintSurfaces timer_repaints() const override { return {.grid = true}; }
	/// ```
	///
	/// silently also claimed the OLED, because a designated initializer leaves unnamed members at
	/// their member default. Every one of this tree's overrides happens to spell both fields, so
	/// nothing was broken -- but the next author to write the obvious thing would have armed an
	/// unconditional per-timer blit on every OLED panel, which is the exact regression that shipped
	/// twice in E3 (50 Hz, then 100 Hz, both two-panel; the account is below). With `false` members,
	/// a designated initializer can only ever arm what the author named, and the "both" default is
	/// still reached by anyone who overrides nothing.
	///
	/// **Per-surface, not all-or-nothing -- E3 task 10 fix round 1.** The fold's first cut armed
	/// every surface a scene is drawn on from one shared deadline, on the reasoning that
	/// over-arming only costs a repaint of an unchanged frame. That reasoning held for the grid,
	/// whose `Shell::flush` diffs against `last_sent` and blits nothing when a repaint changed no
	/// pixel -- but the OLED panels have no such diff: `flush` blits an OLED surface unconditionally
	/// once it has rendered. `MenuScene::next_timer()` returns a constant interval whenever the
	/// focused node has shortcut or source pads (`menu_scene.cpp`), which is purely a GRID
	/// concern -- the submenu's OLED content never depends on that blink -- so arming the OLED
	/// gates from it too turned "sitting on a patched-param leaf" into both panels blitting
	/// unconditionally, forever, at the timer's own cadence. No golden or pixel comparison can see
	/// this: the content blitted is byte-identical every time, which is exactly why it is a battery
	/// cost rather than a correctness bug, and exactly why it needed a name rather than a note.
	///
	/// Override this (`MenuScene` does, `{.oled = false, .grid = true}`) when a scene's timer has a
	/// visible consequence on only one kind of surface. A scene that needs both -- `RecorderScene`'s
	/// poll updates the OLED word AND, through the grid, keeps running under its own popup
	/// (`recorder_scene.h`'s class note) -- takes the default and pays for both, correctly.
	///
	/// **A claim, not a prediction -- and only honoured where the scene can paint** (E3 final
	/// review, A2). The render functions draw from a base up: `Shell::render_grid` from
	/// `Shell::grid_base()`, `Shell::render_oled` from `Shell::oled_lowest(role)`, per role. Step 8b
	/// arms a claimed surface only for a scene at or above that surface's base, because a covered
	/// scene arming a gate buys a repaint it can never appear in -- and on the OLED that is a blit.
	/// A covered scene loses nothing else: `compute_next_wake` still wakes the shell at its
	/// deadline, and `Shell::tick_stack` still calls `tick(dt)` on it.
	///
	/// @return `{true, true}` (what the un-overridden body below returns) to arm both kinds of
	///         surface at the timer's deadline; `{false, false}` -- which is what a
	///         default-constructed `RepaintSurfaces` is -- for a timer with no visible consequence
	///         at all: state a later input will read, a deadline that only matters once something
	///         else happens. That makes `Shell::tick` wake the shell for it without dirtying either.
	struct RepaintSurfaces {
		/// @brief Arm the OLED panels. **Defaults to `false` on purpose** -- see this struct's
		///        owning function: an unnamed member of a designated initializer must not arm a
		///        surface the author did not ask for. The "both" default is the base body's, not
		///        the type's.
		bool oled = false;
		/// @brief Arm the grid. `false` for the same reason `oled` is.
		bool grid = false;
		/// @brief Whether this asks for anything at all -- the full opt-out. **Not** the test
		///        `compute_next_wake` skips on: that one is `Shell::timer_arms_a_gate`, which also
		///        answers false for a covered scene and for an OLED-only claim on a profile with no
		///        OLED surfaces.
		constexpr bool any() const { return oled || grid; }
	};
	virtual RepaintSurfaces timer_repaints() const { return {.oled = true, .grid = true}; }
};

// The property the member defaults exist for, checked by the compiler rather than by review:
// naming one surface must never arm the other, and the "both" default belongs to
// `Scene::timer_repaints()`'s body rather than to the type. It costs nothing and it fails at the
// build if anyone puts the `true`s back on the members. (Namespace scope and not class scope: a
// nested class's default member initializers are not usable inside the enclosing class's body.)
static_assert(!Scene::RepaintSurfaces{.grid = true}.oled, "naming .grid must not arm the OLED");
static_assert(!Scene::RepaintSurfaces{.oled = true}.grid, "naming .oled must not arm the grid");
static_assert(!Scene::RepaintSurfaces{}.any(), "a default-constructed claim arms nothing");

/// @brief The application's scene table: one pointer per `SceneKind`, indexed by the kind itself.
///
/// A borrowed view over an array the application owns (a `static constexpr`-sized array of
/// pointers to its scene singletons), so the registry itself allocates nothing and a spec can hand
/// the stack a table of two test scenes. Kinds the application does not implement are left null.
struct SceneRegistry {
	std::span<Scene* const> by_kind;

	/// @brief The scene registered for \p k.
	/// @param k The kind to look up.
	/// @return The scene, or null when \p k is unregistered or past the end of `by_kind` -- a
	///         short table is a table whose later kinds are simply not implemented, never an
	///         out-of-bounds read.
	Scene* find(SceneKind k) const {
		const size_t i = static_cast<size_t>(k);
		return i < by_kind.size() ? by_kind[i] : nullptr;
	}
};

} // namespace deluge::ui::shell
