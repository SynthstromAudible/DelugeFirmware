#pragma once
#include "notify/dirty_set.h"
#include "shell/fixed_vector.h"
#include "shell/input_lowering.h"
#include "shell/leds.h"
#include "shell/loading_scene.h"
#include "shell/mirror.h"
#include "shell/notices.h"
#include "shell/profile.h"
#include "shell/router.h"
#include "shell/scene.h"
#include "shell/scene_stack.h"
#include "shell/screensaver.h"
#include "shell/service.h"
#include "shell/surfaces.h"
#include "ui/grid/effects.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace deluge::ui::shell {

/// @brief The board-independent settings one `Shell` is built with.
struct ShellConfig {
	/// Passed straight through to the `InputLowering` the shell owns.
	LoweringConfig lowering;
	/// @brief How long the shell may sit with no input before the screensaver scene is pushed; 0 is
	///        off.
	///
	/// @note **This is the value at construction; after that the number lives in the service.**
	///       Legacy's is
	///       `screensaverMode == ScreensaverMode::OFF ? 0 : screensaverTimeoutMinutes * 60000`
	///       -- both live in `FlashStorage`, which `src/shell` may not read -- and legacy re-reads
	///       them on every arm and every tick, so the port has to be *told* when they change.
	///       `Shell::set_screensaver_idle_ms` is that entry point and the port of
	///       `Screensaver::settingsChanged()`; a host built at static-init cannot supply a real
	///       number here at all, so for such a host this default of 0 is only ever a placeholder.
	///       F1 task 8 wired the firmware's: `hid/display/screensaver.cpp`'s `settingsChanged()`
	///       computes the formula and pushes it, which covers boot and both settings items.
	uint32_t screensaver_idle_ms = 0;
	/// The seed the screensaver's animation is started from; passed through to the pushed
	/// scene's `SceneId::arg` (see `Screensaver`).
	uint32_t seed = 0;
};

/// @brief What the shell calls to find the application's root scene for a song, on the two paths
///        that ask for it (`Shell::song_loading(false)` and a bare `Dirty::SongSwapped`).
///
/// A free function pointer plus an opaque context rather than a `std::function`, matching the
/// rest of `src/shell`'s no-allocation rule; `ctx` is passed back unchanged, so an application
/// wanting member access closes over `this` there.
struct SongHooks {
	/// @param ctx The context below, passed straight through.
	/// @return The scene to make the new root.
	SceneId (*root_for_song)(void* ctx) = nullptr;
	void* ctx = nullptr;
};

/// @brief The shell's own name for \p k, for `.state` lines and diagnostics.
/// @param k The kind to name.
/// @return A static, NUL-terminated name for one of the shell's own kinds, or nullptr for an
///         application kind (`SceneKind::FirstApp` and up) and for a value the shell does not
///         know -- the shell has no vocabulary for an application's scenes, so `state_lines()`
///         spells those numerically instead of inventing a name for them.
const char* scene_kind_name(SceneKind k);

