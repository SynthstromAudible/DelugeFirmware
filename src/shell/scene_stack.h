#pragma once
#include "shell/scene.h"

#include <array>
#include <cstddef>
#include <span>

namespace deluge::ui::shell {

/// @brief The scene `SceneStack::top()` reports when the stack is empty.
///
/// It draws nothing and swallows every event, which is what removes the legacy "remember the last
/// UI before nullifying so the null check has something to fall back on" hack: the boot window and
/// the moment after `reset()` are ordinary scenes rather than a null pointer every caller has to
/// test for.
class NullScene final : public Scene {
public:
	SceneKind kind() const override { return SceneKind::Null; }
	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }
};

/// @brief The stack of scenes the shell renders and routes input through, root first.
///
/// Holds pointers to registry-owned scenes and per-`SceneKind` state slots; it allocates nothing
/// and owns nothing but its own `NullScene`. Every operation is described in terms of the scene
/// lifecycle hooks it fires, in the order it fires them:
///
/// | operation | lifecycle, in order |
/// |---|---|
/// | `push(id)` | `on_enter(id)`; a refusal rolls back and resumes the new input top, if it moved |
/// | `pop(kind)` | `on_exit()` top-down through `kind`, then `on_resume()` on the new input top, if it moved |
/// | `remove(kind)` | `save_state()` then `on_exit()` on that scene alone, wherever it sits; `on_resume()` on the new
/// input top, if it moved | | `replace_root(id)` | per scene, top first: `save_state()` then `on_exit()`; `push(id)`;
/// `restore_state()` | | `replace_top(id)` | `on_exit()` on the top, `on_enter(id)` on the replacement; no state save;
/// a refusal restores and unconditionally resumes the scene it exited | | `reset()` | `on_exit()` on every scene, top
/// first |
///
/// @par What "if it moved" means
///
/// `on_resume()` asserts that this scene is the one input reaches again, so the three arms above
/// fire it only when `input_top()` -- not `top()` -- is a different scene after the mutation than
/// it was before. Two consequences, and both were defects until E4 task 10 (design spec §15.7):
/// removing an `input_transparent()` layer notifies **nobody**, because `input_top()` already
/// skipped it (`SceneStack::input_top`); and removing the top of `[A, popup, B]` notifies `A`, the
/// scene that genuinely takes input again, rather than the popup that happens to sit highest.
/// `replace_top`'s refusal path is the deliberate exception -- see the note at that call site.
///
/// @warning Not reentrant. A lifecycle hook must not call back into the stack; scenes ask for
///          navigation by returning `Action`s, which the shell applies between calls.
class SceneStack {
public:
	/// @brief Build an empty stack over \p registry.
	/// @param registry The scene table, held by reference: it must outlive the stack, and a
	///                 registry whose span is repointed afterwards is seen by the stack.
	explicit SceneStack(const SceneRegistry& registry) : reg_(registry) {}

	/// @name Operations
	/// Every one of these marks the stack changed, so the shell re-queries greyout and the
	/// composition gates on its next tick.
	/// @{
	/// @brief Push \p id on top of the stack.
	/// @param id What to push; `id.kind` is looked up in the registry.
	/// @return False when the kind is unregistered, the stack is already `kStackDepth` deep, or
	///         the scene's `on_enter` refused -- in which case the push is rolled back, leaving the
	///         stack exactly as it was, and the input top is resumed if the refused scene had taken
	///         it (it is installed before `on_enter` runs, so an opaque one had).
	bool push(SceneId id);

	/// @brief Pop scenes off the top.
	/// @param to Pop down to and including the topmost scene of this kind. The default,
	///           `SceneKind::Null`, means "just the top". A kind that is not on the stack pops
	///           nothing.
	void pop(SceneKind to = SceneKind::Null);

