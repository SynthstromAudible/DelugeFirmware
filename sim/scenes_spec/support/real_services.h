#pragma once

/// @file
/// @brief The three real `shell::Service`s, built over a script run's own `Shell` and installed
///        into it -- the fixture a level-2 script uses when it wants an *application's* shell
///        rather than an empty one.
///
/// Lifted out of `scene_scripts_spec.cpp` by F1 task 11, unchanged, and **it has one consumer, not
/// two.** That is deliberate and it is the honest state: task 11 wired this fixture into
/// `menu_scripts_spec.cpp`'s twelve gold-turn blocks to carry out the re-record the migration
/// spec's §10.2 row asks of it, measured what happened, and **refused the re-record** -- see that
/// file's `@par Every script here runs with `Services{}`` and F1 task 11's report for the
/// measurement and the reason. The move stays because the next attempt needs exactly this fixture
/// and two copies of "install exactly what the application installs" is the shape that lets the two
/// drift; it is one edit of owed work already done, not a second caller pretending to exist.

#include "scenes/overlay/param_popup_scene.h"
#include "scenes/services/global_controls.h"
#include "scenes/services/midi_learn.h"
#include "scenes/services/mod_knobs.h"
#include "shell/shell.h"

#include <functional>
#include <optional>

namespace deluge::scenes::spec {

/// @brief The three real services, built over the run's own `Shell` from `after_construct` and
///        installed into it with `Shell::set_services`.
///
/// Example-scoped: the services themselves are ordinary objects (unlike every *scene* in this tree,
/// which is a process singleton), so nothing here leaks into the next block. The `Shell` is
/// constructed and destroyed inside `expect_script_goldens`, which this outlives, so nothing here
/// is ever reached through a shell that has gone away.
///
/// @note This used to hold three `LateService` shims -- `Service`s that forwarded every hook to a
///       real one bound in afterwards -- because `Services` was fixed at the `Shell`'s
///       construction and the three real services each need the `SceneStack&` (and, for two of
///       them, the `InputLowering&`) that only exists once that shell does. `Shell::set_services`
///       (F1 task 2) closed that loop for the application, and `scene_scripts_spec.cpp` is the
///       reason it was found; the shim is gone and the 34 golden snapshots there did not move,
///       which is the check that the setter installs exactly what the shim forwarded to.
struct RealServices {
	std::optional<services::MidiLearn> learn;
	std::optional<services::ModKnobs> knobs;
	std::optional<services::GlobalControls> controls;

	/// The song every service is told about, and the timeline counter `ModKnobs` points at. Both
	/// are set by the caller before the run; a null `mod_target` leaves the knobs unpointed, which
	/// is `set_target_with_timeline`'s own "no target" state.
	Song* song = nullptr;
	TimelineCounter* mod_target = nullptr;

	RealServices() = default;
	RealServices(const RealServices&) = delete;
	RealServices& operator=(const RealServices&) = delete;

	/// @return What to hand `expect_script_goldens` as its `Services`: nothing. The shell is built
	///         with empty slots, exactly as an application's is, and `installer()` fills them.
	deluge::ui::shell::Services slots() { return {}; }

	/// @return The `RunOptions::after_construct` hook that builds the real three over @p shell and
	///         installs them into it.
	std::function<void(deluge::ui::shell::Shell&)> installer() {
		return [this](deluge::ui::shell::Shell& shell) {
			// `MidiLearn` first: `GlobalControls` takes it by reference as its `LearnMode`.
			learn.emplace(shell.stack());
			knobs.emplace(overlay::param_popup_scene(), shell.stack(), shell.lowering());
			controls.emplace(shell.stack(), shell.lowering(), *learn);
			learn->set_song(song);
			knobs->set_song(song);
			controls->set_song(song);
			if (mod_target != nullptr) {
				knobs->set_target_with_timeline(mod_target, false);
				knobs->set_mod_region();
			}
			shell.set_services({&*knobs, &*controls, &*learn});
		};
	}
};

} // namespace deluge::scenes::spec
