// The sound's instrument settings on targets (step 2a's pilot): every node of the sound editor's tree that edits a row
// of instrument::kSettings names it as its target, and the menu reads, ranges and writes it through the binding
// library, on a synth and on a kit row.

#include "scenes/bind/dispatch.h"
#include "scenes/bind/write_sink.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/nodes.h"

#include "engine-api/types/setting.h"
#include "engines/deluge/settings.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "processing/sound/sound_drum.h"

#include "support/menu_engine.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
namespace bindings = deluge::scenes::menu::bindings;

namespace {

/// Every node reachable from the sound editor's root, children and chain targets alike, each once.
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

/// The target nodes reachable from the sound editor's root: those of @p kind, or all of them.
std::vector<const Node*> target_nodes(std::optional<bind::Target::Kind> kind = bind::Target::Kind::SOUND) {
	std::set<const Node*> seen;
	std::vector<const Node*> all;
	collect(menu::nodes::sound_root, seen, all);
	std::vector<const Node*> targets;
	for (const Node* node : all) {
		if (node->bind.target.valid() && (!kind || node->bind.target.kind == *kind)) {
			targets.push_back(node);
		}
	}
	return targets;
}

/// Where a target node disagrees with the binding library on @p ctx: its read, its range, or a write at its ends and
/// middle that does not round-trip through the descriptor. Empty when it agrees.
std::string disagreements(const Node& node, MenuContext ctx) {
	const auto shown = bind::view(node.bind.target, *ctx.model, ctx.addresses, *ctx.facts);
	if (!shown) {
		return std::string(node.bind.target.path) + " unresolved ";
	}
	std::string found;
	const std::string path(node.bind.target.path);
	if (bindings::read(node, ctx) != shown->value) {
		found += path + " read ";
	}
	const auto range = bindings::range(node, ctx);
	if (range.min != shown->least || range.max != shown->greatest) {
		found += path + " range ";
	}
	const deluge::engine::SettingInfo& row = *deluge::engine::find_setting(deluge::instrument::kSettings, path);
	const std::array<int32_t, 3> probes{shown->least, shown->greatest,
	                                    shown->least + (shown->greatest - shown->least) / 2};
	bind::RecordingSink recorded;
	MenuContext recording = ctx;
	recording.sink = &recorded;
	for (const int32_t v : probes) {
		if (shown->stale == v) {
			continue;
		}
		recorded.requests.clear();
		bindings::write(node, recording, v);
		if (recorded.requests.size() != 1 || !deluge::engine::from_menu(row, recorded.requests[0].menu_value)) {
			found += path + " request(" + std::to_string(v) + ") ";
		}
	}
	// Through the app's sink, where the row applies (its Edit refuses a row that does not): the Edit lands, the menu
	// reads back from the record what it wrote, and the original value goes back so later nodes see the same sound.
	if (shown->applies && !shown->stale) {
		for (const int32_t v : probes) {
			bindings::write(node, ctx, v);
			if (bindings::read(node, ctx) != v) {
				found += path + " write(" + std::to_string(v) + ")->" + std::to_string(bindings::read(node, ctx)) + " ";
			}
		}
		bindings::write(node, ctx, shown->value);
	}
	return found;
}

} // namespace

