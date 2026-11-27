// clang-format off
#include "ui/controls/follow.h"
#include "support/printers/plugin_id_printer.h"
#include <array>
#include <cppspec.hpp>
#include <optional>
#include <string>

using namespace deluge;
using namespace deluge::ui::controls;

namespace {
using project::OwnerId;
using project::ParamAddress;
using project::ParamTarget;

constexpr uint8_t kSoundCc = 3;    ///< follows sound param 7 and global param 8
constexpr uint8_t kPortaCc = 4;    ///< follows the instrument's portamento (a sound param only)
constexpr uint8_t kGlobalCc = 5;   ///< follows global param 9 only
constexpr uint8_t kSoundOnlyCc = 6; ///< follows sound param 10 only
constexpr uint8_t kBareCc = 7;     ///< follows nothing

struct Tables {
	std::array<std::optional<ParamTarget>, 128> sound{};
	std::array<std::optional<ParamTarget>, 128> global{};
	Tables() {
		sound[kSoundCc] = project::engine_target(7);
		global[kSoundCc] = project::engine_target(8);
		sound[kPortaCc] = project::engine_target(kFollowRefusedOnKitRow);
		global[kGlobalCc] = project::engine_target(9);
		sound[kSoundOnlyCc] = project::engine_target(10);
	}
	[[nodiscard]] FollowTables view() const { return {.sound = sound, .global = global}; }
};

/// A short name for @p address, or "none".
std::string name(const std::optional<ParamAddress>& address) {
	if (!address) {
		return "none";
	}
	std::string out;
	switch (address->owner.kind) {
	case OwnerId::Kind::SONG: out = "S"; break;
	case OwnerId::Kind::CLIP: out = "C" + std::to_string(address->owner.id); break;
	case OwnerId::Kind::INSTRUMENT: out = "I" + std::to_string(address->owner.id); break;
	case OwnerId::Kind::DRUM: out = "D" + std::to_string(address->owner.id); break;
	case OwnerId::Kind::NOTE_ROW: out = "N" + std::to_string(address->owner.id); break;
	}
	if (address->clip_id) {
		out += "@" + std::to_string(*address->clip_id);
	}
	return out + "#" + std::to_string(address->id.value);
}

const KnobFocus kSynth{.owner = SoundFocus{.slot = {project::SlotAddress::Kind::OUTPUT, 1}, .clip = project::ClipAddress{11}}};
const KnobFocus kKitRow{.owner = SoundFocus{.slot = {project::SlotAddress::Kind::DRUM, 5},
                                            .clip = project::ClipAddress{12},
                                            .row = project::NoteRowAddress{12, true, 5},
                                            .kit = 2}};
const KnobFocus kKitEntire{.owner = ChainFocus{.chain = {project::ChainAddress::Kind::OUTPUT, 2},
                                               .owner = ChainOwner::KIT,
                                               .clip = project::ClipAddress{12}}};
const KnobFocus kAudio{.owner = ChainFocus{.chain = {project::ChainAddress::Kind::OUTPUT, 3},
                                           .owner = ChainOwner::AUDIO_TRACK,
                                           .clip = project::ClipAddress{13}}};
const KnobFocus kSong{.owner = SongFocus{}};
} // namespace

