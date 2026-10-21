#include "deluge-project/setting_group.h"
#include "engine-api/types/setting.h"
#include "gui/l10n/setting_names.h"
#include "scenes/bind/dispatch.h"
#include "scenes/bind/setting_context.h"
#include "support/model_fixture.h"

#include <array>
#include <cppspec.hpp>
#include <string>
#include <vector>

using namespace deluge::scenes::bind;
using deluge::engine::SettingContext;
using deluge::project::SettingAddress;
using deluge::project::SettingGroupId;
using deluge::serialization::Value;
using namespace deluge::scenes::bind::test;

namespace {

using Instrument = SettingContext::Instrument;

Target model(SettingGroupId group, std::string_view path) {
	return {.kind = Target::Kind::MODEL, .path = path, .group = group};
}

/// The menu value the record at @p address holds for @p path, as the row converts it.
int32_t record_menu(const ModelFixture& fixture, const SettingAddress& address, std::string_view path) {
	const deluge::engine::SettingInfo& row =
	    *deluge::engine::find_setting(deluge::project::table_of(address.group), path);
	const auto text = deluge::project::read_setting(fixture.song, address, path);
	return deluge::engine::to_menu(row, deluge::engine::stored_value(row, text->str()).value_or(-999));
}

/// Whether @p target's row shows at @p where.
bool applies(const ModelFixture& fixture, const Target& target, const Addresses& where) {
	const auto shown = view(target, fixture.song, where, fixture.facts);
	return shown.has_value() && shown->applies;
}

} // namespace

