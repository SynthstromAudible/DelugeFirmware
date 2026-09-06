#pragma once
#include "shell/input_lowering.h"
#include "shell/scene.h"
#include "shell/scene_stack.h"
#include "shell/service.h"
#include "ui/core/frame_context.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace deluge::ui::shell {

/// @brief The services the `Router` consults alongside the scene stack.
///
/// Each slot defaults to `null_service()`, so a `Router` built with `Services{}` -- or with only
/// the slots a caller cares about set -- runs the full legacy chain shape with the unfilled
/// slots contributing nothing. Spec E supplies the real `mod_knobs`/`global_controls`; `midi_learn`
/// is carried here for a later task and is not yet called from `Router::route`.
struct Services {
	Service* mod_knobs = &null_service();
	Service* global_controls = &null_service();
	Service* midi_learn = &null_service();
};

/// @brief The shell's routing chain: the top scene, then the mod-knob bar, then the global
///        controls.
///
/// The tail half is `Buttons::buttonAction`'s shape exactly -- `getCurrentUI()->buttonAction` at
/// `hid/buttons.cpp:134`, then the `:143-231` tail, which is where `MOD_ENCODER_0`/`MOD_ENCODER_1`
/// (`ButtonId::ModEnc0/1`) actually live, at `:225-231`, reached only after the scene has already
/// had a look. The `mod_knobs` step sits between them because `mod_knobs` is the port of `View`,
/// and `View` is what `UI`'s **base-class** hooks forward to; see `route()`.
///
/// The scene step is `SceneStack::input_top()` -- the topmost scene that is not
/// `Scene::input_transparent()` -- rather than `top()`, so a passive overlay (popup, console,
/// working animation) drawn over a scene does not intercept that scene's input.
///
/// A `Router` owns no scenes and no services -- it borrows the `SceneStack` and `InputLowering`
/// the shell already has, and the `Services` it is built with -- and applies every `Result` it
/// gets back through `apply()`, in order, whether or not that step went on to handle the event.
/// Routing stops at the first step that reports `handled`; an event nothing handles is counted by
/// `dropped_events()`.
///
/// @note Never mutates the stack directly. `apply()` is the only place `Push`/`Replace`/
///       `ReplaceRoot`/`Pop` reach `SceneStack`, which is what keeps a `Scene::handle()` call from
///       ever reentering the stack it is itself being called through.
class Router {
public:
	/// @brief Build a router over \p stack and \p lowering, consulting \p services.
	/// @param stack    The scene stack to route to and apply navigation actions on.
	/// @param services The mod-knob/global-controls/MIDI-learn services; unfilled slots default to
	///                 `null_service()`.
	/// @param lowering The lowering `LockInput` actions take effect on.
	Router(SceneStack& stack, Services services, InputLowering& lowering)
	    : stack_(stack), services_(services), lowering_(lowering) {}

