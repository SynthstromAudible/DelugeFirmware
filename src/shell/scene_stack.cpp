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
	changed_ = true;
	Scene* scene = reg_.find(id.kind);
	if (scene == nullptr || n_ == kStackDepth) {
		return false;
	}
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
		return false;
	}
	return true;
}

void SceneStack::pop(SceneKind to) {
	changed_ = true;
	if (n_ == 0) {
		return;
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
			return; // no scene of that kind is on the stack; pop nothing
		}
		target = i - 1;
	}

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

bool SceneStack::replace_root(SceneId id) {
	changed_ = true;
	if (reg_.find(id.kind) == nullptr) {
		// Nothing to replace the stack with, so leave it standing rather than emptying it into a
		// state the caller did not ask for.
		return false;
	}

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
	changed_ = true;
	Scene* scene = reg_.find(id.kind);
	if (scene == nullptr) {
		return false;
	}
	if (n_ == 0) {
		return push(id);
	}

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

Scene* SceneStack::root() {
	return n_ > 0 ? stack_[0] : nullptr;
}

bool SceneStack::changed_since_last_query() {
	const bool was = changed_;
	changed_ = false;
	return was;
}

} // namespace deluge::ui::shell
