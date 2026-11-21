#pragma once

/// @file
/// @brief The fixtures and port-side predicates of the sound-root tables: a synth clip, a two-drum kit, and the
///        facts the spec and the menu oracle freezer both need to decide what to ask.

#include "gui/ui/menu_view_of.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/present.h"
#include "scenes/profile.h"

#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"

#include "deluge-project/param_automation.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/project_bridge/patch_cable_words.h"
#include "model/song/song.h"
#include "modulation/patch/patch_cable.h"
#include "processing/sound/sound_drum.h"

#include "support/menu_engine.h"
#include "support/oracle_tree.h"
#include "support/song_fixture.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace deluge::scenes::spec::tables2 {

namespace nodes = deluge::scenes::menu::nodes;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;

/// @brief Make the song, clip and UI mode those @p ctx is read over: both the menu's reads and a reference item's reach
///        the same objects through one fixture.
inline void install(const MenuContext& ctx, Song* song) {
	currentSong = song;
	song->setCurrentClip(menu_engine(ctx).clip);
	currentUIMode = ctx.affect_entire_held ? UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR : 0;
}

inline std::string text_of(uint16_t id) {
	deluge::ui::shell::TextScratch scratch;
	return std::string{deluge_profile().text_or_digits(id, scratch)};
}

/// @brief @p id as @p node shows it: its placeholder filled (`menu::node_text`).
inline std::string shown(uint16_t id, const Node& node, const MenuContext& ctx) {
	menu::NodeText scratch;
	return std::string{menu::node_text(id, node, ctx, scratch)};
}

/// @brief Nodes whose name or title is built at render time (the mod-FX type's, the reverb model's, the morph
///        and source ones), keyed on the node so a shared name id never hides a static one.
inline bool has_dynamic_name(const Node& node) {
	static const std::set<const Node*> kDynamic = {
	    &nodes::lpf_morph,     &nodes::hpf_morph,        &nodes::source0,
	    &nodes::source1,       &nodes::mod_fx_depth,     &nodes::mod_fx_feedback,
	    &nodes::mod_fx_offset, &nodes::reverb_room_size, &nodes::reverb_width,
	};
	return kDynamic.contains(&node);
}

/// @brief Nodes whose write is not driven through a reference item, because each reaches a UI global only a booted
///        app has. Every one has its port hook driven on its own in the writes group.
inline constexpr const Node* kWritesNeedingBootedUi[] = {
    &nodes::synth_mode,
    &nodes::arp_mode,
    &nodes::arp_preset,
    &nodes::mod_fx_type,
};

inline bool write_needs_booted_ui(const Node& node) {
	return std::ranges::find(kWritesNeedingBootedUi, &node) != std::end(kWritesNeedingBootedUi);
}

/// @brief One fixture and the `MenuContext` resolved from it.
struct Rigged {
	spec::SongFixture song;
	MenuContext ctx{};
};

/// Writes @p menu_value to the cable @p learn names through the app's sink, as the strength page writes it; a new
/// cable takes its source's default polarity.
inline bool seed_cable(const menu::MenuContext& ctx, deluge::synth::CableTarget learn, int32_t menu_value) {
	const std::optional<deluge::scenes::bind::WriteRequest> request =
	    menu::nodes::cable_request(ctx, learn, menu_value, PatchCable::getDefaultPolarity(learn.bottom_source()));
	return request.has_value() && ctx.ports.writes->write(*request);
}

/// @brief The synth clip every shape/value example runs against.
inline Rigged& fixture() {
	static Rigged f = [] {
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		Rigged r;
		r.song = spec::make_synth_song();
		r.ctx = menu::context_for(*r.song.song, deluge::gui::menuViewOf(r.song.clip), 0, false, false, false,
		                          /*kit_global_fx=*/false);
		return r;
	}();
	install(f.ctx, f.song.song);
	return f;
}

