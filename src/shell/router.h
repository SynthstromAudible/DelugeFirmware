#pragma once
#include "shell/input_lowering.h"
#include "shell/scene.h"
#include "shell/scene_stack.h"
#include "ui/core/frame_context.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace deluge::ui::shell {

/// @brief One thing outside the scene stack that also wants first (or last) look at routed
///        input: the mod-knob bar, the global transport/shift/record handling, MIDI learn.
///
/// The `Service` interface is deliberately the same shape as `Scene::handle`/`leds`/`tick` --
/// it is a scene-shaped participant in the chain that is not itself a scene, because it is not
/// pushed or popped and has no render surface of its own. A minimal service overrides nothing at
/// all: every hook already does nothing and returns "unhandled".
class Service {
public:
	virtual ~Service() = default;

	/// @brief React to one routed event, exactly as `Scene::handle` does.
	/// @param event The event.
	/// @param ctx   The world around it.
	/// @return Whether the event was handled, and what should happen next.
	virtual Result handle(const UiEvent& event, const UiContext& ctx) { return {}; }
	/// @brief Declare this service's LED and knob-indicator states for the tick.
	/// @param frame The tick's LED declarations, composed alongside every scene's.
	virtual void leds(LedFrame& frame) {}
	/// @brief Advance any time-based state.
	/// @param now_ms The tick's wall clock.
	/// @param dt_ms   Milliseconds since this service was last ticked.
	virtual void tick(uint32_t now_ms, uint32_t dt_ms) {}
	/// @brief When this service next needs a `tick()` even with no input.
	/// @return Milliseconds from now, or `std::nullopt` for "no timer".
	virtual std::optional<uint32_t> next_timer() const { return std::nullopt; }
};

/// @brief The "does nothing, claims nothing" `Service` -- the default for every unfilled slot in
///        `Services` until a real implementation exists.
class NullService final : public Service {};

/// @brief The single, shared `NullService` instance every unfilled `Services` slot points at.
///
/// An inline function so the function-local `static` is one instance across every translation
/// unit that includes this header, exactly as an `inline` variable would be, without needing
/// C++17's inline-variable syntax for a class that must stay hidden behind a reference.
/// @return The shared instance.
inline Service& null_service() {
	static NullService instance;
	return instance;
}

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

/// @brief The shell's routing chain: modifiers, then the mod-knob bar, then the top scene, then
///        the global controls, in the order `Buttons::buttonAction` tried them
///        (`hid/buttons.cpp:100-107,134,143-231`).
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

	/// @brief Route one event through the full chain: modifiers -> `mod_knobs` -> top scene ->
	///        `global_controls` -> drop.
	///
	/// `services.mod_knobs->handle` is only consulted when \p event is a mod button, a gold-knob
	/// turn, or a gold-knob's own push (`ButtonId::Mod0..Mod7`, `EncoderRole::GoldUpper/GoldLower`,
	/// `ButtonId::ModEnc0/1`); anything else goes straight to the top scene. Every `Result` along
	/// the way is `apply()`d immediately, whether or not it reports `handled`, and routing stops at
	/// the first `handled` step.
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
	/// for `pending_popup_string()`/`pending_popup_type()`; `Repaint` sets the named surface's flag
	/// (or, when `surface == SurfaceRole::None`, every surface's -- see `repaint_requested()`);
	/// `LockInput` calls `InputLowering::set_input_lock`.
	/// @param result The result to apply.
	void apply(const Result& result);

	/// @brief Has a `Repaint` action requested \p role since the last `clear_repaint_requests()`?
	/// @param role The surface to ask about.
	/// @return True if requested; false for `SurfaceRole::None` itself, which is never a real
	///         surface to request.
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

	/// Real surfaces a `Repaint` action can name -- `Main` through `Aux`; `None` is the "all
	/// surfaces" sentinel and is not itself a slot (see `repaint_requested()`).
	static constexpr size_t kSurfaceCount = static_cast<size_t>(SurfaceRole::Aux) + 1;

	SceneStack& stack_;
	Services services_;
	InputLowering& lowering_;

	bool repaint_[kSurfaceCount]{};
	uint16_t popup_string_ = 0;
	uint8_t popup_type_ = 0;
	uint32_t dropped_ = 0;
};

} // namespace deluge::ui::shell
