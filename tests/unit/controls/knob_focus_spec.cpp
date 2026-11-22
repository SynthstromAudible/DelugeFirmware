// clang-format off
#include "board_controls.hpp"
#include "controls/knob_focus.h"
#include "project/song.h"
#include "support/printers/knob_focus_printer.h"
#include "support/printers/owner_id_printer.h"
#include <cppspec.hpp>

using namespace deluge;

namespace {
/// A song with outputs 2 (a synth or kit), 4 (a kit) and 6 in modes 3, 5 and 9 (past the last button).
project::Song song_with_modes() {
	project::Song song{};
	song.output_structure.getOrCreate(2).mod_knob_mode = 3;
	song.output_structure.getOrCreate(4).mod_knob_mode = 5;
	song.output_structure.getOrCreate(6).mod_knob_mode = 9;
	song.mod_knob_mode = 1;
	return song;
}
const project::ClipAddress kClip{7};
} // namespace

describe knob_focus("the gold knobs' focus", ${
	it("has no owner and mode 0 with no focus", _{
		const project::Song song{};
		const controls::KnobFocus focus{};
		expect(controls::knob_owner(focus).has_value()).to_be_false();
		expect(int{controls::knob_mode(song, focus, kDelugeControls)}).to_equal(0);
	});
	it("names no owner and no mode owner for an owner with no controls, and mode 0 though its output has one", _{
		const controls::KnobFocus cv{.owner = controls::NoControlsFocus{
		    .kind = controls::NoControlsFocus::Kind::CV_TRACK, .track = {2}, .clip_id = 7}};
		const controls::KnobFocus drum{.owner = controls::NoControlsFocus{
		    .kind = controls::NoControlsFocus::Kind::MIDI_DRUM, .track = {4}, .drum = project::KitRowAddress{4, 11}}};
		for (const controls::KnobFocus& focus : {cv, drum}) {
			expect(controls::knob_owner(focus).has_value()).to_be_false();
			expect(controls::knob_mode_owner(focus).has_value()).to_be_false();
			expect(int{controls::knob_mode(song_with_modes(), focus, kDelugeControls)}).to_equal(0);
			expect(controls::has_no_knobs(focus)).to_be_true();
		}
		expect(controls::has_no_knobs(controls::KnobFocus{})).to_be_true();
		expect(controls::has_no_knobs(controls::KnobFocus{.owner = controls::SongFocus{}})).to_be_false();
		expect(cv.owner).not_().to_equal(drum.owner);
	});
	it("names the song as the owner of the song's knobs", _{
		const controls::KnobFocus focus{.owner = controls::SongFocus{}};
		expect(controls::knob_owner(focus)->owner).to_equal(project::OwnerId::song());
		expect(std::holds_alternative<project::SongAddress>(*controls::knob_mode_owner(focus))).to_be_true();
		expect(int{controls::knob_mode(song_with_modes(), focus, kDelugeControls)}).to_equal(1);
	});
	it("follows a synth's own mode, on the synth", _{
		const controls::KnobFocus focus{
		    .owner = controls::SoundFocus{.slot = {project::SlotAddress::Kind::OUTPUT, 2}, .clip = kClip}};
		expect(controls::knob_owner(focus)->owner).to_equal(project::OwnerId::instrument(2));
		expect(controls::knob_owner(focus)->clip_id).to_equal(std::optional<uint32_t>{7});
		expect(std::get<project::TrackAddress>(*controls::knob_mode_owner(focus)).output_id).to_equal(2u);
		expect(int{controls::knob_mode(song_with_modes(), focus, kDelugeControls)}).to_equal(3);
	});
	it("follows a kit row's kit's mode, on the drum", _{
		const controls::KnobFocus focus{
		    .owner = controls::SoundFocus{.slot = {project::SlotAddress::Kind::DRUM, 11}, .clip = kClip, .kit = 4}};
		expect(controls::knob_owner(focus)->owner).to_equal(project::OwnerId::drum(11));
		expect(std::get<project::TrackAddress>(*controls::knob_mode_owner(focus)).output_id).to_equal(4u);
		expect(int{controls::knob_mode(song_with_modes(), focus, kDelugeControls)}).to_equal(5);
	});
	it("names no mode owner for a drum that names no kit, as its mode is none", _{
		const controls::KnobFocus focus{.owner = controls::SoundFocus{.slot = {project::SlotAddress::Kind::DRUM, 11}}};
		expect(controls::knob_mode_owner(focus).has_value()).to_be_false();
		expect(int{controls::knob_mode(song_with_modes(), focus, kDelugeControls)}).to_equal(0);
	});
	it("follows a kit's mode on its chain, on the kit", _{
		const controls::KnobFocus focus{.owner = controls::ChainFocus{
		                                    .chain = {project::ChainAddress::Kind::OUTPUT, 4}, .clip = kClip}};
		expect(controls::knob_owner(focus)->owner).to_equal(project::OwnerId::instrument(4));
		expect(controls::knob_owner(focus)->clip_id).to_equal(std::optional<uint32_t>{7});
		expect(std::get<project::TrackAddress>(*controls::knob_mode_owner(focus)).output_id).to_equal(4u);
		expect(int{controls::knob_mode(song_with_modes(), focus, kDelugeControls)}).to_equal(5);
	});
	it("writes an audio track's chain on its clip, or on the track with no clip", _{
		const controls::KnobFocus clip{.owner = controls::ChainFocus{.chain = {project::ChainAddress::Kind::OUTPUT, 2},
		                                                             .owner = controls::ChainOwner::AUDIO_TRACK,
		                                                             .clip = kClip}};
		const controls::KnobFocus track{.owner = controls::ChainFocus{.chain = {project::ChainAddress::Kind::OUTPUT, 2},
		                                                              .owner = controls::ChainOwner::AUDIO_TRACK}};
		expect(controls::knob_owner(clip)->owner).to_equal(project::OwnerId::clip(7));
		expect(controls::knob_owner(track)->owner).to_equal(project::OwnerId::instrument(2));
		expect(int{controls::knob_mode(song_with_modes(), clip, kDelugeControls)}).to_equal(3);
	});
	it("moves a MIDI track's CCs only in a clip, and follows the track's mode either way", _{
		const controls::KnobFocus clip{.owner = controls::MidiFocus{.track = {2}, .clip = kClip}};
		const controls::KnobFocus no_clip{.owner = controls::MidiFocus{.track = {2}}};
		expect(controls::knob_owner(clip)->owner).to_equal(project::OwnerId::clip(7));
		expect(controls::knob_owner(no_clip).has_value()).to_be_false();
		expect(std::get<project::TrackAddress>(*controls::knob_mode_owner(no_clip)).output_id).to_equal(2u);
		expect(int{controls::knob_mode(song_with_modes(), no_clip, kDelugeControls)}).to_equal(3);
	});
	it("caps a mode past the board's last section at the last", _{
		const controls::KnobFocus focus{.owner = controls::SoundFocus{.slot = {project::SlotAddress::Kind::OUTPUT, 6}}};
		expect(int{controls::knob_mode(song_with_modes(), focus, kDelugeControls)}).to_equal(kDelugeControls.sections - 1);
		const controls::KnobFocus song{.owner = controls::SongFocus{}};
		expect(int{controls::knob_mode(song_with_modes(), song, {.sections = 4, .slots = 2})}).to_equal(1);
		expect(int{controls::knob_mode(song_with_modes(), focus, {.sections = 4, .slots = 2})}).to_equal(3);
		expect(int{controls::knob_mode(song_with_modes(), song, {.sections = 1, .slots = 2})}).to_equal(0);
	});
});
// clang-format on

CPPSPEC_SPEC(knob_focus)