/// @brief The whole UI, once per tick: drain what changed, route what was pressed, tick the scene
///        stack, render every surface that needs a frame, and derive the LEDs.
///
/// One object replacing the legacy `doAnyPendingUIRendering` / `uiTimerManager.routine` / the UI
/// half of `readButtonsAndPadsOnce` / `GRAPHICS_ROUTINE` quartet (`deluge.cpp:594-626`). It owns
/// the scene stack, the input lowering, the router, the three overlay scenes and the `Notices`
/// service that drives them; it *borrows* the `SurfaceSet` (the only hardware seam) and the board
/// profile. Nothing here is global, and nothing on the tick path allocates -- `state_lines()`,
/// which builds a `std::string`, is the UI test harness's entry point and is never called from a
/// tick.
///
/// # The tick window
///
/// `InputLowering` is a begin/feed/end machine (`input_lowering.h`, "The tick"), and
/// `begin_tick()` is what clears the held-back queue, the tick's claims and `had_input_this_tick`.
/// Input, however, arrives *between* ticks -- the board polls and calls `on_board_event` long
/// before `tick()` runs -- so the shell cannot open the window at the top of `tick()` without
/// throwing away exactly the events that tick is meant to act on. The window is therefore opened
/// lazily by whichever comes first, a feed or `pump_input`, and closed at the *bottom* of
/// `tick()`:
///
///     on_board_event ... -> [begin_tick] feeds ... | tick: pump_input -> end_tick -> route
///       -> render -> after_render -> deliver held-back (claims still live) -> leds -> flush
///       -> window closed; the next feed re-opens it, which is what clears held_back()/claims.
///
/// That ordering is what lets `deliver_post_render_input` read `held_back()` and `ctx().claims`
/// after the frame has been built: nothing between `end_tick()` and the end of the tick calls
/// `begin_tick()`, so neither has been cleared yet. The window's *clock*, however, is the tick's,
/// not the opening feed's: `InputLowering::end_tick(now_ms)` re-stamps the snapshot, so
/// `f.now_ms()` is always the clock the frame is being built on.
///
/// # Repaint requests
///
/// **A `Router` repaint request is always read before it is cleared, and one raised too late to be
/// read this tick takes effect on the next one.** `Action::Repaint`/`RepaintAll` set a flag on the
/// `Router` and nothing else -- the router holds no surface and arms no gate -- so the flag's only
/// reader is `tick()`'s OLED loop (step 5). `router_.clear_repaint_requests()` therefore runs
/// immediately *after* the render decisions, not at the bottom of the tick: steps 7
/// (`after_render()`) and 8 (post-render delivery) both `apply()` results of their own, and a
/// clear placed after them would set and wipe such a request within one tick with no reader in
/// between -- losing it outright rather than delaying it. What those two steps request instead
/// lands on the named surfaces' own `RepaintGate`s at the bottom of the tick, at a zero-millisecond
/// deadline, so `next_wake()` reports `now` and the next tick both happens and renders.
///
/// # What a scene may read, and where
///
/// **A scene reads any one input in at most one render pass per tick.** Reading through a frame
/// *is* claiming it (`ui/core/frame_context.h`, rule 2), and a scene drawn on two surfaces builds
/// two independent `FrameInput`s in the same tick -- so reading `f.turn()` in both `render_oled`
/// and `render_grid` claims the same detent twice and trips `InputClaims::double_claim`. A scene
/// that wants one input to affect both surfaces reads it in the pass that owns it and lets the
/// other surface draw the state that produced; a scene that needs the raw event on both leaves it
/// unclaimed and reacts in `handle()`, which the shell delivers *after* render for exactly that
/// reason.
///
/// Only the **input-frontmost** scene (`SceneStack::input_top()`, the topmost that is not
/// `Scene::input_transparent()`) is drawn with `InputMask::all()`; every other layer in a pass
/// draws with `none()` and therefore reads 0/false and claims nothing.
class Shell {
public:
	/// @brief Build a shell over \p surfaces for the board \p profile describes.
	/// @param surfaces The hardware seam; borrowed, and must outlive the shell.
	/// @param profile  The board's tables; copied (a `BoardProfile` is a handful of spans).
	/// @param registry The application's scene table. Copied into the shell's own table, which
	///                 additionally holds the shell's own `Popup`/`Console`/`Working` scenes: the
	///                 shell owns those three kinds and `Notices` drives its own instances of
	///                 them, so an entry an application put in one of those slots is not used.
	/// @param services The mod-knob/global-controls/MIDI-learn services; unfilled slots default to
	///                 `null_service()`.
	/// @param config   Lowering and screensaver settings.
	Shell(SurfaceSet& surfaces, const BoardProfile& profile, const SceneRegistry& registry, Services services,
	      ShellConfig config);

	Shell(const Shell&) = delete;
	Shell& operator=(const Shell&) = delete;

	/// @brief Install (or replace) the three services, after construction.
	///
	/// **This is how an application installs a service at all.** The constructor takes `Services`
	/// by value, but `services::ModKnobs`, `services::GlobalControls` and `services::MidiLearn`
	/// are each built from a `SceneStack&` -- and the first two also from an `InputLowering&` --
	/// and the only instances of either in an application are the ones this shell **owns**
	/// (`stack_`, `lowering_`, reachable through `stack()`/`lowering()`). Each of the three really
	/// uses what it is handed: `ModKnobs::show_popup` pushes the param popup onto that stack
	/// (`scenes/services/mod_knobs.cpp:413`), and `ModKnobs::popup_suppressed` (`:351`) and
	/// `ModKnobs::mod_encoder_button_action` (`:564`) both read `stack_.input_top()`;
	/// `GlobalControls::handle` gates its `View::buttonAction` group on
	/// `stack_.input_top().is_root()` (`scenes/services/global_controls.cpp:580`);
	/// `MidiLearn::handle` reads the same (`scenes/services/midi_learn.cpp:491`). So a real
	/// service cannot be constructed before the shell it serves, and before this existed it could
	/// not be installed after it either -- an application could only ever pass `Services{}`, and
	/// no gold-knob turn, transport button or MIDI-learn press reached anything outside the scene
	/// stack. Migration spec §10.2's blocker 7; the shell spec's §15.9 has the account.
	///
	/// Build the shell with `Services{}`, construct the services over `stack()`/`lowering()`, then
	/// call this. Three things move together and this call is what keeps them in step: `services_`
	/// (the slots), `Router`'s own by-value copy (what routing reads), and `service_list_` (the
	/// deduplicated per-tick list). Installing twice, or over a live set, is well defined: the
	/// list is rebuilt from scratch, so a service installed earlier is not left in it and a
	/// re-install does not double-tick anything.
	///
	/// @note The dedup rule the constructor has always had holds afterwards, unchanged: a service
	///       occupying two slots is ticked, asked for LEDs and asked for its timer exactly once
	///       per tick. It is still *routed to* once per slot -- `Router::route` reads the slots,
	///       not the list, and the chain's shape is not what the dedup is about.
	/// @param services The new slots; an unfilled one defaults to `null_service()`, and a slot set
	///                 to `nullptr` is skipped by the list (but must not be routed to -- see
	///                 `Services`).
	void set_services(Services services);