/// @brief A kit with **two** `SoundDrum`s, the first selected.
///
/// Needed for two things the synth clip cannot show: the kit-only relevance predicates (against one synth context
/// most of them agree vacuously), and every affect-entire sweep, which by definition needs more than one drum to be
/// observable.
struct KitRig {
	spec::SongFixture song;
	SoundDrum* drum_a = nullptr;
	SoundDrum* drum_b = nullptr;
	MenuContext ctx{};
};

inline KitRig& kit_fixture() {
	static KitRig k = [] {
		KitRig r;
		r.song = spec::make_kit_song();
		r.drum_a = spec::add_drum<SoundDrum>(r.song);
		r.drum_b = spec::add_drum<SoundDrum>(r.song);
		static_cast<Kit*>(r.song.instrument)->selectedDrum = r.drum_a;
		r.ctx = menu::context_for(*r.song.song, deluge::gui::menuViewOf(r.song.clip), 0, false, false, false,
		                          /*kit_global_fx=*/false);
		return r;
	}();
	return k;
}

/// @brief Re-resolve the kit context with @p drum selected, and install it.
inline MenuContext select_drum(KitRig& k, Drum* drum, bool affect_entire_held = false) {
	static_cast<Kit*>(k.song.instrument)->selectedDrum = drum;
	currentSong = k.song.song; // a context binds the app's facts and sink over the current song only
	MenuContext ctx = menu::context_for(*k.song.song, deluge::gui::menuViewOf(k.song.clip), 0, false, false, false,
	                                    /*kit_global_fx=*/false);
	ctx.affect_entire_held = affect_entire_held;
	ctx.addresses.affect_entire = menu::bindings::writes_whole_kit(ctx); // as the menu scene sets it
	install(ctx, k.song.song);
	return ctx;
}

/// @brief A node pointer as a name the failure message can print. Compared instead of the raw
///        pointers so a mismatch says *which* page each side chose.
inline std::string chain_name(const Node* n) {
	if (n == nullptr) {
		return "<ascend>";
	}
	// The four arp pages share their base's `name`/`title`, so the identity has to come from the
	// pointer, not from the text.
	if (n == &nodes::arp_octave_mode_to_note_mode) {
		return "arp_octave_mode_to_note_mode";
	}
	if (n == &nodes::arp_octave_mode_to_note_mode_drums) {
		return "arp_octave_mode_to_note_mode_drums";
	}
	if (n == &nodes::arp_note_mode_from_octave_mode) {
		return "arp_note_mode_from_octave_mode";
	}
	if (n == &nodes::arp_note_mode_from_octave_mode_drums) {
		return "arp_note_mode_from_octave_mode_drums";
	}
	if (n == &nodes::unison_spread) {
		return "unison_spread";
	}
	if (n == &nodes::voice_count) {
		return "voice_count";
	}
	// The four patch-cable pages come in same-named pairs (`source_selection_regular` and
	// `patch_cable_strength_regular` both say "Modulate with"), so these too must come from the
	// pointer rather than from the text.
	if (n == &nodes::source_selection_regular) {
		return "source_selection_regular";
	}
	if (n == &nodes::source_selection_range) {
		return "source_selection_range";
	}
	if (n == &nodes::patch_cable_strength_regular) {
		return "patch_cable_strength_regular";
	}
	if (n == &nodes::patch_cable_strength_range) {
		return "patch_cable_strength_range";
	}
	return "UNMAPPED:" + text_of(n->name);
}

/// @brief Does @p node's `chain_targets` span name @p target?
///
/// The half of the chain contract `validate_tree` cannot check: `Flag::ChainTargetsMismatch` tests
/// only that `select_chain` and `chain_targets` are both set or neither is, because the hook is
/// opaque to a static walk. A hook returning a node absent from its own span produces a page with
/// no id -- unaddressable by `SceneId::arg`, unvalidated, and missing from the dump -- with nothing
/// to notice. Every answer a spec actually exercises is checked against the span here.
inline bool chain_targets_contain(const Node& node, const Node* target) {
	return std::find(node.chain_targets.begin(), node.chain_targets.end(), target) != node.chain_targets.end();
}

} // namespace deluge::scenes::spec::tables2
