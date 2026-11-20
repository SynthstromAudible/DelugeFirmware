#pragma once

/// @file
/// @brief `FakeHostRig`: the menu's context over a fake of every port and a model song of its own, for a spec that
///        drives a menu page with no legacy song and no host behind it.

#include "deluge-project/song.h"
#include "scenes/bind/ports/ports.h"
#include "scenes/menu/context.h"
#include "scenes/services/view_state.h"

#include "fakes.h"

namespace deluge::scenes::spec {

/// @brief A fake of every port (`bind::test::FakeHost`) and a model song, and the contexts a menu reads them through.
///        The ports are the fakes', never the host's (`menu::host_ports()`).
struct FakeHostRig {
	bind::test::FakeHost host; ///< the ports' fakes, which the spec sets and reads back
	project::Song model;       ///< the song a context's model names; empty, as a new song's record is

	FakeHostRig() = default;
	FakeHostRig(const FakeHostRig&) = delete;
	FakeHostRig& operator=(const FakeHostRig&) = delete;

	/// @brief The settings root's context, as `menu::context_for` resolves one with no clip and no song view over the
	///        rig's model and fakes: resolved, rooted at Settings, with no addresses.
	/// @param with_model Whether the context names the rig's model; without it, only what reads no song resolves.
	/// @return The context; its ports point at this rig's fakes, so it is valid while the rig lives.
	[[nodiscard]] menu::MenuContext settings_context(bool with_model = true) {
		const bind::Ports ports = host.ports();
		menu::MenuContext ctx = menu::context_for(model, &ports, services::MenuView{}, /*source_index=*/0,
		                                          /*performance_view=*/false, /*session_or_arranger_root=*/false,
		                                          /*session_view_root=*/false, /*kit_global_fx=*/false);
		if (!with_model) {
			ctx.model = nullptr;
		}
		return ctx;
	}
};

} // namespace deluge::scenes::spec
