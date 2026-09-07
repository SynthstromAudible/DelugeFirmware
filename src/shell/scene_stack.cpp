#include "shell/scene_stack.h"

namespace deluge::ui::shell {

void SceneStack::resume_new_input_top(Scene* before) {
	Scene* const after = &input_top();
	if (after == before || after == &null_) {
		// Same scene on both sides of the mutation: nothing changed hands, so there is no
		// transition to announce. This is the whole of the E4 task 10 fix -- see the header's
		// contract table and design spec §15.7. `null_` is excluded because an empty stack, or one
		// left holding nothing but `input_transparent()` layers, has no input top to resume: the
		// `NullScene` is what `input_top()` reports for "nobody", not a scene that took over.
		return;
	}
	after->on_resume();
}

void SceneStack::save_slot(Scene& scene) {
	const size_t k = static_cast<size_t>(scene.kind());
	if (k >= kMaxSceneKinds) {
		// A scene whose kind is outside the slot array cannot be cached. Registering one is a
		// programming error, but dropping the save is better than writing off the end.
		return;
	}
	scene.save_state(state_slots_[k]);
	slot_valid_[k] = true;
}

bool SceneStack::push(SceneId id) {
	Scene* scene = reg_.find(id.kind);
	if (scene == nullptr || n_ == kStackDepth) {
		// Nothing was touched, so nothing changed. Flagging this would make a caller that retries a
		// refused push every tick -- `Screensaver::tick` against an unregistered `SceneKind::
		// Screensaver` was exactly that -- re-query the greyout and invalidate every gate on every
		// one of those ticks, which is a repaint storm rather than the "one redundant re-query"
		// `changed_since_last_query` budgets for.
		return false;
	}
	changed_ = true;
	// Installed before `on_enter` runs, so a scene can see itself as the top from inside it.
	stack_[n_] = scene;
	ids_[n_] = id;
	n_++;
	if (!scene->on_enter(id)) {
		// Snapshotted with the refused scene still installed, because that is what it held while
		// `on_enter` ran. `openUI`'s rollback resumes `getCurrentUI()` unconditionally
		// (`gui/ui/ui.cpp:236-241`) and is right to for legacy, where every UI on
		// `uiNavigationHierarchy` takes input; here a refused `input_transparent()` push covered
		// nobody, so it hands nothing back.
		Scene* const before = &input_top();
		n_--;
		stack_[n_] = nullptr;
		ids_[n_] = SceneId{};
		resume_new_input_top(before);
		// `changed_` deliberately stays set here, unlike the refusal above: `on_enter` and
		// `on_resume` both ran, so a scene may have moved its greyout mask even though the stack
		// composition ended up where it started.
		return false;
	}
	return true;
}

void SceneStack::pop(SceneKind to) {
	if (n_ == 0) {
		return; // nothing popped, so nothing changed -- see `push`'s note on the refusal path
	}

	// `target` is the lowest index that gets popped: the top for the default, otherwise the
	// topmost scene of the requested kind.
	size_t target = n_ - 1;
	if (to != SceneKind::Null) {
		size_t i = n_;
		while (i > 0 && stack_[i - 1]->kind() != to) {
			i--;
		}
		if (i == 0) {
			return; // no scene of that kind is on the stack; pop nothing, and flag nothing
		}
		target = i - 1;
	}

	changed_ = true;
	Scene* const before = &input_top();
	while (n_ > target) {
		n_--;
		Scene* scene = stack_[n_];
		stack_[n_] = nullptr;
		ids_[n_] = SceneId{};
		scene->on_exit();
	}
	// The *input* top, not `stack_[n_ - 1]`: popping the top of `[A, popup, B]` leaves the popup
	// as the raw top and `A` as the scene that actually takes input again, so `A` is what legacy's
	// `closeUI` -> `focusRegained` (`gui/ui/ui.cpp:194-199`) corresponds to here.
	resume_new_input_top(before);
}

bool SceneStack::remove(SceneKind kind) {
	// Find the topmost scene of `kind`, searching downward from the top -- same search `pop(kind)`
	// does, but here it names the one scene to excise rather than a floor to pop down to.
	size_t i = n_;
	while (i > 0 && stack_[i - 1]->kind() != kind) {
		i--;
	}
	if (i == 0) {
		return false; // no scene of that kind is on the stack; remove nothing, flag nothing
	}
	const size_t idx = i - 1;

	changed_ = true;
	Scene* const before = &input_top();
	Scene* scene = stack_[idx];
	save_slot(*scene);
	scene->on_exit();

	// Close the gap: everything above `idx` shifts down one slot. Everything below `idx` is never
	// touched -- no `on_exit`/`on_enter`/`on_resume` runs on it, exactly as the doc promises.
	for (size_t j = idx; j + 1 < n_; j++) {
		stack_[j] = stack_[j + 1];
		ids_[j] = ids_[j + 1];
	}
	n_--;
	stack_[n_] = nullptr;
	ids_[n_] = SceneId{};

	// Whether the *input* top changed hands, which is not the same question as "was the removed
	// scene the raw top". Both directions matter and both were wrong before E4 task 10:
	//
	//   * Removing an `input_transparent()` layer -- the case this method exists for -- changes
	//     nothing, even though the layer was the raw top. `Notices::tick` retiring an 800 ms popup
	//     used to fire `on_resume` on the scene beneath, and `SlicerScene::on_resume` is
	//     `Slicer::focusRegained` (`gui/ui/slicer.cpp:59-72`), which clears the slice table.
	//   * Removing the top of `[A, popup, B]` hands the input top to `A`, not to the popup, so the
	//     old `stack_[n_ - 1]` notified the one scene that had not changed state.
	resume_new_input_top(before);
	return true;
}

bool SceneStack::replace_root(SceneId id) {
	if (reg_.find(id.kind) == nullptr) {
		// Nothing to replace the stack with, so leave it standing rather than emptying it into a
		// state the caller did not ask for -- and, nothing having moved, flag nothing.
		return false;
	}
	changed_ = true;

	// Top first, so a scene saves before anything beneath it does -- the order `reset()` exits in.
	for (size_t i = n_; i > 0; i--) {
		Scene* scene = stack_[i - 1];
		stack_[i - 1] = nullptr;
		ids_[i - 1] = SceneId{};
		save_slot(*scene);
		scene->on_exit();
	}
	n_ = 0;

	if (!push(id)) {
		return false; // `on_enter` refused; the stack stays empty and `top()` is the NullScene
	}
	const size_t k = static_cast<size_t>(id.kind);
	if (k < kMaxSceneKinds && slot_valid_[k]) {
		// After `on_enter`, so a scene's saved state wins over whatever the entry argument set up.
		stack_[n_ - 1]->restore_state(state_slots_[k]);
	}
	return true;
}

bool SceneStack::replace_top(SceneId id) {
	Scene* scene = reg_.find(id.kind);
	if (scene == nullptr) {
		return false; // untouched stack, unflagged -- see `push`'s note on the refusal path
	}
	if (n_ == 0) {
		return push(id); // which flags it, if it takes
	}
	changed_ = true;

	Scene* previous = stack_[n_ - 1];
	const SceneId previous_id = ids_[n_ - 1];
	previous->on_exit();
	stack_[n_ - 1] = scene;
	ids_[n_ - 1] = id;
	if (!scene->on_enter(id)) {
		// Put the previous scene back and resume it -- **unconditionally**, which is the one arm
		// E4 task 10 deliberately left alone.
		//
		// The rest of the class fires `on_resume` only when the input top actually changed hands,
		// because an unpaired edge cannot mean anything stronger. Here the edge *is* paired:
		// `previous->on_exit()` above really ran, so `on_resume` is that call's dual and not a
		// claim about the top at all. Suppressing it for an `input_transparent()` `previous` would
		// leave a scene that had been told it left the stack with no notice that it is back.
		// `changeUIAtLevel`'s own failure path does the same thing for the same reason
		// (`gui/ui/ui.cpp:96-101`, `oldUI->focusRegained()` after restoring the hierarchy).
		stack_[n_ - 1] = previous;
		ids_[n_ - 1] = previous_id;
		previous->on_resume();
		return false;
	}
	return true;
}

void SceneStack::reset() {
	changed_ = true;
	while (n_ > 0) {
		n_--;
		Scene* scene = stack_[n_];
		stack_[n_] = nullptr;
		ids_[n_] = SceneId{};
		scene->on_exit();
	}
}

Scene& SceneStack::top() {
	return n_ > 0 ? *stack_[n_ - 1] : null_;
}

Scene& SceneStack::input_top() {
	// The walk always terminates on a real scene in practice: the flag exists for a passive layer
	// drawn *above* a base -- a popup, a console line, the working animation -- and a base scene is
	// never input-transparent, so the `NullScene` below is reached only by an empty stack. A stack
	// of nothing but transparent scenes would degrade safely rather than misroute: nothing holds
	// `InputMask::all()`, so every widget-owned event stays unclaimed and reaches `handle()`.
	for (size_t i = n_; i-- > 0;) {
		if (!stack_[i]->input_transparent()) {
			return *stack_[i];
		}
	}
	return null_;
}

Scene* SceneStack::root() {
	return n_ > 0 ? stack_[0] : nullptr;
}

bool SceneStack::changed_since_last_query() {
	const bool was = changed_;
	changed_ = false;
	return was;
}

} // namespace deluge::ui::shell