	/// @brief Push the first scene.
	/// @param initial What to push.
	void init(SceneId initial);

	/// @brief Prepare the surfaces for the first frame, with nothing on the stack.
	///
	/// Ports legacy's boot sequence, which is only `renderUIsForOled` marking the OLED dirty
	/// (`deluge.cpp:119,132,370`, now `raise(Dirty::Oled)`): a repaint requested on every surface,
	/// so the first `tick()` after this call draws regardless of any gate's prior state.
	///
	/// **It pushes no scene, deliberately.** The stack stays empty until the application pushes its
	/// own root, and an empty stack draws nothing -- which is what legacy's firmware does before its
	/// first root frame. The logo a player sees at power-on is the *bootloader's*, not this
	/// firmware's: `hid/display/rainfall.cpp` transcribes its `logoPixels` for the screensaver, and
	/// that is the only thing here that draws it.
	///
	/// This call used to push a `BootScene` -- a "DELUGE" title over the firmware version -- which
	/// was removed because the port must not add UI the instrument did not have. The shell design
	/// spec's section 9 amendment has the full account.
	void boot();

	/// @brief Wire (or clear) the hooks the shell asks for the application's song root through,
	///        on `song_loading(false)` and on a bare `Dirty::SongSwapped` (see `apply_dirty`).
	/// @param hooks The hooks; a default-constructed `SongHooks` (`root_for_song == nullptr`)
	///              makes both paths a no-op -- see their own docs.
	void set_song_hooks(SongHooks hooks) { song_hooks_ = hooks; }

	/// @brief Re-supply `ShellConfig::screensaver_idle_ms` after the settings behind it changed.
	///
	/// The shell-level half of `Screensaver::set_idle_ms`, which is the port of legacy's
	/// `Screensaver::settingsChanged()` (`hid/display/screensaver.cpp:200-241`). The number is
	/// `screensaverMode == ScreensaverMode::OFF ? 0 : screensaverTimeoutMinutes * 60000`, computed
	/// by the host -- `src/shell` may not read `storage/` (`check_layer_rules.py`'s
	/// `shell-includes-outside` row) -- and it has to be re-supplied on every change, because the
	/// service holds a copy where legacy re-read the globals on every arm and every tick.
	///
	/// **This is why the field is not construction-only.** A host that is a static-init global
	/// cannot read flash when it builds its `Shell`, so without this the value stays at its
	/// `ShellConfig` default of 0 and the screensaver can never activate however the two settings
	/// are set. `scenes/registry.h`'s boot-wiring note is the in-tree record; F1 task 8 is the
	/// caller.
	/// @param idle_ms The new idle period in milliseconds; 0 is off.
	void set_screensaver_idle_ms(uint32_t idle_ms) {
		// `config_` is kept in step so it stays the description of the shell it is a member of;
		// nothing else reads this field after construction, and a copy that disagreed with the
		// service would be the next reader's trap.
		config_.screensaver_idle_ms = idle_ms;
		screensaver_.set_idle_ms(idle_ms);
	}

