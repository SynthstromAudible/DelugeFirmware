// One arp setting written across a kit's lanes as one Edit, as legacy's affect-entire loops write it: every drum,
// sound, MIDI and gate alike, whose row's conditions hold in its own lane; for the kit arp, a sound drum only where it
// can hold note tails. The kit clip's own arp is left alone.

#include "deluge-project/setting_group.h"
#include "gui/ui/menu_view_of.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/edits/edit.h"
#include "model/edits/set_kit_lane_setting.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound_drum.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <limits>
#include <string>
#include <vector>

using namespace deluge::scenes;
using deluge::project::SettingGroupId;
using deluge::scenes::menu::MenuContext;
using deluge::serialization::Value;

namespace {

/// A kit with a sound row, a second sound row, a MIDI row and a gate row.
struct Lanes {
	spec::SongFixture f = spec::make_kit_song();
	SoundDrum* sound = spec::add_drum<SoundDrum>(f);
	SoundDrum* second = spec::add_drum<SoundDrum>(f);
	MIDIDrum* midi = spec::add_drum<MIDIDrum>(f);
	GateDrum* gate = spec::add_drum<GateDrum>(f);
	[[nodiscard]] uint32_t kit() const { return *f.instrument->id(); }
};

/// @p drum's lane record's value at @p path, as text.
std::string lane(Lanes& l, Drum& drum, std::string_view path) {
	const auto value = deluge::project::read_setting(
	    l.f.song->project, {SettingGroupId::LANE_ARP, deluge::project::LaneAddress{l.kit(), *drum.id()}}, path);
	return value ? value->str() : "none";
}

/// The kit clip's arp record's value at @p path, as text.
std::string kit_clip(Lanes& l, std::string_view path) {
	const auto value = deluge::project::read_setting(
	    l.f.song->project, {SettingGroupId::CLIP_ARP, deluge::project::ClipAddress{l.f.clip->id()}}, path);
	return value ? value->str() : "none";
}

uint32_t apply(Lanes& l, std::string_view path, Value value, bool needs_note_tails = false) {
	deluge::edits::Context ctx{*l.f.song, l.f.song->project};
	deluge::edits::SetKitLaneSetting edit{l.kit(), std::string(path), std::move(value), needs_note_tails};
	(void)deluge::edits::apply(ctx, edit);
	return edit.drums_written();
}

} // namespace

// clang-format off
describe set_kit_lane_setting("an arp setting written across a kit's lanes", ${
	it("reaches every lane its row applies to, and not a gate lane where it does not", _ {
		Lanes l;
		const std::string clip_before = kit_clip(l, "arpeggiator/numOctaves");
		expect(apply(l, "arpeggiator/numOctaves", Value::makeNumber(3))).to_equal(3u);
		for (Drum* drum : std::initializer_list<Drum*>{l.sound, l.second, l.midi}) {
			expect(lane(l, *drum, "arpeggiator/numOctaves")).to_equal("3");
		}
		expect(lane(l, *l.gate, "arpeggiator/numOctaves") != "3").to_be_true();
		expect(static_cast<int>(l.midi->arpSettings.numOctaves)).to_equal(3);
		expect(kit_clip(l, "arpeggiator/numOctaves")).to_equal(clip_before);
	});

	it("reaches every lane, gate included, for a row every lane has", _ {
		Lanes l;
		expect(apply(l, "arpeggiator/arpMode", Value::makeScalar("arp"))).to_equal(4u);
		for (Drum* drum : std::initializer_list<Drum*>{l.sound, l.second, l.midi, l.gate}) {
			expect(lane(l, *drum, "arpeggiator/arpMode")).to_equal("arp");
		}
	});

	it("includes in the kit arp a sound lane only where it can hold note tails, as legacy's loop does", _ {
		Lanes l;
		// The second row's envelope never sustains (no sustain, and a release no shorter than its decay): it holds no tails.
		char memory[MODEL_STACK_MAX_SIZE];
		PatchedParamSet* patched = getModelStackFromSoundDrum(memory, l.second)->paramManager->getPatchedParamSet();
		patched->params[deluge::modulation::params::LOCAL_ENV_0_SUSTAIN].setCurrentValueBasicForSetup(
		    *patched->docForParam(deluge::modulation::params::LOCAL_ENV_0_SUSTAIN), std::numeric_limits<int32_t>::min());
		patched->params[deluge::modulation::params::LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(
		    *patched->docForParam(deluge::modulation::params::LOCAL_ENV_0_RELEASE), std::numeric_limits<int32_t>::max());
		const MenuContext ctx = menu::context_for(*l.f.song, deluge::gui::menuViewOf(l.f.clip), 0, false, false, false, false);
		const bool sound_tails = spec::drum_allows_note_tails(l.sound);
		const bool second_tails = spec::drum_allows_note_tails(l.second);
		const std::string sound_before = lane(l, *l.sound, "arpeggiator/kitArp");
		const std::string second_before = lane(l, *l.second, "arpeggiator/kitArp");
		expect(second_tails).to_be_false();
		const uint32_t written = apply(l, "arpeggiator/kitArp", Value::makeNumber(1), true);
		expect(written).to_equal(2u + (sound_tails ? 1u : 0u) + (second_tails ? 1u : 0u));
		expect(lane(l, *l.sound, "arpeggiator/kitArp")).to_equal(sound_tails ? std::string{"1"} : sound_before);
		expect(lane(l, *l.second, "arpeggiator/kitArp")).to_equal(second_before);
		expect(lane(l, *l.midi, "arpeggiator/kitArp")).to_equal("1");
		expect(lane(l, *l.gate, "arpeggiator/kitArp")).to_equal("1");
	});

	it("leaves a lane as it was where the value is refused", _ {
		Lanes l;
		const std::string before = lane(l, *l.midi, "arpeggiator/numOctaves");
		expect(apply(l, "arpeggiator/numOctaves", Value::makeNumber(99))).to_equal(0u);
		expect(lane(l, *l.midi, "arpeggiator/numOctaves")).to_equal(before);
	});

	it("seeds each lane's record from its drum's arp before writing", _ {
		Lanes l;
		l.midi->docDrum_->arp.reset();
		l.midi->arpSettings.numOctaves = 5;
		(void)apply(l, "arpeggiator/stepRepeat", Value::makeNumber(2));
		expect(lane(l, *l.midi, "arpeggiator/numOctaves")).to_equal("5");
	});
});

CPPSPEC_SPEC(set_kit_lane_setting)
