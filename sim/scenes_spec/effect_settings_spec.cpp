// The effect menus on targets, on every owner that lists them: each root lists exactly its effect rows as EFFECT
// targets, and every one that applies reads, ranges and writes through the binding library and the app's sink, landing
// on the owner the root names.

#include "scenes/bind/dispatch.h"
#include "scenes/bind/write_sink.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/nodes.h"

#include "engine-api/types/setting.h"
#include "engines/effects/settings.h"
#include "model/global_effectable/global_effectable.h"
#include "model/instrument/kit.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <array>
#include <set>
#include <string>
#include <vector>

using namespace deluge::scenes;
using deluge::scenes::bind::Target;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;

namespace {

/// Every node reachable from @p node, children and chain targets alike, each once.
void collect(const Node& node, std::set<const Node*>& seen, std::vector<const Node*>& out) {
	if (!seen.insert(&node).second) {
		return;
	}
	out.push_back(&node);
	for (const Node* child : node.children) {
		if (child != nullptr) {
			collect(*child, seen, out);
		}
	}
	for (const Node* target : node.chain_targets) {
		if (target != nullptr) {
			collect(*target, seen, out);
		}
	}
}

/// The EFFECT target nodes reachable from @p root.
std::vector<const Node*> effect_nodes(const Node& root) {
	std::set<const Node*> seen;
	std::vector<const Node*> all;
	collect(root, seen, all);
	std::vector<const Node*> effects;
	for (const Node* node : all) {
		if (node->bind.target.kind == Target::Kind::EFFECT) {
			effects.push_back(node);
		}
	}
	return effects;
}

std::set<std::string> paths_of(const Node& root) {
	std::set<std::string> paths;
	for (const Node* node : effect_nodes(root)) {
		paths.insert(std::string(node->bind.target.path));
	}
	return paths;
}

/// The effects of the owner @p ctx edits: its sound's, else its chain's.
const deluge::effects::EffectStates* owner_effects(const MenuContext& ctx) {
	if (ctx.sound != nullptr) {
		return &ctx.sound->effects();
	}
	::GlobalEffectable* owner =
	    ctx.addresses.chain ? deluge::bridge::chainOwnerAt(*ctx.addresses.chain, *ctx.song) : nullptr;
	return owner != nullptr ? &owner->effects() : nullptr;
}

/// Where an EFFECT node disagrees with the binding library on @p ctx: its read, its range, or a write at its ends and
/// middle that does not land on the context's owner and read back. Empty when it agrees.
std::string disagreements(const Node& node, const MenuContext& ctx) {
	const std::string path(node.bind.target.path);
	const auto shown = bind::view(node.bind.target, *ctx.model, ctx.addresses, *ctx.facts);
	if (!shown) {
		return path + " unresolved ";
	}
	std::string found;
	if (menu::bindings::read(node, ctx) != shown->value) {
		found += path + " read ";
	}
	const auto range = menu::bindings::range(node, ctx);
	if (range.min != shown->least || range.max != shown->greatest) {
		found += path + " range ";
	}
	if (!shown->applies || shown->stale) {
		return found;
	}
	const deluge::engine::SettingInfo& row = *deluge::engine::find_setting(deluge::effects::kEffectSettings, path);
	const std::array<int32_t, 3> probes{shown->least, shown->greatest,
	                                    shown->least + (shown->greatest - shown->least) / 2};
	for (const int32_t v : probes) {
		const auto request = bind::request_for(node.bind.target, *ctx.model, ctx.addresses, *ctx.facts, v);
		const auto stored = request ? deluge::engine::from_menu(row, request->menu_value) : std::nullopt;
		// What the menu then shows: the position written, or for a number what the stored value reads as (a
		// compressor knob's top step stores what reads as the step below, as legacy's shows on re-entry).
		const int32_t shows = row.kind == deluge::engine::SettingKind::CHOICE || !stored
		                          ? v
		                          : deluge::engine::to_menu(row, stored->first, stored->second);
		menu::bindings::write(node, ctx, v);
		if (menu::bindings::read(node, ctx) != shows) {
			found +=
			    path + " write(" + std::to_string(v) + ")->" + std::to_string(menu::bindings::read(node, ctx)) + " ";
		}
		const auto* effects = owner_effects(ctx);
		if (!stored || effects == nullptr || deluge::effects::held_effect_setting(*effects, path) != stored->first) {
			found += path + " owner(" + std::to_string(v) + ") ";
		}
	}
	menu::bindings::write(node, ctx, shown->value);
	return found;
}

std::string all_disagreements(const Node& root, const MenuContext& ctx) {
	std::string found;
	for (const Node* node : effect_nodes(root)) {
		found += disagreements(*node, ctx);
	}
	return found;
}

const std::set<std::string> kChainPaths{"lpfMode",         "hpfMode",    "filterRoute", "delay/pingPong",
                                        "delay/syncLevel", "delay/type", "modFXType"};
const std::set<std::string> kCompressorAndSidechain{
    "audioCompressor/attack",    "audioCompressor/release", "audioCompressor/ratio", "audioCompressor/compHPF",
    "audioCompressor/compBlend", "sidechain/attack",        "sidechain/release",     "sidechain/syncLevel"};

std::set<std::string> joined(std::initializer_list<std::set<std::string>> sets) {
	std::set<std::string> out;
	for (const auto& set : sets) {
		out.insert(set.begin(), set.end());
	}
	return out;
}

} // namespace