describe follow("MIDI follow's params", ${
	const Tables tables;
	const FollowTables view = tables.view();

	it("names a synth's sound param, in the clip the focus is on", _{
		expect(name(resolve_follow(kSynth, view, kSoundCc))).to_equal(std::string{"I1@11#7"});
		expect(name(resolve_follow(kSynth, view, kSoundOnlyCc))).to_equal(std::string{"I1@11#10"});
	});
	it("names a synth nothing for a CC only the global table holds", _{
		expect(name(resolve_follow(kSynth, view, kGlobalCc))).to_equal(std::string{"none"});
	});
	it("names a kit's chain param under affect-entire, from the global table", _{
		expect(name(resolve_follow(kKitEntire, view, kSoundCc))).to_equal(std::string{"I2@12#8"});
		expect(name(resolve_follow(kKitEntire, view, kGlobalCc))).to_equal(std::string{"I2@12#9"});
		expect(name(resolve_follow(kKitEntire, view, kSoundOnlyCc))).to_equal(std::string{"none"});
	});
	it("names a kit's selected row's sound param without affect-entire, from the sound table", _{
		expect(name(resolve_follow(kKitRow, view, kSoundCc))).to_equal(std::string{"D5@12#7"});
		expect(name(resolve_follow(kKitRow, view, kGlobalCc))).to_equal(std::string{"none"});
	});
	it("refuses a kit row its portamento, and a synth not", _{
		expect(name(resolve_follow(kKitRow, view, kPortaCc))).to_equal(std::string{"none"});
		expect(name(resolve_follow(kSynth, view, kPortaCc))).to_equal(std::string{"I1@11#101"});
	});
	it("names an audio track's param on its clip, from the global table", _{
		expect(name(resolve_follow(kAudio, view, kSoundCc))).to_equal(std::string{"C13#8"});
		expect(name(resolve_follow(kAudio, view, kSoundOnlyCc))).to_equal(std::string{"none"});
	});
	it("names the song's param, from the global table", _{
		expect(name(resolve_follow(kSong, view, kGlobalCc))).to_equal(std::string{"S#9"});
		expect(name(resolve_follow(kSong, view, kSoundOnlyCc))).to_equal(std::string{"none"});
	});
	it("names nothing for a MIDI or CV track, a drum with no knobs, no focus, or a CC nothing follows", _{
		const KnobFocus midi{.owner = MidiFocus{.track = {4}, .clip = project::ClipAddress{14}}};
		const KnobFocus cv{.owner = NoControlsFocus{.kind = NoControlsFocus::Kind::CV_TRACK, .track = {5}, .clip_id = 15}};
		for (const KnobFocus& focus : {midi, cv, KnobFocus{}}) {
			expect(name(resolve_follow(focus, view, kSoundCc))).to_equal(std::string{"none"});
		}
		expect(name(resolve_follow(kSynth, view, kBareCc))).to_equal(std::string{"none"});
		expect(name(resolve_follow(kSynth, view, 200))).to_equal(std::string{"none"});
	});
	it("reads a sound's rows for a sound and the global effectable's for a chain or the song", _{
		expect(follow_plugin(kSynth)).to_equal(plugin::kDelugeInstrument);
		expect(follow_plugin(kKitRow)).to_equal(plugin::kDelugeInstrument);
		expect(follow_plugin(kKitEntire)).to_equal(plugin::kGlobalEffectable);
		expect(follow_plugin(kAudio)).to_equal(plugin::kGlobalEffectable);
		expect(follow_plugin(kSong)).to_equal(plugin::kGlobalEffectable);
	});

	it("follows a MIDI CC and an expression axis for either owner, whatever the table holds", _{
		auto none = [](plugin::PluginId, plugin::ParamId) { return false; };
		for (project::OwnerPlugin owner : {project::OwnerPlugin::INSTRUMENT, project::OwnerPlugin::GLOBAL_EFFECTABLE}) {
			const ParamTarget cc{project::ParamSpace::MIDI_CC, plugin::ParamId{74}};
			const ParamTarget axis{project::ParamSpace::EXPRESSION, plugin::ParamId{2}};
			expect(follow_target(owner, project::PluginParam{.plugin = owner, .target = cc}, none)).to_equal(std::optional{cc});
			expect(follow_target(owner, project::PluginParam{.plugin = owner, .target = axis}, none)).to_equal(std::optional{axis});
		}
	});
	it("follows an engine param only in its owner's plugin, and only when its table has the id", _{
		auto has_7 = [](plugin::PluginId, plugin::ParamId id) { return id.value == 7; };
		const auto sound = project::OwnerPlugin::INSTRUMENT;
		const auto global = project::OwnerPlugin::GLOBAL_EFFECTABLE;
		const auto engine = [](project::OwnerPlugin plugin, uint32_t id) {
			return project::PluginParam{.plugin = plugin, .target = project::engine_target(id)};
		};
		expect(follow_target(sound, engine(sound, 7), has_7)).to_equal(std::optional{project::engine_target(7)});
		expect(follow_target(global, engine(global, 7), has_7)).to_equal(std::optional{project::engine_target(7)});
		// the other owner's param
		expect(follow_target(global, engine(sound, 7), has_7)).not_().to_have_value();
		expect(follow_target(sound, engine(global, 7), has_7)).not_().to_have_value();
		// an id the table lacks
		expect(follow_target(sound, engine(sound, 8), has_7)).not_().to_have_value();
	});
	it("follows a cable's depth in a sound without asking the table", _{
		auto none = [](plugin::PluginId, plugin::ParamId) { return false; };
		const project::PluginParam depth{.plugin = project::OwnerPlugin::INSTRUMENT, .target = project::engine_target(0x4000'0119u)};
		expect(follow_target(project::OwnerPlugin::INSTRUMENT, depth, none)).to_equal(std::optional{project::engine_target(0x4000'0119u)});
		expect(follow_target(project::OwnerPlugin::GLOBAL_EFFECTABLE, depth, none)).not_().to_have_value();
		const project::PluginParam chain_flagged{.plugin = project::OwnerPlugin::GLOBAL_EFFECTABLE, .target = project::engine_target(0x4000'0119u)};
		expect(follow_target(project::OwnerPlugin::GLOBAL_EFFECTABLE, chain_flagged, none)).not_().to_have_value();
	});
});
// clang-format on

CPPSPEC_SPEC(follow)