	/// @brief Route one event through the full chain: top scene -> `mod_knobs` ->
	///        `global_controls` -> drop.
	///
	/// `services.mod_knobs->handle` is only consulted when \p event is a mod button or a gold-knob
	/// turn (`ButtonId::Mod0..Mod7`, `EncoderRole::GoldUpper/GoldLower`), and then only once the
	/// top scene has declined it; anything else goes from the scene straight to `global_controls`.
	///
	/// @par Why the scene comes first
	///
	/// `mod_knobs` is the port of `View`, and legacy never dispatches to `view` directly for
	/// either of these two event shapes. `interpretEncoders` calls
	/// `getCurrentUI()->modEncoderAction(e, offset_accelerated)`
	/// (`Encoders::interpretEncoders`, `hid/encoder_input.cpp:194`) and `Buttons::buttonAction`'s
	/// mod-button loop calls `getCurrentUI()->modButtonAction(i, on)` (`hid/buttons.cpp:104`).
	/// `UI::modEncoderAction` and `UI::modButtonAction` (`gui/ui/ui.cpp:35-41`) are the
	/// **base-class** implementations, and both do nothing but forward to `view` -- so the current
	/// UI gets first refusal and `View` is the fallback. Six UIs override the first
	/// (`SoundEditor`, `gui/ui/sound_editor.h:80`; `ArrangerView`, `gui/views/arranger_view.h:49`;
	/// `AutomationView`, `gui/views/automation_view.h:116`; `PerformanceView`,
	/// `gui/views/performance_view.h:96`; `SessionView`, `gui/views/session_view.h:100`;
	/// `InstrumentClipView`, `gui/views/instrument_clip_view.h:215`) and two override the second
	/// (`SessionView`, `gui/views/session_view.h:81`; `PerformanceView`,
	/// `gui/views/performance_view.h:98`).
	///
	/// **This was inverted from plan D until E4 task 3.** It was invisible for three plans because
	/// the slot held a `NullService`; the concrete cost was that `SoundEditor::modEncoderAction`'s
	/// MIDI-learn arm (`gui/ui/sound_editor.cpp:1583-1607`), whose port is `MenuScene`, could never
	/// have fired -- the service would have claimed the turn first. Recorded as amendment §15.4 of
	/// `docs/superpowers/specs/2026-08-26-ui-shell-design.md`.
	///
	/// In particular, a gold knob's own push
	/// (`ButtonId::ModEnc0`/`ModEnc1`) is **not** a `mod_knobs` event: legacy routes it to
	/// `getCurrentUI()->modEncoderButtonAction` from the tail, after the scene's own `buttonAction`
	/// has already run (`hid/buttons.cpp:225-231`), so it reaches the scene first here too, exactly
	/// as any other plain button would. Every `Result` along the way is `apply()`d immediately,
	/// whether or not it reports `handled`, and routing stops at the first `handled` step.
	/// @param event The event to route.
	/// @param ctx   The world around it.
	void route(const UiEvent& event, const UiContext& ctx);

	/// @brief Route one of the six widget-owned event shapes after render, unless a widget already
	///        claimed it this tick.
	///
	/// Shares `route()`'s exact chain and `apply()` call, and differs only in being skipped
	/// outright -- not even reaching the top scene -- when \p claims shows the bit this event maps
	/// to is set: `Encoder{Select}` -> `turn`, `Encoder{GoldUpper}` -> `edit0`,
	/// `Encoder{GoldLower}` -> `edit1`, `Encoder{Horizontal}` -> `horizontal`,
	/// `Encoder{Vertical}` -> `vertical`, `EncoderButton{SelectEnc}` -> `select`,
	/// `Button{Back}` -> `back`. A claimed event is neither routed nor counted by
	/// `dropped_events()`: a widget consumed it, which is not the same thing as nothing did.
	/// @param event  The event to route.
	/// @param ctx    The world around it.
	/// @param claims This tick's widget claims, read once to decide whether to route at all.
	void route_post_render(const UiEvent& event, const UiContext& ctx, const InputClaims& claims);

	/// @brief Apply every action in \p result, in order.
	///
	/// `Push`/`Replace`/`ReplaceRoot`/`Pop` reach `SceneStack`; `Popup` stores its string and type
	/// for `pending_popup_string()`/`pending_popup_type()`; `Repaint` sets the named surface's
	/// flag (a `surface` of `SurfaceRole::None` is a no-op -- see `repaint_requested()`);
	/// `RepaintAll` sets every surface's flag **and the grid's** (`grid_repaint_requested()`);
	/// `LockInput` calls `InputLowering::set_input_lock`.
	/// @param result The result to apply.
	void apply(const Result& result);