	/// @brief The shell's half of the song-load protocol (spec D §9): what the firmware calls from
	///        the load path (spec F wires it) to bracket a song read.
	///
	/// `loading=true`: reset the whole stack, push the shell's own `LoadingScene`, and take the
	/// input lock -- the port of `nullifyUIs()` (`gui/ui/ui.cpp:257-261`) plus the input side of
	/// `gui/ui/load/load_song_ui.*`'s "loading" presentation. `loading=false`: pop the loading
	/// scene, release the lock, and -- only if `set_song_hooks` was given a real
	/// `root_for_song` -- `replace_root()` to whatever it returns for the now-loaded song.
	///
	/// @note With no hooks set, `loading=false` pops `Loading` and stops there: the stack is left
	///       empty (`top()` is the `NullScene`, which draws nothing and swallows every event) --
	///       valid, if not useful, until either a later `init()`/`song_loading(true)` or hooks
	///       finally being set and a bare `Dirty::SongSwapped` arriving.
	/// @param loading True as loading starts, false once the song is ready.
	void song_loading(bool loading);

	/// @brief One pass of the whole UI, in spec D §7's order.
	/// @param now_ms The tick's wall clock.
	void tick(uint32_t now_ms);

	/// @brief Wire (or, with `nullptr`, unwire) the sysex/MIDI display mirror `flush` drives after
	///        every real blit of the `Main` surface.
	/// @param sink The mirror sink, or `nullptr` to stop mirroring.
	void set_mirror(MirrorSink sink) { mirror_.sink = sink; }

	/// @brief Start (or replace) the grid's tween.
	///
	/// Snapshots the live grid as \p tw's "from" frame, arms the grid gate for the next tick at
	/// this kind's step cadence, and hands the tween to `render_grid`, which blends from that
	/// frozen snapshot toward each tick's freshly rendered grid until `tw.duration_ms` has
	/// elapsed, at which point the tween clears and the live grid shows through unblended.
	/// A tween started before the surfaces are built (`grid_storage` unset) is refused outright --
	/// there is no grid to snapshot, and holding one pending would let it activate later against a
	/// snapshot from an unrelated moment.
	///
	/// \p tw.param's meaning depends on `tw.kind`:
	/// | `kind` | render, per tick | `param` |
	/// |---|---|---|
	/// | `Fade` | `gfx::grid::fade(from, live, t, live)` | unused |
	/// | `ScrollH` | `gfx::grid::scroll_horizontal(from, live, param * t / 65536, live)` | target offset, in main
	/// columns (`scroll_horizontal`'s own `offset`) | | `ScrollV` | `gfx::grid::scroll_vertical(from, live, param * t /
	/// 65536, live)` | target offset, in rows | | `GreyOut` | `ui::grid::greyout(live, greyout_mask, param * t /
	/// 65536)` | target `RGB::greyOut` proportion (the ~0..6500000 scale, e.g. spec D's `kGreyoutAmountFull`), reached
	/// at `t == 65535`; ramps **in** only -- there is no ramp-out kind |
	///
	/// where `t` is `(now_ms - tw.start_ms) * 65536 / tw.duration_ms`, clamped to 0..65535.
	/// `GreyOut` replaces (not stacks with) the tick's ordinary full-strength greyout pass while it
	/// runs, and applies only inside the stack's greyout mask, matching legacy's ramp
	/// (`pad_leds.cpp:983-1005`) feeding the same masked `prepareColour` (`:293-296`).
	/// @param tw The tween to play; `tw.start_ms` is the clock it is measured from.
	void start_tween(ActiveTween tw);

	/// @name Feeding input
	/// Called by the board poll loop between ticks; both open the lowering's tick window if it is
	/// not already open (see the class note).
	/// @{
	/// @brief Lower one event from `deluge_control_poll_event`.
	/// @param ev     The event.
	/// @param now_ms The wall clock at which it arrived.
	void on_board_event(const DelugeInputEvent& ev, uint32_t now_ms);
	/// @brief Lower the A-pin edges one encoder has accumulated.
	/// @param index  The board's encoder index.
	/// @param edges  Signed A-pin edge count since the last call.
	/// @param now_ms The wall clock.
	void on_encoder_edges(uint8_t index, int8_t edges, uint32_t now_ms);

