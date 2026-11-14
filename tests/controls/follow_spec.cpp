// clang-format off
#include "controls/follow.h"
#include <array>
#include <cppspec.hpp>
#include <optional>
#include <string>

using namespace deluge;
using namespace deluge::controls;

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
		expect(name(resolve_follow(kSynth, view, kPortaCc))).to_equal(std::string{"I1@11#116"});
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
		expect(follow_plugin(kSynth) == plugin::kDelugeInstrument).to_be_true();
		expect(follow_plugin(kKitRow) == plugin::kDelugeInstrument).to_be_true();
		expect(follow_plugin(kKitEntire) == plugin::kGlobalEffectable).to_be_true();
		expect(follow_plugin(kAudio) == plugin::kGlobalEffectable).to_be_true();
		expect(follow_plugin(kSong) == plugin::kGlobalEffectable).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(follow)