// clang-format off
describe bind_model("a model target", ${
	it("reads each group's record through its row, from the owner the addresses name", _ {
		ModelFixture f;
		struct Case {
			SettingAddress address;
			std::string_view path;
			Value value;
			Addresses where;
		};
		const Case cases[] = {
		    {{SettingGroupId::SONG, deluge::project::SongAddress{}}, "reverb/roomSize", Value::makeNumber(1073741824), {}},
		    {{SettingGroupId::INSTRUMENT_DEVICE, kCvTrack}, "cv2Source", Value::makeNumber(3),
		     {.track = kCvTrack, .instrument = Instrument::CV}},
		    {{SettingGroupId::CLIP_VALUES, kMidiClip}, "midiPGM", Value::makeNumber(17),
		     {.clip = kMidiClip, .track = kMidiTrack, .instrument = Instrument::MIDI}},
		    {{SettingGroupId::AUDIO_CLIP, kAudioClip}, "reversed", Value::makeNumber(1),
		     {.clip = kAudioClip, .track = kAudioTrack, .instrument = Instrument::AUDIO}},
		    {{SettingGroupId::NOTE_ROW, kSoundRow}, "bendRange", Value::makeNumber(5),
		     {.clip = kKitClip, .note_row = kSoundRow, .instrument = Instrument::KIT}},
		    {{SettingGroupId::CLIP_ARP, kSynthClip}, "arpeggiator/numOctaves", Value::makeNumber(3),
		     {.clip = kSynthClip, .instrument = Instrument::SYNTH}},
		};
		for (const Case& c : cases) {
			expect(deluge::project::write_setting(f.song, c.address, c.path, c.value)).to_be_true();
			const auto shown = view(model(c.address.group, c.path), f.song, c.where, f.facts);
			expect(std::string{c.path} + "=" + (shown ? std::to_string(shown->value) : "none"))
			    .to_equal(std::string{c.path} + "=" + std::to_string(record_menu(f, c.address, c.path)));
		}
	});

	it("reads a kit lane's arp, not its kit clip's, when the addresses name the lane", _ {
		ModelFixture f;
		const auto octaves = [&](const SettingAddress& address, int64_t n) {
			expect(deluge::project::write_setting(f.song, address, "arpeggiator/numOctaves", Value::makeNumber(n)))
			    .to_be_true();
		};
		octaves({SettingGroupId::CLIP_ARP, kKitClip}, 2);
		octaves({SettingGroupId::LANE_ARP, kSoundLane}, 4);
		const Target arp = model(SettingGroupId::CLIP_ARP, "arpeggiator/numOctaves");
		const auto on_lane =
		    view(arp, f.song, {.clip = kKitClip, .lane = kSoundLane, .instrument = Instrument::KIT}, f.facts);
		const auto on_kit = view(arp, f.song, {.clip = kKitClip, .instrument = Instrument::KIT}, f.facts);
		expect(on_lane.has_value() && on_kit.has_value()).to_be_true();
		expect(on_lane->value).to_equal(record_menu(f, {SettingGroupId::LANE_ARP, kSoundLane}, "arpeggiator/numOctaves"));
		expect(on_kit->value).to_equal(record_menu(f, {SettingGroupId::CLIP_ARP, kKitClip}, "arpeggiator/numOctaves"));
		expect(on_lane->value != on_kit->value).to_be_true();
	});

	it("shows the device's default bend range for a bend a clip or kit row does not hold", _ {
		ModelFixture f;
		const Addresses midi{.clip = kMidiClip, .track = kMidiTrack, .instrument = Instrument::MIDI};
		const auto shown = [&](SettingGroupId group, std::string_view path, const Addresses& where) {
			const auto v = view(model(group, path), f.song, where, f.facts);
			return v ? v->value : -1;
		};
		expect(shown(SettingGroupId::CLIP_VALUES, "bendRange", midi)).to_equal(kDeviceBendRange);
		expect(shown(SettingGroupId::CLIP_VALUES, "bendRangeMPE", midi)).to_equal(kDeviceBendRangeMpe);
		const Addresses row{.clip = kKitClip, .note_row = kSoundRow, .instrument = Instrument::KIT};
		expect(shown(SettingGroupId::NOTE_ROW, "bendRange", row)).to_equal(kDeviceBendRange); // a drum's starts at main
		expect(deluge::project::write_setting(f.song, {SettingGroupId::CLIP_VALUES, kMidiClip}, "bendRange",
		                                      Value::makeNumber(12)))
		    .to_be_true();
		expect(shown(SettingGroupId::CLIP_VALUES, "bendRange", midi)).to_equal(12);
	});

	it("shows a choice its record holds no word for as the row's default, and lets it be chosen again", _ {
		ModelFixture f;
		f.song.output_structure.getOrCreate(kMidiTrack.output_id).output_mpe_y = 2; // a file's CC 2
		const Addresses midi{.track = kMidiTrack, .instrument = Instrument::MIDI};
		const Target y = model(SettingGroupId::INSTRUMENT_DEVICE, "outputMpeY");
		const auto shown = view(y, f.song, midi, f.facts);
		expect(shown.has_value()).to_be_true();
		expect(shown->value).to_equal(0); // CC 74, as legacy shows any CC other than 1
		const auto request = request_for(y, f.song, midi, f.facts, 1);
		expect(request.has_value()).to_be_true();
		expect(request->menu_value).to_equal(1);
	});

	it("applies a row as its conditions say in the owner's context", _ {
		ModelFixture f;
		const Target cv2 = model(SettingGroupId::INSTRUMENT_DEVICE, "cv2Source");
		expect(applies(f, cv2, {.track = kCvTrack, .instrument = Instrument::CV})).to_be_true();
		expect(applies(f, cv2, {.track = kMidiTrack, .instrument = Instrument::MIDI})).to_be_false();
		expect(applies(f, model(SettingGroupId::CLIP, "sequenceDirection"),
		               {.clip = kKitClip, .instrument = Instrument::KIT}))
		    .to_be_true();
	});

	it("names a song row through the song group's table", _ {
		ModelFixture f;
		std::array<ChoiceName, 32> out{};
		const size_t count = choice_names(model(SettingGroupId::SONG, "reverb/model"), f.song, {}, f.facts, out);
		const auto* names = deluge::l10n::setting_names(SettingGroupId::SONG, "reverb/model",
		                                                SettingContext{.owner = SettingContext::Owner::SONG});
		expect(names != nullptr).to_be_true();
		expect(count).to_equal(names->choices.size());
		for (size_t i = 0; i < count; ++i) {
			expect(out[i].name).to_equal(names->choices[i]);
		}
	});

	it("builds a coherent context for every owner", _ {
		ModelFixture f;
		const std::vector<std::pair<SettingAddress, Instrument>> owners{
		    {{SettingGroupId::SONG, deluge::project::SongAddress{}}, Instrument::NONE},
		    {{SettingGroupId::INSTRUMENT_DEVICE, kMidiTrack}, Instrument::MIDI},
		    {{SettingGroupId::AUDIO_INPUT, kAudioTrack}, Instrument::AUDIO},
		    {{SettingGroupId::CLIP_VALUES, kMidiClip}, Instrument::MIDI},
		    {{SettingGroupId::AUDIO_CLIP, kAudioClip}, Instrument::AUDIO},
		    {{SettingGroupId::CLIP_ARP, kKitClip}, Instrument::KIT},
		    {{SettingGroupId::LANE_ARP, kSoundLane}, Instrument::KIT},
		    {{SettingGroupId::LANE_ARP, kMidiLane}, Instrument::KIT},
		    {{SettingGroupId::LANE_ARP, kGateLane}, Instrument::KIT},
		    {{SettingGroupId::NOTE_ROW, kSoundRow}, Instrument::KIT},
		};
		for (const auto& [address, instrument] : owners) {
			expect(deluge::engine::context_is_coherent(setting_context(f.song, address, instrument, f.facts)))
			    .to_be_true();
		}
		expect(setting_context(f.song, {SettingGroupId::LANE_ARP, kGateLane}, Instrument::KIT, f.facts).instrument)
		    .to_equal(Instrument::GATE_DRUM);
	});

	it("reads an audio clip's own row for a sound setting an audio clip also has", _ {
		ModelFixture f;
		expect(deluge::project::write_setting(f.song, {SettingGroupId::AUDIO_CLIP, kAudioClip}, "priority",
		                                      Value::makeScalar("2")))
		    .to_be_true();
		const Target priority{.kind = Target::Kind::SOUND, .path = "voicePriority", .audio_clip_path = "priority"};
		const auto shown = view(priority, f.song,
		                        {.clip = kAudioClip, .track = kAudioTrack, .instrument = Instrument::AUDIO}, f.facts);
		expect(shown.has_value()).to_be_true();
		expect(shown->value).to_equal(2);
		std::array<ChoiceName, 32> out{};
		const size_t count = choice_names(
		    priority, f.song, {.clip = kAudioClip, .track = kAudioTrack, .instrument = Instrument::AUDIO}, f.facts, out);
		const auto* names = deluge::l10n::setting_names(SettingGroupId::AUDIO_CLIP, "priority",
		                                                SettingContext{.owner = SettingContext::Owner::CLIP,
		                                                               .instrument = Instrument::AUDIO});
		expect(names != nullptr).to_be_true();
		expect(count).to_equal(names->choices.size());
	});

	it("shows a twin page only on its own kind of root: a kit lane, or every other", _ {
		ModelFixture f;
		Target on_lane = model(SettingGroupId::CLIP_ARP, "arpeggiator/noteMode");
		on_lane.shown_on = Target::ShownOn::ON_LANE;
		Target off_lane = on_lane;
		off_lane.shown_on = Target::ShownOn::OFF_LANE;
		const Addresses lane{.clip = kKitClip, .lane = kSoundLane, .instrument = Instrument::KIT};
		const Addresses clip{.clip = kSynthClip, .instrument = Instrument::SYNTH};
		expect(view(on_lane, f.song, lane, f.facts).has_value()).to_be_true();
		expect(view(on_lane, f.song, clip, f.facts).has_value()).to_be_false();
		expect(view(off_lane, f.song, clip, f.facts).has_value()).to_be_true();
		expect(view(off_lane, f.song, lane, f.facts).has_value()).to_be_false();
		expect(request_for(on_lane, f.song, clip, f.facts, 0).has_value()).to_be_false();
	});

	it("resolves nothing for an owner the song does not hold", _ {
		ModelFixture f;
		const auto shown = view(model(SettingGroupId::CLIP_VALUES, "midiPGM"), f.song,
		                        {.clip = deluge::project::ClipAddress{999}, .instrument = Instrument::MIDI}, f.facts);
		expect(shown.has_value()).to_be_false();
	});
});

CPPSPEC_SPEC(bind_model)
