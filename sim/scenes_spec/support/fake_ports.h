#pragma once

/// @file
/// @brief `HeldPorts`: the host's ports with the UI and card facts a spec holds in its own hand, for an example that
///        needs a state the host cannot be put into on demand (a sample load running, a popup up, a note auditioned).

#include "scenes/bind/ports/ports.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/context.h"

#include "fakes.h"

namespace deluge::scenes::spec {

/// @brief The host's ports with `ui` and `card` replaced by fakes that start quiet. Every UI and card fact comes from
///        the fakes, the display settings included: `flash_cursor`, `performance_editing_mode` and
///        `dx7_engine_enabled` answer the fake's values, not the host's. Its address is what the ports hold, so it
///        stays where it was declared and outlives every scene that read from it.
struct HeldPorts {
	deluge::scenes::bind::test::FakeUiFacts ui;     ///< answers what the example sets; nothing is auditioned
	deluge::scenes::bind::test::FakeCardFacts card; ///< answers what the example sets; no load runs
	/// @brief Quiet fakes, installed nowhere yet.
	HeldPorts() = default;
	HeldPorts(const HeldPorts&) = delete;
	HeldPorts& operator=(const HeldPorts&) = delete;
	/// @brief Fakes that start quiet, installed into @p ctx at once.
	/// @param ctx The context whose ports become these.
	explicit HeldPorts(deluge::scenes::menu::MenuContext& ctx) { install(ctx); }
	/// @brief The host's ports with the fakes in place of its UI and card facts.
	[[nodiscard]] deluge::scenes::bind::Ports ports() const {
		deluge::scenes::bind::Ports held = deluge::scenes::menu::host_ports();
		held.ui = &ui;
		held.card = &card;
		return held;
	}
	/// @brief Put these ports into @p ctx.
	void install(deluge::scenes::menu::MenuContext& ctx) const { ctx.ports = ports(); }
};

} // namespace deluge::scenes::spec