	/// @brief Route one MIDI-learn message through the chain **now**, and say whether anything took
	///        it.
	///
	/// The odd one out of the three feeds, in the one way that matters: `on_board_event` and
	/// `on_encoder_edges` *queue*, and nothing is routed until the next `tick()`
	/// (`pump_input`) -- so neither can report what a scene did. This one routes synchronously,
	/// because its caller is a legacy `UI` virtual whose whole contract is a bool: the note-on and
	/// program-change learn dispatchers are
	/// `if (getCurrentUI()->...ForMidiLearn(...)) {} else { view....(...); }`
	/// (`playback/playback_handler.cpp:2968-2971`, `:2897-2901`), and a guest that answered "ask me
	/// again next tick" would hand the message to `view` as well.
	///
	/// It is not a lowering event and does not open the lowering's tick window: \p event is built
	/// by the caller (`Kind::MidiNoteOn` or `Kind::MidiCc`, with the cable, channel and two data
	/// bytes), not derived from a `DelugeInputEvent`, and the held sets, shift latch and press
	/// bookkeeping `make_context` reads are levels that are as valid between ticks as within one.
	///
	/// @param event  The event; anything but the two MIDI kinds is a caller error and is routed
	///               anyway (there is no shape here worth a second gate -- `Router` is total over
	///               `Kind`).
	/// @param now_ms The wall clock, for the `UiContext`.
	/// @return True if the top scene or a service reported `handled`, which the caller should
	///         return to legacy so its `else` does not also run.
	bool route_midi(const UiEvent& event, uint32_t now_ms);
	/// @}

	/// @brief Take or release the SD-routine lock, deferring board input while it is held.
	///
	/// Releasing it replays the deferred queue, so this opens the lowering's tick window like any
	/// other feed does -- see the tick-window note on this class.
	/// @param busy True to take the lock.
	void set_sd_busy(bool busy);

	/// @brief When the shell next has something to do with no input at all.
	/// @return The absolute deadline in milliseconds, or `std::nullopt` when nothing is armed --
	///         the scheduler may then sleep until the next input. Recomputed at the end of every
	///         `tick()` from the surface gates, the widgets' gate, every service's `next_timer()`,
	///         and the `next_timer()` of every stacked scene for which step 8b armed no gate at all
	///         (`timer_arms_a_gate`) -- a scene whose timer did reach a gate already has its
	///         deadline read from that gate. The three that reach the scene loop are the full
	///         opt-out, a scene covered on every surface it claims, and an OLED-only claim on a
	///         profile with no OLED surfaces.
	///
	/// @warning **A returned `now_ms` does not distinguish "wake once, immediately" from "keep
	///          ticking as fast as you can", and a host cannot currently tell them apart.** Zero is
	///          a legal `Scene::next_timer()` answer meaning "next tick" (its own doc has the
	///          account), and `scenes::audio_clip::AudioClipScene::next_timer()` returns it for the
	///          whole of playback (`audio_clip_scene.cpp:1199`) -- faithfully, because legacy drives
	///          that play cursor from `graphicsRoutine`, which is a per-frame redraw rather than a
	///          deadline. (It is **not** driven from the main loop: `TimerName::GRAPHICS_ROUTINE`
	///          re-arms it at 15 ms, `gui/ui_timer_manager.cpp:213-218` -- corrected F1 task 7, and
	///          `Scene::next_timer`'s own doc has the measurement.) So while an audio clip is
	///          playing this hook reports `now_ms` on
	///          every tick, for ever, which is indistinguishable from the busy loop the screensaver
	///          service shipped before its refusal latch (see `Screensaver`'s note below).
	///
	///          `0u` is still the right answer for a scene: zero means "next tick", and the *rate* is
	///          the host's decision rather than the scene's. `AudioClipScene` claims
	///          `{.oled = false, .grid = true}`, so there is no per-tick OLED blit, and the grid's
	///          flush diffs against `last_sent`, so a tick on which the column did not move sends
	///          nothing.
	///
	///          **What is no longer true is "free", and F1 task 7 is what made it untrue.** This
	///          paragraph used to say "it is not costing anything: the shell only ticks when its host
	///          asks it to". There is now a host -- `ShellHostUI` (`gui/ui/shell_host_ui.h`) -- and
	///          it converts this hook's answer into a `TimerName::UI_SPECIFIC` duration with a floor
	///          of 1 ms, because zero would leave `triggerTime` at the current audio sample and fire
	///          the timer on every pass of the main loop. So while a zero-`next_timer()` scene is
	///          stacked under that host the cost is paid twice: the scene's grid gate is re-armed at
	///          `now` on every tick, which is a full grid *render* at up to 1 kHz, and the guest
	///          re-arms `UI_SPECIFIC` at 1 ms, which runs legacy's whole 29-timer sweep at up to
	///          1 kHz where `graphicsRoutine` -- the thing this is faithful to -- runs at ~66 Hz
	///          (`gui/ui_timer_manager.cpp:213-218`). Latent rather than live: the guest's only
	///          tenant today is `MenuScene`, whose `next_timer()` answers `std::nullopt` for most of
	///          the tree. It stops being latent the moment `AudioClipScene` becomes a guest tenant.
	///
	///          It is **owed** as an expressiveness gap, and it is
	///          recorded here rather than at `next_timer()` because the host author scheduling on
	///          this value is the one who needs it. Whoever closes it wants a third answer -- a
	///          per-frame cadence, or a distinct "every tick" sentinel -- not a change to what zero
	///          means, since zero's current meaning is what makes overriding `next_timer()`
	///          sufficient for an animation at all.
	std::optional<uint32_t> next_wake() const { return next_wake_; }

