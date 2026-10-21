#include "controls_test_engines.h"
#include "deluge-project/document.h"
#include "serialization/parse_json.h"
#include "serialization/parse_xml.h"
#include "serialization/write_json.h"

#include <cppspec.hpp>

using namespace deluge::project;
using deluge::serialization::parseJson;
using deluge::serialization::parseXml;
using deluge::serialization::write_json;

namespace {

constexpr const char* kSong =
    R"(<song firmwareVersion="4.1.0" inputTickMagnitude="2" timePerTimerTick="11" timerTickFraction="-5" swingAmount="5" swingInterval="7" rootNote="2">
<modeNotes><modeNote>0</modeNote><modeNote>3</modeNote><modeNote>7</modeNote></modeNotes>
<reverb roomSize="1000000" pan="4"><compressor attack="3" syncLevel="6"/></reverb>
<instruments>
<sound id="1" presetName="PAD" defaultVelocity="64" colour="3"/>
<kit id="2" presetName="KIT"><soundSources><sound name="K" id="5"/><midiOutput name="M" channel="2" note="40" id="6"/></soundSources></kit>
<audioTrack id="3" name="AUDIO1"/>
<midi id="4" channel="9" suffix="1"/>
</instruments>
<sessionClips>
<instrumentClip id="10" length="96" colourOffset="3"><noteRows><noteRow y="60"/><noteRow y="62" muted="1"/></noteRows></instrumentClip>
<instrumentClip id="11" length="96"><kitParams/><noteRows><noteRow colourOffset="2"/><noteRow/></noteRows></instrumentClip>
<audioClip id="12" filePath="a.wav" length="48" startSamplePos="10"/>
</sessionClips>
<arrangementOnlyTracks><instrumentClip id="13" length="48" section="2"/></arrangementOnlyTracks>
<sections><section id="0" numRepeats="2" midiCommandChannel="1" midiCommandNote="60"/></sections>
<sessionMacros><macro kind="clip_launch" clipId="10"/><macro kind="output_cycle" output="1"/></sessionMacros>
</song>)";

SongDocument decoded(const char* xml) {
	auto root = parseXml(xml);
	return *decode_song(*root, deluge::project::test::test_engines());
}

SongDocument through_json(const SongDocument& document) {
	auto root = parseJson(write_json(encode_song(document, "c1.0.0", deluge::project::test::test_engines())));
	return *decode_song(*root, deluge::project::test::test_engines());
}

} // namespace

// clang-format off
describe document("the model's parts of a whole song", ${
	it("survives a JSON round trip", _ {
		SongDocument a = decoded(kSong);
		SongDocument b = through_json(a);
		expect(b).to_equal(a);
		expect(write_json(encode_song(b, "c1.0.0", deluge::project::test::test_engines()))).to_equal(write_json(encode_song(a, "c1.0.0", deluge::project::test::test_engines())));
	});

	it("reads every record of the song", _ {
		SongDocument a = decoded(kSong);
		expect(a.clips.size()).to_equal(4u);
		expect(a.audio_clips.size()).to_equal(1u);
		expect(a.outputs.size()).to_equal(4u);
		expect(a.drums.size()).to_equal(2u);
		expect(a.clips.at(10).note_rows.size()).to_equal(2u);
		expect(static_cast<int>(a.clips.at(13).section)).to_equal(255); // arranger-only
		expect(a.outputs.at(2).name).to_equal(std::string("KIT"));
		expect(a.drums.at(6).drum_type).to_equal(DrumType::MIDI);
		expect(a.groove.swing_amount).to_equal(5);
		// a slot's sync levels are relative to the magnitude its song was saved at
		expect(a.outputs.at(1).instrument->tick_magnitude).to_equal(2);
		expect(a.drums.at(5).instrument->tick_magnitude).to_equal(2);
		// a pre-id macro naming its output by position names the output at that position
		expect(a.arrangement.session_macros[1].kind).to_equal(SessionMacroKind::OUTPUT_CYCLE);
		expect(*a.arrangement.session_macros[1].output_id).to_equal(2u);
		expect(*a.arrangement.session_macros[0].clip_id).to_equal(10u);
	});

	it("reads and writes a synth track's arpeggiator, as a kit row's is", _ {
		constexpr const char* kSynthArp =
		    R"(<song firmwareVersion="4.1.0" inputTickMagnitude="2"><instruments>)"
		    R"(<sound id="1" presetName="ARP"><arpeggiator mode="up" numOctaves="3" syncLevel="7"/></sound>)"
		    R"(</instruments></song>)";
		SongDocument a = decoded(kSynthArp);
		expect(a.outputs.at(1).arp.has_value()).to_be_true();
		expect(static_cast<int>(a.outputs.at(1).arp->num_octaves)).to_equal(3);
		expect(a.outputs.at(1).instrument->state.encode({.input_tick_magnitude = 2}).find("arpeggiator") == nullptr)
		    .to_be_true();
		expect(through_json(a)).to_equal(a);
		expect(decoded(kSong).outputs.at(1).arp.has_value()).to_be_false();
	});

	it("tells a kit clip by its rows, so a kit row keeps its colour and takes no y from the file", _ {
		SongDocument a = decoded(kSong);
		expect(a.clip_entry(11)->in_kit).to_be_true();
		expect(a.clip_entry(10)->in_kit).to_be_false();
		expect(static_cast<int>(a.clips.at(11).note_rows.front().colour_offset)).to_equal(2);
	});

	it("gives a pre-id song ids in document order", _ {
		std::string noIds = kSong;
		for (const char* id : {" id=\"1\"", " id=\"2\"", " id=\"3\"", " id=\"4\"", " id=\"5\"", " id=\"6\"",
		                       " id=\"10\"", " id=\"11\"", " id=\"12\"", " id=\"13\""}) {
			noIds.erase(noIds.find(id), std::string(id).size());
		}
		SongDocument a = decoded(noIds.c_str());
		expect(a.outputs.size()).to_equal(4u);
		expect(a.clips.size()).to_equal(4u);
		expect(a.outputs.count(1)).to_equal(1u);
		expect(a.clips.count(1)).to_equal(1u);
		expect(through_json(a)).to_equal(a);
	});
});

CPPSPEC_SPEC(document)