	/// @brief Excise the topmost scene of \p kind, wherever it sits, without touching anything
	///        else on the stack.
	///
	/// `pop(kind)` and `remove(kind)` answer different questions. `pop(kind)` means "this kind, and
	/// everything that was stacked above it, should go" -- the right call when what is above was
	/// legitimately built on top of that kind and should leave with it (`Screensaver::note_activity`
	/// waking the device takes down whatever the user pushed over the idle animation, on purpose).
	/// `remove(kind)` means "just this one scene should go, whatever else is on the stack" -- the
	/// right call for a passive, `input_transparent()` layer such as a popup: because it never took
	/// input, an application `Push` can legitimately land *above* it, and that application scene must
	/// not be collateral damage when the popup's own timeout, or an explicit cancel, decides the
	/// popup itself is done.
	///
	/// Only the target scene's lifecycle runs: `save_state()` then `on_exit()`, in that order, on
	/// that scene alone -- every scene above and below keeps its place, and none of them is exited
	/// or entered by this call. The one other hook that can fire is `on_resume()`, on the new
	/// `input_top()` and only when removing \p kind's scene changed which scene that is. Removing
	/// a passive layer therefore notifies nobody at all, which is the case this method exists for:
	/// the layer never held the input top, so nothing beneath it lost or regained anything.
	/// @note This is stricter than "was it the top". `remove` used to resume `stack_[n_ - 1]`
	///       whenever the removed scene sat highest, which both fired on a popup's own timeout --
	///       wiping the slicer's slice table 800 ms after a pad tap -- and, for `[A, popup, B]`,
	///       notified the popup instead of `A`. See design spec §15.7.
	/// @param kind The kind to remove the topmost instance of.
	/// @return False, touching nothing, if no scene of \p kind is on the stack.
	bool remove(SceneKind kind);

	/// @brief Save every scene's state, empty the stack, and make \p id the new root.
	///
	/// The state save is what makes leaving and coming back to a root scene cheap: each scene's
	/// `save_state` is written to its `SceneKind`'s slot on the way out, and a later
	/// `replace_root` back to that kind restores the slot after `on_enter` has run.
	///
	/// The stack is unwound one scene at a time, top first, and each scene's `save_state()` runs
	/// immediately before its own `on_exit()` -- interleaved per scene, not two passes over the
	/// whole stack. A scene therefore saves while it is still the state it was in when it lost
	/// the stack, before anything beneath it has been told it is leaving.
	///
	/// @note The slots are keyed by `SceneKind` alone, so a `replace_root` to a kind that already
	///       has a saved slot restores that slot **whatever `id.arg` says** -- coming back to a
	///       kind with a different argument restores the kind's last saved state and leaves it to
	///       `on_enter(id)` (which runs first) to have re-targeted the scene at the new argument.
	///       A scene whose state is argument-specific must therefore either re-derive it in
	///       `restore_state` or not save it at all.
	/// @param id The new root.
	/// @return False if the new root could not be entered. An unregistered kind is rejected before
	///         anything is torn down and leaves the stack exactly as it was; a refusal from
	///         `on_enter` comes after the old stack has already been saved and exited, and leaves
	///         the stack empty with `top()` the `NullScene`.
	bool replace_root(SceneId id);

	/// @brief Swap the top scene for \p id without saving state.
	///
	/// The sideways move between peer scenes. On an empty stack this is a plain `push`.
	/// @param id The replacement.
	/// @return False when the kind is unregistered (the stack is untouched) or the replacement's
	///         `on_enter` refused -- in which case the previous top is put back and resumed,
	///         unconditionally, because it is the one scene here that was actually exited.
	bool replace_top(SceneId id);

	/// @brief Exit every scene, top first, and empty the stack.
	///
	/// No state is saved. `top()` becomes the `NullScene`.
	void reset();
	/// @}