	/// @brief Has a `Repaint`/`RepaintAll` action requested \p role since the last
	///        `clear_repaint_requests()`?
	/// @param role The surface to ask about.
	/// @return True if requested; false for `SurfaceRole::None`, which keeps its "no surface,
	///         never matches" meaning here (`surfaces.h`) and is never itself a requestable slot.
	bool repaint_requested(SurfaceRole role) const;
	/// @brief Has a `RepaintAll` action requested the **grid** since the last
	///        `clear_repaint_requests()`?
	///
	/// Separate from `repaint_requested()` because `SurfaceRole` names OLED panels only
	/// (`shell/profile.h:60`) -- there is no role a `Repaint` action could give the grid, so
	/// `RepaintAll` is the one action that reaches it and this is the one way to ask.
	///
	/// **Added by E3 task 11**, whose `marker.txt` script recorded the hole: `Action::Kind::
	/// RepaintAll` is documented as "invalidate every surface for the next tick" (`shell/scene.h`),
	/// but `Shell::tick`'s grid step consulted only the grid's own gate, so a scene that changed
	/// what the *pads* show in response to post-render input -- `MarkerScene::handle`'s select turn,
	/// `BrowserScene::handle`'s -- redrew the OLED immediately and left the grid showing the
	/// previous frame until something else happened to repaint it. In the marker editor's case that
	/// was the blink timer, up to one 200 ms phase later.
	/// @return True if a `RepaintAll` has been applied since the last clear.
	bool grid_repaint_requested() const { return grid_repaint_; }
	/// @brief Clear every surface's repaint flag, the grid's included.
	///
	/// Consumed once a tick, after the shell has read `repaint_requested()` for every surface it
	/// owns.
	void clear_repaint_requests();

	/// @brief The most recent `Popup` action's string id, if any.
	/// @return The string id `apply()` last stored, or 0 if none is pending.
	uint16_t pending_popup_string() const { return popup_string_; }
	/// @brief The most recent `Popup` action's popup type.
	/// @return The `notify::Notice::popup_type` value `apply()` last stored, or 0 if none is
	///         pending (0 also means "unspecified", per `Action::popup_type`'s own note).
	uint8_t pending_popup_type() const { return popup_type_; }
	/// @brief Clear the pending popup string and type together.
	///
	/// Read and cleared by Notices (task 6); the two are stored and cleared as a pair so a reader
	/// never sees one updated without the other.
	void clear_pending_popup() {
		popup_string_ = 0;
		popup_type_ = 0;
	}

	/// @brief How many events fell off the end of the chain with nothing handling them.
	/// @return The count since construction.
	uint32_t dropped_events() const { return dropped_; }

private:
	/// @brief The shared chain body: top scene -> `mod_knobs` -> `global_controls` -> drop,
	///        `apply()`d at every step. Used by both `route()` and `route_post_render()`
	///        (the latter only once the claim gate has let the event through).
	void run_chain(const UiEvent& event, const UiContext& ctx);

	/// Real surfaces a `Repaint` action can name -- `Main` through `Aux`. `None` is excluded: it
	/// keeps `surfaces.h`'s "no surface, never matches" meaning and is never itself a slot (see
	/// `repaint_requested()`); `RepaintAll` is what sets every one of these flags at once.
	static constexpr size_t kSurfaceCount = static_cast<size_t>(SurfaceRole::Aux) + 1;
	// Pins the assumption above to SurfaceRole's actual declaration order (Main, Param0..3, Aux,
	// None) -- reordering the enum would otherwise silently change which surfaces kSurfaceCount
	// covers with no compile error.
	static_assert(static_cast<size_t>(SurfaceRole::None) == kSurfaceCount,
	              "SurfaceRole::None must immediately follow the last real surface");

	SceneStack& stack_;
	Services services_;
	InputLowering& lowering_;

	bool repaint_[kSurfaceCount]{};
	/// `RepaintAll`'s grid half -- see `grid_repaint_requested()`.
	bool grid_repaint_ = false;
	uint16_t popup_string_ = 0;
	uint8_t popup_type_ = 0;
	uint32_t dropped_ = 0;
};

} // namespace deluge::ui::shell
