#pragma once

/// @file
/// @brief The fixtures the shortcut layout spec and the menu oracle freezer share: the synth song the shortcut tables
/// are
///        read over, the cables the source blinks find on it, and the names that make two blink plans compare as text.

#include "effects/reverb.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/menu_view_of.h"
#include "processing/engines/audio_engine.h"
#include "scenes/bind/write_sink.h"
#include "scenes/menu/context.h"
#include "scenes/menu/nodes/patch_cables.h"
#include "scenes/profile.h"

#include "model/clip/instrument_clip.h"
#include "model/song/song.h"
#include "modulation/patch/patch_cable.h"

#include "support/menu_engine.h"
#include "support/song_fixture.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace deluge::scenes::spec::shortcuts {

/// @brief The reverb at its original model for the scope, put back after. The reverb's width and room-size items name
///        themselves by the active model (Diffusion and Time under MUTABLE and DIGITAL); the port's nodes carry the
///        model-independent names, so the layouts are read at FREEVERB.
struct FreeverbModel {
	deluge::effects::ReverbModel saved = AudioEngine::reverb.model();
	FreeverbModel() { AudioEngine::setReverbModel(deluge::effects::ReverbModel::FREEVERB); }
	~FreeverbModel() { AudioEngine::setReverbModel(saved); }
	FreeverbModel(const FreeverbModel&) = delete;
	FreeverbModel& operator=(const FreeverbModel&) = delete;
};

inline void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
}

/// @brief The English text of string id @p id.
inline std::string text_of(uint16_t id) {
	deluge::ui::shell::TextScratch scratch;
	return std::string{deluge_profile().text_or_digits(id, scratch)};
}

/// @brief Install a real synth clip as the context the shortcut tables are read over, and pick a language. Several
/// items
///        build their names out of live state (the filter items read the filter mode of the sound being edited), so
///        nothing may be asked before this has run. The fixture is built once and never torn down.
inline const menu::MenuContext& install_sound_context() {
	select_language();
	static SongFixture fixture = make_synth_song();
	static const menu::MenuContext ctx =
	    menu::context_for(*fixture.song, deluge::gui::menuViewOf(fixture.clip), 0, false, false, false,
	                      /*kit_global_fx=*/false);
	currentSong = fixture.song;
	fixture.song->setCurrentClip(menu_engine(ctx).clip);
	return ctx;
}

/// @brief A flash colour byte as the grid names it.
inline std::string colour_name(uint8_t colour) {
	switch (colour) {
	case 0:
		return "white";
	case 0b11:
		return "yellow";
	case 0b110:
		return "palette6";
	default:
		return "colour" + std::to_string(colour);
	}
}

/// @brief @p paths joined by spaces, as the rows name an item.
inline std::string join_paths(const std::vector<std::string>& paths) {
	std::string out;
	for (const std::string& p : paths) {
		out += (out.empty() ? "" : " ") + p;
	}
	return out;
}

/// @brief The paths of @p paths that lie in the tree of the root @p label, joined; `outside:` and all of them where
///        none does, so a page outside the root's tree shows in a row rather than matching by accident.
///
/// A page is one object the port shares across the roots whose menus list it, where `main` has one per root family
/// (the sound's EQ page and the global one are the same port node). A landing is the page the pad's own root opens, so
/// it is named by where that page sits in that root's tree.
inline std::string paths_in_root(const std::vector<std::string>& paths, std::string_view label) {
	std::vector<std::string> own;
	for (const std::string& p : paths) {
		if (p == label || (p.size() > label.size() && p.starts_with(label) && p[label.size()] == '/')) {
			own.push_back(p);
		}
	}
	return own.empty() ? "outside:" + join_paths(paths) : join_paths(own);
}

/// @brief Write a cable through the app's sink, as the strength page writes one.
inline bool seed_cable(const menu::MenuContext& ctx, deluge::synth::CableTarget learn, int32_t menu_value) {
	const std::optional<deluge::scenes::bind::WriteRequest> request =
	    menu::nodes::cable_request(ctx, learn, menu_value, PatchCable::getDefaultPolarity(learn.bottom_source()));
	return request.has_value() && ctx.ports.writes->write(*request);
}

/// @brief Seed, once, cables on the fixture's sound for the source blinks to find: both layers of one pad pair, a
/// volume
///        cable the volume triple folds, a cable on a second-layer param, and one on another cable's depth.
inline bool seed_blink_cables(const menu::MenuContext& ctx) {
	namespace p = deluge::modulation::params;
	using deluge::synth::CableTarget;
	static bool seeded = false;
	if (seeded) {
		return true;
	}
	seeded = true;
	bool ok = true;
	ok = seed_cable(ctx, CableTarget::depth_of(p::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_1), 1000) && ok;
	ok = seed_cable(ctx, CableTarget::depth_of(p::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_3), 1000) && ok;
	ok = seed_cable(ctx, CableTarget::depth_of(p::LOCAL_LPF_FREQ, PatchSource::LFO_GLOBAL_1), 900) && ok;
	ok = seed_cable(ctx, CableTarget::depth_of(p::LOCAL_VOLUME, PatchSource::AFTERTOUCH), 800) && ok;
	ok = seed_cable(ctx, CableTarget::depth_of(p::LOCAL_ENV_2_ATTACK, PatchSource::RANDOM), 700) && ok;
	ok = seed_cable(ctx,
	                CableTarget::depth_of_depth(p::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_1, PatchSource::LFO_LOCAL_1),
	                600)
	     && ok;
	return ok;
}

} // namespace deluge::scenes::spec::shortcuts
