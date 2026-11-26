#pragma once

/// @file
/// @brief The three real `shell::Service`s, built over a script run's own `Shell` and installed
///        into it -- the fixture a level-2 script uses when it wants an *application's* shell
///        rather than an empty one.
///
/// Shared by `scene_scripts_spec.cpp` and `menu_scripts_spec.cpp` (see that file's
/// `@par The twelve gold-turn scripts install the real three, UNSEEDED`), because two copies of
/// "install exactly what the application installs" is the shape that lets the two drift.

#include "board_controls.hpp"
#include "gui/ui/shell_host/host_ports.h"
#include "ui/scenes/overlay/param_popup_scene.h"
#include "ui/scenes/services/global_controls.h"
#include "ui/scenes/services/midi_learn.h"
#include "ui/scenes/services/mod_knobs.h"

#include "gui/views/knob_focus_from_stack.h"
#include "gui/views/view.h"
#include "model/song/song.h"
#include "ui/shell/shell.h"

#include <functional>
#include <optional>

namespace deluge::ui::scenes::spec {

/// @brief The three real services, built over the run's own `Shell` from `after_construct` and
///        installed into it with `Shell::set_services`.
///
/// Example-scoped: the services themselves are ordinary objects (unlike every *scene*,
/// which is a process singleton), so nothing here leaks into the next block. The `Shell` is
/// constructed and destroyed inside `expect_script_goldens`, which this outlives, so nothing here
/// is ever reached through a shell that has gone away.
///
/// @note The services are installed after construction because the three real services each need
///       the `SceneStack&` (and, for two of them, the `InputLowering&`) that only exists once the
///       shell does; `Shell::set_services` is how an application installs them too.
struct RealServices {
	std::optional<services::MidiLearn> learn;
	std::optional<services::ModKnobs> knobs;
	std::optional<services::GlobalControls> controls;

	/// The song every service is told about, and the timeline counter `ModKnobs` points at (its focus as legacy's
	/// view resolves it). Both are set by the caller before the run; a null `mod_target` leaves the knobs unpointed.
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
			knobs.emplace(overlay::param_popup_scene(), shell.stack(), shell.lowering(), kDelugeControls);
			controls.emplace(shell.stack(), shell.lowering(), *learn);
			learn->set_ports(&deluge::gui::shell_host::host_ports());
			learn->set_model(song != nullptr ? &song->project : nullptr);
			knobs->set_ports(&deluge::gui::shell_host::host_ports());
			knobs->set_model(song != nullptr ? &song->project : nullptr);
			controls->set_ports(&deluge::gui::shell_host::host_ports());
			controls->set_model(song != nullptr ? &song->project : nullptr);
			if (mod_target != nullptr && song != nullptr) {
				// Where legacy's view points the knobs at the clip, as the guest seeds them.
				view.setActiveModControllableTimelineCounter(mod_target, false);
				knobs->use_focus(view.knobFocus(), [] { view.knobFocusChanged(); });
			}
			shell.set_services({&*knobs, &*controls, &*learn});
		};
	}
};

} // namespace deluge::ui::scenes::spec