// clang-format off
describe sound_settings_targets("the sound's instrument settings on targets", ${
	it("gives every target node a fan-out rule that reads rows of a sound's table", _ {
		std::vector<std::string> invalid;
		for (const Node* node : target_nodes(std::nullopt)) {
			for (const auto& condition : node->bind.target.fan_out_when) {
				if (!deluge::engine::condition_is_valid(deluge::instrument::kSettings, condition)) {
					invalid.emplace_back(node->bind.target.path);
				}
			}
		}
		expect(invalid).to_equal(std::vector<std::string>{});
		constexpr std::array<int32_t, 1> poly{1};
		expect(deluge::engine::condition_is_valid(deluge::instrument::kSettings,
		                                          deluge::engine::setting_is("polyphonc", poly)))
		    .to_be_false();
	});

	it("names exactly the pilot's rows as targets", _ {
		std::set<std::string> paths;
		for (const Node* node : target_nodes()) {
			paths.insert(std::string(node->bind.target.path));
		}
		const std::set<std::string> want{
		    "maxVoices", "polyphonic", "mode", "transpose", "sideChainSend", "midiOutput/channel",
		    "midiOutput/noteForDrum", "unison/num", "unison/detune", "unison/spread", "osc1/type", "osc2/type",
		    "osc1/retrigPhase", "osc2/retrigPhase", "osc1/reversed", "osc2/reversed", "osc1/loopMode", "osc2/loopMode",
		    "osc1/timeStretchAmount", "osc2/timeStretchAmount", "osc2/oscillatorSync", "modulator1/transpose",
		    "modulator2/transpose", "modulator1/retrigPhase", "modulator2/retrigPhase", "modulator2/toModulator1",
		    "lfo1/type", "lfo2/type", "lfo3/type", "lfo4/type", "lfo1/syncLevel", "lfo2/syncLevel", "lfo3/syncLevel",
		    "lfo4/syncLevel", "voicePriority", "osc1/timeStretchEnable", "osc2/timeStretchEnable", "osc1/linearInterpolation",
		    "osc2/linearInterpolation"};
		expect(paths).to_equal(want);
	});

	it("reads, ranges and writes every target node through the binding library on a synth", _ {
		spec::SongFixture f = spec::make_synth_song();
		f.song->setCurrentClip(f.clip);
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		std::string found;
		for (const Node* node : target_nodes()) {
			found += disagreements(*node, ctx);
		}
		expect(found).to_equal(std::string{});
	});

	it("reads, ranges and writes every target node through the binding library on a kit row", _ {
		spec::SongFixture f = spec::make_kit_song();
		spec::add_drum<SoundDrum>(f);
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		std::string found;
		for (const Node* node : target_nodes()) {
			found += disagreements(*node, ctx);
		}
		expect(found).to_equal(std::string{});
	});
});

// clang-format off
describe sound_settings_listed("the menu lists a target node only where its setting applies", ${
	it("leaves the sample rows off a saw oscillator's page, and lists them on a loaded sample's", _ {
		spec::SongFixture f = spec::make_synth_song();
		f.song->setCurrentClip(f.clip);
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		deluge::edits::settingFor(*spec::menu_engine(ctx).sound, "osc1/type", deluge::serialization::Value::makeScalar("saw"));
		std::array<const Node*, menu::kMaxDynamicChildren> listed{};
		const size_t n = bindings::listed_children(menu::nodes::source0, ctx, listed);
		const auto lists = [&](const Node& node) {
			return std::find(listed.begin(), listed.begin() + static_cast<std::ptrdiff_t>(n), &node)
			       != listed.begin() + static_cast<std::ptrdiff_t>(n);
		};
		expect(lists(menu::nodes::osc0_type)).to_be_true();
		expect(lists(menu::nodes::sample0_reverse)).to_be_false();
		expect(lists(menu::nodes::sample0_repeat)).to_be_false();
		expect(lists(menu::nodes::sample0_time_stretch)).to_be_false();
	});

	it("leaves max voices off a mono sound's voice page", _ {
		spec::SongFixture f = spec::make_synth_song();
		f.song->setCurrentClip(f.clip);
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		deluge::edits::settingFor(*spec::menu_engine(ctx).sound, "polyphonic", deluge::serialization::Value::makeScalar("mono"));
		std::array<const Node*, menu::kMaxDynamicChildren> listed{};
		const size_t n = bindings::listed_children(menu::nodes::voice, ctx, listed);
		expect(std::find(listed.begin(), listed.begin() + static_cast<std::ptrdiff_t>(n), &menu::nodes::voice_count)
		       == listed.begin() + static_cast<std::ptrdiff_t>(n))
		    .to_be_true();
	});
});

DELUGE_SPEC(sound_settings_spec, sound_settings_targets, sound_settings_listed)