	/// @name Inspection
	/// @{
	SceneStack& stack() { return stack_; }
	InputLowering& lowering() { return lowering_; }
	Notices& notices() { return notices_; }
	/// @brief The slots `set_services` last installed, for a host or a spec that wants to say which
	///        objects are wired in without owning them itself.
	const Services& services() const { return services_; }
	const SurfaceSet& surfaces() const { return surfaces_; }
	/// @brief The union of every stacked scene's `greyout_mask()`, as re-queried on the last tick
	///        the stack changed.
	grid::GreyoutMask greyout() const { return greyout_; }
	/// @brief How many LED declarations were dropped for naming a board LED past
	///        `LedFrame::kMaxEntries`.
	uint32_t leds_dropped() const { return leds_dropped_; }
	/// @brief How many times the idle deadline came and went with no `SceneKind::Screensaver`
	///        scene for the stack to push (`Screensaver::push_refused`).
	/// @return 0 normally; 1 once the screensaver service has latched itself off, which is what
	///         `screensaver_idle_ms > 0` does on an application that has not registered a
	///         screensaver scene.
	uint32_t screensaver_push_refused() const { return screensaver_.push_refused(); }
	/// @brief The idle period the screensaver service is running on; 0 while it is switched off.
	///        What `set_screensaver_idle_ms` last installed, or `ShellConfig`'s value if nothing has.
	uint32_t screensaver_idle_ms() const { return screensaver_.idle_ms(); }
	/// @}