	/// @name Inspection
	/// @{
	/// @brief The scene that draws last.
	/// @return The top scene, or the `NullScene` when the stack is empty.
	Scene& top();
	/// @brief The scene input is routed to first: the topmost one that is not
	///        `Scene::input_transparent()`.
	///
	/// A passive layer -- a popup, a console line, the working animation -- is drawn over the
	/// scene beneath it without ever taking its input, so `top()` is the wrong scene to hand an
	/// event to whenever one of those is up. This walks down past them; a stack of nothing but
	/// input-transparent scenes has no input top and reports the `NullScene`, which swallows
	/// everything, exactly as an empty stack does.
	/// @return The input-frontmost scene, or the `NullScene` when there is none.
	Scene& input_top();
	/// @brief The bottom scene.
	/// @return The root scene, or null when the stack is empty.
	Scene* root();
	/// @brief How many scenes are on the stack.
	size_t depth() const { return n_; }
	/// @brief The whole stack, root first, for a render pass that walks it bottom-up.
	std::span<Scene* const> scenes() const { return std::span<Scene* const>{stack_.data(), n_}; }
	/// @brief The id a stacked scene was entered with.
	/// @param i Index from the root (0) to the top (`depth() - 1`).
	/// @return The id, or a default-constructed `SceneId` when \p i is out of range.
	SceneId id_at(size_t i) const { return i < n_ ? ids_[i] : SceneId{}; }
	/// @}

	/// @brief Has the stack changed since this was last called, and clear the flag.
	///
	/// The shell polls this once a tick and, when it is set, re-queries `greyout_mask()` and the
	/// composition gates and invalidates every surface.
	///
	/// @note Deliberately conservative *among operations that did something*: an operation that
	///       ran a lifecycle hook sets the flag even if the stack ended up where it started (a
	///       push whose `on_enter` refused still ran `on_enter` and `on_resume`, either of which
	///       may have moved a greyout mask). The cost of that false positive is one redundant
	///       re-query; the cost of a false negative is a stale greyout.
	/// @note An operation that touched **nothing** -- a push of an unregistered kind or onto a
	///       full stack, a `pop` for a kind that is not stacked, a `replace_root`/`replace_top`
	///       of an unregistered kind -- deliberately does *not* set the flag. A caller that
	///       retries such a call every tick would otherwise make `Shell::tick` re-query the
	///       greyout and `request_repaint()` every gate every tick, which is a repaint storm
	///       rather than one redundant re-query.
	/// @return True if any operation that touched the stack or ran a lifecycle hook has run since
	///         the last call.
	bool changed_since_last_query();

private:
	/// @brief Save \p scene's state into its kind's slot and mark the slot valid.
	void save_slot(Scene& scene);

	/// @brief Resume the current `input_top()`, but only if it is not \p before.
	///
	/// The one place `on_resume` is decided for `push`'s rollback, `pop` and `remove`. A caller
	/// snapshots `&input_top()` immediately before its mutation and hands it here immediately
	/// after; the call happens only when the two differ, and never on the `NullScene`, which is
	/// what `input_top()` reports for "no scene takes input" rather than a scene that took over.
	///
	/// @note The comparison is scene identity, not stack position, and the registry holds one
	///       `Scene` per kind -- so a kind stacked *twice* is one object at two indices, and
	///       popping the upper one reads as "no transition" even though that object was exited.
	///       Nothing in the shell builds such a stack, and no lifecycle hook is coherent under one
	///       (the scene takes `on_enter` twice with no `on_exit` between, and both indices share a
	///       single `SceneState` slot); refusing it in `push` would be the fix, and is not this
	///       method's business.
	/// @param before The input top as it was before the mutation.
	void resume_new_input_top(Scene* before);

	const SceneRegistry& reg_;
	std::array<Scene*, kStackDepth> stack_{};
	std::array<SceneId, kStackDepth> ids_{};
	size_t n_ = 0;
	std::array<SceneState, kMaxSceneKinds> state_slots_{};
	std::array<bool, kMaxSceneKinds> slot_valid_{};
	NullScene null_;
	bool changed_ = false;
};

} // namespace deluge::ui::shell
