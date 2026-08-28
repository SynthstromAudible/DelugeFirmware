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

/// @brief The shell's routing chain: the mod-knob bar, then the top scene, then the global
///        controls, in the order `Buttons::buttonAction` tried them (`hid/buttons.cpp:100-107`
///        for the mod-button loop, `:134` for the scene, `:143-231` for the tail -- which is where
///        `MOD_ENCODER_0`/`MOD_ENCODER_1` (`ButtonId::ModEnc0/1`) actually live, at `:225-231`,
///        reached only after the scene has already had a look).
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

	/// @brief Route one event through the full chain: `mod_knobs` -> top scene ->
	///        `global_controls` -> drop.
	///
	/// `services.mod_knobs->handle` is only consulted when \p event is a mod button or a gold-knob
	/// turn (`ButtonId::Mod0..Mod7`, `EncoderRole::GoldUpper/GoldLower`); anything else goes
	/// straight to the top scene. In particular, a gold knob's own push
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
	/// outright -- not even reaching `mod_knobs` -- when \p claims shows the bit this event maps to
	/// is set: `Encoder{Select}` -> `turn`, `Encoder{GoldUpper}` -> `edit0`,
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
	/// `RepaintAll` sets every surface's flag; `LockInput` calls `InputLowering::set_input_lock`.
	/// @param result The result to apply.
	void apply(const Result& result);

	/// @brief Has a `Repaint`/`RepaintAll` action requested \p role since the last
	///        `clear_repaint_requests()`?
	/// @param role The surface to ask about.
	/// @return True if requested; false for `SurfaceRole::None`, which keeps its "no surface,
	///         never matches" meaning here (`surfaces.h`) and is never itself a requestable slot.
	bool repaint_requested(SurfaceRole role) const;
	/// @brief Clear every surface's repaint flag.
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
	/// @brief The shared chain body: modifiers -> `mod_knobs` -> top scene -> `global_controls` ->
	///        drop, `apply()`d at every step. Used by both `route()` and `route_post_render()`
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
	uint16_t popup_string_ = 0;
	uint8_t popup_type_ = 0;
	uint32_t dropped_ = 0;
};

} // namespace deluge::ui::shell
