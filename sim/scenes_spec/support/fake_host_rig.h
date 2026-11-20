#pragma once

/// @file
/// @brief `FakeHostRig`: the menu's context over a fake of every port and a model song of its own, for a spec that
///        drives a menu page with no legacy song and no host behind it.

#include "deluge-project/instrument_slot.h"
#include "deluge-project/setting_group.h"
#include "deluge-project/song.h"
#include "instruments/deluge/codec.h"
#include "instruments/deluge/deluge_synth.h"
#include "instruments/deluge/sound_state.h"
#include "scenes/bind/ports/ports.h"
#include "scenes/menu/context.h"
#include "scenes/services/view_state.h"

#include "fakes.h"

namespace deluge::scenes::spec {

/// @brief A fake of every port (`bind::test::FakeHost`) and a model song, and the contexts a menu reads them through.
///        The ports are the fakes', never the host's (`menu::host_ports()`).
struct FakeHostRig {
	/// The synth track a synth context edits: its slot, its track and its clip.
	static constexpr project::SlotAddress kSynthSlot{project::SlotAddress::Kind::OUTPUT, 4};
	static constexpr project::TrackAddress kSynthTrack{kSynthSlot.id};
	static constexpr project::ClipAddress kSynthClip{35};

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

	/// @brief Give the synth track @p state: the sound fake answers it, and the model's slot holds it encoded, as a
	///        coherent song holds them.
	/// @param state The synth's state.
	void hold_synth(const instrument::SoundState& state) {
		host.sound.states[kSynthSlot.id] = state;
		model.output_structure.getOrCreate(kSynthSlot.id).instrument =
		    project::InstrumentSlot{.plugin = "deluge.instrument",
		                            .state = plugin::StateHandle::of<instrument::DelugeSynth>(state),
		                            .tick_magnitude = 1};
		(void)model.clip_structure.getOrCreate(kSynthClip.clip_id).values.arp.emplace();
	}

	/// @brief The sound root's context over the synth clip, as `menu::context_for` resolves one opened from a synth
	///        clip's view: the synth's slot, clip and track, on source 0. Gives the synth a new synth's state first
	///        when it has none.
	/// @return The context; its ports point at this rig's fakes, so it is valid while the rig lives.
	[[nodiscard]] menu::MenuContext synth_context() {
		if (!host.sound.states.contains(kSynthSlot.id)) {
			hold_synth(instrument::new_synth_state());
		}
		const bind::Ports ports = host.ports();
		return menu::context_for(
		    model, &ports,
		    services::MenuView{.clip = kSynthClip, .track = kSynthTrack, .kind = project::TrackKind::SYNTH},
		    /*source_index=*/0, /*performance_view=*/false, /*session_or_arranger_root=*/false,
		    /*session_view_root=*/false, /*kit_global_fx=*/false);
	}
};

} // namespace deluge::scenes::spec