	/// @brief The shell's half of the UI test harness's `.state` snapshot (spec G §3).
	///
	/// `"scene <KindName>[/<qualifier>]"` per stacked scene, **top first**, then `"lock input"`
	/// while the input lock is held, then `"notice <Kind> <arg>"` while the popup scene is on the
	/// stack. The surfaces' half (pads, LEDs, indicators) comes from the `SurfaceSet` side of the
	/// harness.
	///
	/// A scene of an application kind, which the shell has no name for, is spelled `app<n>`,
	/// qualified by its `SceneId::arg` when that is non-zero (`app16/3`). `Popup` is qualified by
	/// its popup type's name (`Popup/General`); every other shell kind is spelled bare. The
	/// `notice` line names the `Notice::Kind` that raised the popup and the text it resolved to
	/// (`notice Text SAVED`; an error with no l10n bridge resolves to its digits, `notice Error
	/// 248`).
	/// @return The lines, each newline-terminated; empty when nothing is stacked or locked.
	std::string state_lines() const;

private:
	/// @brief `on_model_changed` on every stacked scene, `Dirty::SongSwapped`'s bare re-query, and
	///        the dirty facts onto the gates.
	///
	/// `Dirty::Oled` invalidates the OLED surfaces; `GridMain`/`GridSidebar`/`ClipContent`/
	/// `NoteRows` invalidate the grid; every other fact is a model change with no surface named,
	/// so it invalidates both. `Dirty::SongSwapped` additionally -- once every scene has had its
	/// `on_model_changed` -- calls `replace_root(hooks.root_for_song(ctx))` if `set_song_hooks`
	/// was given real hooks; this is the narrower of the song-swap protocol's two paths (see
	/// `song_loading`'s own doc for the bracketed one) and a no-op with none set.
	/// @param dirty This tick's drained facts.
	void apply_dirty(const notify::DirtySet& dirty);
	/// @brief Close the lowering's tick window and route everything ordinary out of it.
	/// @param now_ms The tick's wall clock.
	void pump_input(uint32_t now_ms);
	/// @brief Tick the scene stack top-down, stopping below the first scene that occludes the
	///        grid, then every service.
	/// @param now_ms The tick's wall clock.
	/// @param dt_ms  Milliseconds since the previous tick.
	void tick_stack(uint32_t now_ms, uint32_t dt_ms);
	/// @brief The lowest scene an OLED surface with role \p role actually draws.
	///
	/// Walk down from the top while each scene lets the one below show through
	/// (`Scene::oled_transparent`); the scene that stops the walk is the lowest one that has to be
	/// drawn. **Per role**, because transparency is declared per role.
	///
	/// Factored out of `render_oled` so step 8b's timer arming can ask the same question the render
	/// answers -- a scene below this index cannot paint this surface, so arming its gate is a
	/// repaint and a blit that nothing can see (E3 final review, A2).
	///
	/// @param role The surface's role.
	/// @return The index into `stack_.scenes()`, or 0 for an empty stack.
	size_t oled_lowest(SurfaceRole role) const;
	/// @brief Can the scene at \p index paint the pad grid at all?
	///
	/// Two conditions, and the second is easy to forget: the scene has to sit at or above
	/// `grid_base()`, *and* the profile's grid surface has to have storage -- `render_grid` returns
	/// before the stack walk without it, so on such a profile the answer is no for every scene.
	///
	/// @param index The scene's index into `stack_.scenes()`.
	/// @return True when a grid repaint armed for this scene could reach the panel.
	bool grid_paintable(size_t index);
	/// @brief Can the scene at \p index paint OLED surface \p s at all?
	///
	/// The `oled_lowest` test, plus the `Style` `render_oled` itself demands.
	///
	/// @param s     The surface.
	/// @param index The scene's index into `stack_.scenes()`.
	/// @return True when an OLED repaint armed for this scene could reach that panel.
	bool oled_paintable(const OledSurface& s, size_t index);
	/// @brief Would step 8b arm any gate for the scene at \p index?
	///
	/// The one-bool form of what step 8b does: a claimed surface only counts when this scene sits at
	/// or above that surface's base, and the OLED half only counts when the profile has an OLED
	/// surface at all. `compute_next_wake` asks it, because a scene that armed nothing has no gate
	/// deadline standing in for its timer.
	///
	/// @param index    The scene's index into `stack_.scenes()`.
	/// @param surfaces The scene's `timer_repaints()` claim.
	/// @return True when at least one gate would be armed.
	bool timer_arms_a_gate(size_t index, Scene::RepaintSurfaces surfaces);
	/// @brief The lowest scene the pad grid actually draws: the topmost non-overlay scene.
	///
	/// Everything above it is a grid overlay by construction. A stack of nothing but overlays has no
	/// base and starts at the root. Factored out of `render_grid` for the reason `oled_lowest` is.
	///
	/// @return The index into `stack_.scenes()`, or 0 for an empty stack.
	size_t grid_base() const;
	/// @brief Draw one OLED surface: the topmost opaque scene for its role, and everything above.
	/// @param s The surface.
	/// @return False when the surface could not be drawn (no `Style`), so the caller does not mark
	///         an untouched canvas for blitting.
	bool render_oled(OledSurface& s);
	/// @brief Draw the pad grid: the base scene, dimmed if an overlay above it `dims_grid()`, then
	///        the overlays, then the stack's greyout.
	/// @param now_ms The tick's wall clock.
	void render_grid(uint32_t now_ms);
	/// @brief Halve every pad's channels in place, the dim an overlay is composited over.
	/// @param grid The pad framebuffer.
	static void dim_grid(gfx::grid::PadGrid& grid);
	/// @brief Offer this tick's held-back, widget-owned events to the routing chain.
	/// @param now_ms The tick's wall clock.
	void deliver_post_render_input(uint32_t now_ms);
	/// @brief Compose and resolve this tick's LED declarations into the LED surface.
	/// @param now_ms The tick's wall clock.
	void derive_leds(uint32_t now_ms);
	/// @brief Push the rendered surfaces to the hardware.
	/// @param now_ms The tick's wall clock.
	void flush(uint32_t now_ms);

	/// @brief Open the lowering's tick window if it is not already open.
	/// @param now_ms The clock to open it at.
	void ensure_tick_window(uint32_t now_ms);
	/// @brief The world around one event, as the scenes and services see it.
	/// @param now_ms The tick's wall clock.
	UiContext make_context(uint32_t now_ms) const;
	/// @brief Note that \p scene rendered this tick, at most once per tick.
	/// @param scene The scene that was just drawn.
	void append_rendered(Scene* scene);
	/// @brief The earliest deadline any gate, scene or service is waiting on.
	/// @param now_ms The tick's wall clock, which the scenes' relative timers are measured from.
	std::optional<uint32_t> compute_next_wake(uint32_t now_ms);
	/// @brief Rebuild `service_list_` from `services_` plus the shell's own `notices_` and
	///        `screensaver_`, each listed once.
	///
	/// Clears first, so it is idempotent and leaves nothing from a previous `services_` behind.
	/// Called from the constructor and from `set_services`, and it is deliberately the only
	/// writer of `service_list_`: the two callers sharing one body is what keeps the re-install
	/// path's dedup identical to the constructor's rather than a second copy of it that can drift.
	void rebuild_service_list();

