#pragma once

/// @file
/// @brief `FakeHostRig`: the menu's context over a fake of every port and a model song of its own, for a spec that
///        drives a menu page with no legacy song and no host behind it.

#include "instruments/classic/deluge_synth.h"
#include "instruments/classic/sound_state.h"
#include "project/instrument_slot.h"
#include "project/setting_group.h"
#include "project/song.h"
#include "ui/scenes/bind/ports/ports.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/services/view_state.h"

#include "fakes.h"

#include <cstdint>

namespace deluge::ui::scenes::spec {

/// @brief A fake of every port (`bind::test::FakeHost`) and a model song, and the contexts a menu reads them through.
///        The ports are the fakes', never the host's (`deluge::gui::shell_host::host_ports()`).
struct FakeHostRig {
	/// The synth output's durable id. Any id works: the rig's song holds no other output. It is the bind specs'
	/// `ModelFixture` synth's, so a case reads the same on either rig.
	static constexpr uint32_t kSynthOutputId = 4;
	/// The synth clip's durable id; any id no other clip of the rig's song takes, and `ModelFixture`'s synth clip's.
	static constexpr uint32_t kSynthClipId = 35;
	/// The synth track a synth context edits: its slot (the output's own instrument), its track and its clip.
	/// @{
	static constexpr project::SlotAddress kSynthSlot{project::SlotAddress::Kind::OUTPUT, kSynthOutputId};
	static constexpr project::TrackAddress kSynthTrack{kSynthOutputId};
	static constexpr project::ClipAddress kSynthClip{kSynthClipId};
	/// @}

	bind::test::FakeHost host; ///< the ports' fakes, which the spec sets and reads back
	/// The song a context's model names; empty, as a new song's record is, until a synth context adds the synth.
	project::Song model;

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

	/// @brief Give the synth track @p state, in both places a sound target reads it.
	///
	/// - The sound fake answers it: relevance and the hook-bound nodes read `SoundFacts::sound_state`.
	/// - The model's slot holds it encoded: a SOUND, EFFECT or PARAM target reads the slot (`bind::SlotReader`). The
	///   slot keeps the default tick magnitude, a new song's, so its sync levels read as written.
	///
	/// The clip gets no arp record: the sound root's arpeggiator page lists the same rows without one.
	/// @param state The synth's state.
	void hold_synth(const instruments::classic::SoundState& state) {
		host.sound.states[kSynthSlot.id] = state;
		model.output_structure.getOrCreate(kSynthSlot.id).instrument = project::InstrumentSlot{
		    .plugin = "deluge.instrument", .state = plugin::StateHandle::of<instruments::classic::DelugeSynth>(state)};
	}

	/// @brief The sound root's context over the synth clip, as `menu::context_for` resolves one opened from a synth
	///        clip's view: the synth's slot, clip and track, on source 0. Gives the synth a new synth's state first
	///        when it has none.
	/// @return The context; its ports point at this rig's fakes, so it is valid while the rig lives.
	[[nodiscard]] menu::MenuContext synth_context() {
		if (!host.sound.states.contains(kSynthSlot.id)) {
			hold_synth(instruments::classic::SoundState{}); // a default state is a new synth's
		}
		const bind::Ports ports = host.ports();
		return menu::context_for(
		    model, &ports,
		    services::MenuView{.clip = kSynthClip, .track = kSynthTrack, .kind = project::TrackKind::SYNTH},
		    /*source_index=*/0, /*performance_view=*/false, /*session_or_arranger_root=*/false,
		    /*session_view_root=*/false, /*kit_global_fx=*/false);
	}
};

} // namespace deluge::ui::scenes::spec
