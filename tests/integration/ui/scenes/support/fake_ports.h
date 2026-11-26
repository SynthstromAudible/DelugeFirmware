#pragma once

/// @file
/// @brief `HeldPorts` and `CablesOverSound`: the host's ports with the UI and card facts, or a sound's patch cables, a
///        spec holds in its own hand, for an example that needs a state the host cannot be put into on demand (a
///        sample load running, a popup up, a note auditioned, a sound with chosen cables).

#include "gui/ui/shell_host/host_ports.h"
#include "ui/scenes/bind/ports/ports.h"
#include "ui/scenes/menu/context.h"

#include "fakes.h"

#include <optional>

namespace deluge::ui::scenes::spec {

/// @brief The host's ports with `ui` and `card` replaced by fakes that start quiet. Every UI and card fact, the
///        performance editing mode included, is the fake's, not the host's. The device's own settings (the play
///        cursor, the DX7 engine feature) are still read through the host's device port. The ports hold this object's
///        address, so it stays where it was declared and outlives every scene that reads from it.
struct HeldPorts {
	deluge::ui::scenes::bind::test::FakeUiFacts ui;     ///< answers what the example sets; nothing is auditioned
	deluge::ui::scenes::bind::test::FakeCardFacts card; ///< answers what the example sets; no load runs
	/// @brief Quiet fakes, installed nowhere yet.
	HeldPorts() = default;
	HeldPorts(const HeldPorts&) = delete;
	HeldPorts& operator=(const HeldPorts&) = delete;
	/// @brief Fakes that start quiet, installed into @p ctx at once.
	/// @param ctx The context whose ports become these.
	explicit HeldPorts(deluge::ui::scenes::menu::MenuContext& ctx) { install(ctx); }
	/// @brief The host's ports with the fakes in place of its UI and card facts.
	[[nodiscard]] deluge::ui::scenes::bind::Ports ports() const {
		deluge::ui::scenes::bind::Ports held = deluge::gui::shell_host::host_ports();
		held.ui = &ui;
		held.card = &card;
		return held;
	}
	/// @brief Put these ports into @p ctx.
	void install(deluge::ui::scenes::menu::MenuContext& ctx) const { ctx.ports = ports(); }
};

/// @brief The host's sound facts with a sound's patch cables said to be a list the example holds, for a state the host
///        cannot be put into on demand. Every other fact is the host's.
class CablesOverSound final : public deluge::ui::scenes::bind::SoundFacts {
public:
	/// @param real The sound facts every fact but the cables is read from; it must outlive this.
	/// @param cables The cables every sound is said to hold.
	CablesOverSound(const deluge::ui::scenes::bind::SoundFacts& real, deluge::ui::scenes::bind::CableList cables)
	    : real_(real), cables_(cables) {}
	[[nodiscard]] uint8_t loaded_sources(deluge::project::SlotAddress slot) const override {
		return real_.loaded_sources(slot);
	}
	[[nodiscard]] const deluge::instruments::classic::SoundState*
	sound_state(deluge::project::SlotAddress slot) const override {
		return real_.sound_state(slot);
	}
	[[nodiscard]] bool kit_takes_whole_folder(deluge::project::SlotAddress slot) const override {
		return real_.kit_takes_whole_folder(slot);
	}
	[[nodiscard]] std::optional<uint16_t> default_range(deluge::project::SlotAddress slot,
	                                                    uint8_t source) const override {
		return real_.default_range(slot, source);
	}
	[[nodiscard]] std::optional<uint64_t> range_identity(deluge::project::SlotAddress slot, uint8_t source,
	                                                     uint16_t range) const override {
		return real_.range_identity(slot, source, range);
	}
	[[nodiscard]] std::optional<uint16_t>
	take_range(const deluge::ui::scenes::bind::TakeTarget& target) const override {
		return real_.take_range(target);
	}
	[[nodiscard]] std::optional<deluge::ui::scenes::bind::DxCartridgeNames> dx_cartridge() const override {
		return real_.dx_cartridge();
	}
	[[nodiscard]] deluge::ui::scenes::bind::CableList
	cables(const deluge::project::ParamAddress& /*owner*/) const override {
		return cables_;
	}

private:
	const deluge::ui::scenes::bind::SoundFacts& real_;
	deluge::ui::scenes::bind::CableList cables_;
};

} // namespace deluge::ui::scenes::spec