	SurfaceSet& surfaces_;
	BoardProfile profile_;
	ShellConfig config_;
	Services services_;
	/// The shell's own scene table: the application's, plus the three overlay scenes below.
	std::array<Scene*, kMaxSceneKinds> table_{};
	SceneRegistry registry_;
	SceneStack stack_;
	InputLowering lowering_;
	Router router_;
	PopupScene popup_;
	ConsoleScene console_;
	WorkingScene working_;
	/// The shell's own boot splash and song-loading scenes, installed into `table_` alongside
	/// `popup_`/`console_`/`working_` above -- unlike `SceneKind::Screensaver`, which names no
	/// shell-owned scene at all (see `screensaver_`'s own doc).
	LoadingScene loading_;
	/// Pushes/pops `SceneKind::Screensaver` after idle. Deliberately does *not* own a scene
	/// instance the way `popup_`/`console_`/`working_`/`loading_` do: the real animation
	/// is spec E3's, registered by the application (or a spec's stand-in) like any other scene, so
	/// `table_[Screensaver]` is left exactly as `registry.find()` reported it.
	Screensaver screensaver_;
	Notices notices_;
	/// `mod_knobs`, `global_controls`, `midi_learn`, `Notices` and `Screensaver`, each listed
	/// once. Built by `rebuild_service_list()` -- from the constructor, and again from every
	/// `set_services()` call -- rather than once per tick, since the slots change only when an
	/// application says so.
	FixedVector<Service*, 5> service_list_;
	/// The hooks `song_loading(false)` and a bare `Dirty::SongSwapped` ask for the application's
	/// song root through; unset (`root_for_song == nullptr`) until `set_song_hooks` is called.
	SongHooks song_hooks_;

	/// The union of the stack's `greyout_mask()`s, re-queried whenever the stack changes.
	grid::GreyoutMask greyout_{};
	/// The sysex/MIDI display mirror `flush` drives after a real blit of the `Main` surface.
	Mirror mirror_;
	/// The grid tween's "from" frame: a snapshot taken by `start_tween` of the grid as it stood at
	/// that moment. `std::nullopt` whenever `GridSurface::tween` is, and cleared together with it;
	/// held here rather than on `GridSurface` because it is a `Shell`-side concern (the surface
	/// pool has no business owning a whole extra framebuffer) and `PadGrid` has no default
	/// constructor for a plain array member to hold before a tween's first snapshot.
	std::optional<gfx::grid::PadGrid> tween_from_;
	/// Scratch storage for `ScrollH`/`ScrollV` tween steps: `gfx::grid::scroll_horizontal`/
	/// `scroll_vertical` read `incoming` at column/row indices other than the one they are
	/// writing, so blending straight into the live grid could read a cell this same pass already
	/// overwrote. Reused every step rather than a per-tick stack local (~1.5 KB for `kMaxGridRows`
	/// x `kMaxGridCols` `RGB`s -- `PadGrid`'s own doc), since `Shell` already lives for the whole
	/// tick loop and the render path should not pay for it on every call.
	std::optional<gfx::grid::PadGrid> tween_scratch_;
	/// The scenes drawn this tick, in the order they were drawn; each gets one `after_render()`.
	FixedVector<Scene*, kStackDepth> rendered_this_tick_;
	std::optional<uint32_t> next_wake_;
	uint32_t last_ms_ = 0;
	/// Whether the lowering accepted anything this tick -- `RepaintGate::needs_frame`'s
	/// `had_input`, and the place Task 4 parked "set where legacy sets `anything`"
	/// (`deluge.cpp:611-614`).
	bool had_input_ = false;
	/// Whether the lowering's tick window is open; see the class note on the tick window.
	bool tick_open_ = false;
	/// Whether `tick()` has ever run, so the first tick's `dt` is 0 rather than the boot clock.
	bool ticked_ = false;
	/// How many LED declarations named a board LED at or past `LedFrame::kMaxEntries` and could
	/// therefore neither be diffed nor sent. Should stay at zero; non-zero means the board profile
	/// has outgrown the shadow arrays.
	uint32_t leds_dropped_ = 0;
};

} // namespace deluge::ui::shell