// clang-format off
describe effect_settings_targets("the effect menus on targets, on every owner", ${
	it("lists exactly each root's effect rows as targets", _ {
		const auto sound = joined({kChainPaths, kCompressorAndSidechain, {"clippingAmount"}});
		expect(paths_of(menu::nodes::sound_root)).to_equal(sound);
		expect(paths_of(menu::nodes::root_drum)).to_equal(sound);
		expect(paths_of(menu::nodes::root_audio_clip)).to_equal(sound);
		expect(paths_of(menu::nodes::root_kit_global_fx)).to_equal(joined({kChainPaths, kCompressorAndSidechain}));
		expect(paths_of(menu::nodes::root_song)).to_equal(kChainPaths);
		expect(paths_of(menu::nodes::root_performance)).to_equal(kChainPaths);
	});

	it("reads, ranges and writes every effect node on a synth", _ {
		spec::SongFixture f = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		expect(all_disagreements(menu::nodes::sound_root, ctx)).to_equal(std::string{});
	});

	it("reads, ranges and writes every effect node on a kit row", _ {
		spec::SongFixture f = spec::make_kit_song();
		spec::add_drum<SoundDrum>(f);
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		expect(all_disagreements(menu::nodes::root_drum, ctx)).to_equal(std::string{});
	});

	it("reads, ranges and writes every effect node on the whole kit", _ {
		spec::SongFixture f = spec::make_kit_song();
		spec::add_drum<SoundDrum>(f);
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/true);
		expect(all_disagreements(menu::nodes::root_kit_global_fx, ctx)).to_equal(std::string{});
	});

	it("reads, ranges and writes every effect node on an audio track", _ {
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		expect(all_disagreements(menu::nodes::root_audio_clip, ctx)).to_equal(std::string{});
	});

	it("reads, ranges and writes every effect node on the master, from the song and from performance view", _ {
		spec::SongFixture f = spec::make_synth_song();
		const MenuContext song = menu::context_for(*f.song, nullptr, 0, false, true, false, false);
		expect(all_disagreements(menu::nodes::root_song, song)).to_equal(std::string{});
		const MenuContext performance = menu::context_for(*f.song, nullptr, 0, true, false, false, false);
		expect(all_disagreements(menu::nodes::root_performance, performance)).to_equal(std::string{});
	});
});

DELUGE_SPEC(effect_settings_spec, effect_settings_targets)
