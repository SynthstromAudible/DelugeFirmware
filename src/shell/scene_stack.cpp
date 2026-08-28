#include "shell/scene_stack.h"

namespace deluge::ui::shell {

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
		n_--;
		stack_[n_] = nullptr;
		ids_[n_] = SceneId{};
		if (n_ > 0) {
			// The previous top was never told it lost focus, but it is told it got it back --
			// this is the legacy `openUI` rollback, which resumes unconditionally.
			stack_[n_ - 1]->on_resume();
		}
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
	while (n_ > target) {
		n_--;
		Scene* scene = stack_[n_];
		stack_[n_] = nullptr;
		ids_[n_] = SceneId{};
		scene->on_exit();
	}
	if (n_ > 0) {
		stack_[n_ - 1]->on_resume();
	}
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
	const bool was_top = idx == n_ - 1;

	changed_ = true;
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

	if (was_top && n_ > 0) {
		// The removed scene was the top, so the scene now exposed needs the same "you're back on
		// top" notice `pop()` gives it. A scene that was already below something else, above the
		// removed one, never lost or regained the top and gets no lifecycle call at all.
		stack_[n_ - 1]->on_resume();
	}
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
		// Same rollback shape as `push`: put the previous scene back and resume it. It has already
		// been exited, so `on_resume` is the only notification it gets that it is top again.
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
