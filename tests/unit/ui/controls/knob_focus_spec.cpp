// clang-format off
#include "board_controls.hpp"
#include "ui/controls/knob_focus.h"
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
		const ui::controls::KnobFocus focus{};
		expect(ui::controls::knob_owner(focus).has_value()).to_be_false();
		expect(int{ui::controls::knob_mode(song, focus, kDelugeControls)}).to_equal(0);
	});
	it("names no owner and no mode owner for an owner with no controls, and mode 0 though its output has one", _{
		const ui::controls::KnobFocus cv{.owner = ui::controls::NoControlsFocus{
		    .kind = ui::controls::NoControlsFocus::Kind::CV_TRACK, .track = {2}, .clip_id = 7}};
		const ui::controls::KnobFocus drum{.owner = ui::controls::NoControlsFocus{
		    .kind = ui::controls::NoControlsFocus::Kind::MIDI_DRUM, .track = {4}, .drum = project::KitRowAddress{4, 11}}};
		for (const ui::controls::KnobFocus& focus : {cv, drum}) {
			expect(ui::controls::knob_owner(focus).has_value()).to_be_false();
			expect(ui::controls::knob_mode_owner(focus).has_value()).to_be_false();
			expect(int{ui::controls::knob_mode(song_with_modes(), focus, kDelugeControls)}).to_equal(0);
			expect(ui::controls::has_no_knobs(focus)).to_be_true();
		}
		expect(ui::controls::has_no_knobs(ui::controls::KnobFocus{})).to_be_true();
		expect(ui::controls::has_no_knobs(ui::controls::KnobFocus{.owner = ui::controls::SongFocus{}})).to_be_false();
		expect(cv.owner).not_().to_equal(drum.owner);
	});
	it("names the song as the owner of the song's knobs", _{
		const ui::controls::KnobFocus focus{.owner = ui::controls::SongFocus{}};
		expect(ui::controls::knob_owner(focus)->owner).to_equal(project::OwnerId::song());
		expect(std::holds_alternative<project::SongAddress>(*ui::controls::knob_mode_owner(focus))).to_be_true();
		expect(int{ui::controls::knob_mode(song_with_modes(), focus, kDelugeControls)}).to_equal(1);
	});
	it("follows a synth's own mode, on the synth", _{
		const ui::controls::KnobFocus focus{
		    .owner = ui::controls::SoundFocus{.slot = {project::SlotAddress::Kind::OUTPUT, 2}, .clip = kClip}};
		expect(ui::controls::knob_owner(focus)->owner).to_equal(project::OwnerId::instrument(2));
		expect(ui::controls::knob_owner(focus)->clip_id).to_equal(std::optional<uint32_t>{7});
		expect(std::get<project::TrackAddress>(*ui::controls::knob_mode_owner(focus)).output_id).to_equal(2u);
		expect(int{ui::controls::knob_mode(song_with_modes(), focus, kDelugeControls)}).to_equal(3);
	});
	it("follows a kit row's kit's mode, on the drum", _{
		const ui::controls::KnobFocus focus{
		    .owner = ui::controls::SoundFocus{.slot = {project::SlotAddress::Kind::DRUM, 11}, .clip = kClip, .kit = 4}};
		expect(ui::controls::knob_owner(focus)->owner).to_equal(project::OwnerId::drum(11));
		expect(std::get<project::TrackAddress>(*ui::controls::knob_mode_owner(focus)).output_id).to_equal(4u);
		expect(int{ui::controls::knob_mode(song_with_modes(), focus, kDelugeControls)}).to_equal(5);
	});
	it("names no mode owner for a drum that names no kit, as its mode is none", _{
		const ui::controls::KnobFocus focus{.owner = ui::controls::SoundFocus{.slot = {project::SlotAddress::Kind::DRUM, 11}}};
		expect(ui::controls::knob_mode_owner(focus).has_value()).to_be_false();
		expect(int{ui::controls::knob_mode(song_with_modes(), focus, kDelugeControls)}).to_equal(0);
	});
	it("follows a kit's mode on its chain, on the kit", _{
		const ui::controls::KnobFocus focus{.owner = ui::controls::ChainFocus{
		                                    .chain = {project::ChainAddress::Kind::OUTPUT, 4}, .clip = kClip}};
		expect(ui::controls::knob_owner(focus)->owner).to_equal(project::OwnerId::instrument(4));
		expect(ui::controls::knob_owner(focus)->clip_id).to_equal(std::optional<uint32_t>{7});
		expect(std::get<project::TrackAddress>(*ui::controls::knob_mode_owner(focus)).output_id).to_equal(4u);
		expect(int{ui::controls::knob_mode(song_with_modes(), focus, kDelugeControls)}).to_equal(5);
	});
	it("writes an audio track's chain on its clip, or on the track with no clip", _{
		const ui::controls::KnobFocus clip{.owner = ui::controls::ChainFocus{.chain = {project::ChainAddress::Kind::OUTPUT, 2},
		                                                             .owner = ui::controls::ChainOwner::AUDIO_TRACK,
		                                                             .clip = kClip}};
		const ui::controls::KnobFocus track{.owner = ui::controls::ChainFocus{.chain = {project::ChainAddress::Kind::OUTPUT, 2},
		                                                              .owner = ui::controls::ChainOwner::AUDIO_TRACK}};
		expect(ui::controls::knob_owner(clip)->owner).to_equal(project::OwnerId::clip(7));
		expect(ui::controls::knob_owner(track)->owner).to_equal(project::OwnerId::instrument(2));
		expect(int{ui::controls::knob_mode(song_with_modes(), clip, kDelugeControls)}).to_equal(3);
	});
	it("moves a MIDI track's CCs only in a clip, and follows the track's mode either way", _{
		const ui::controls::KnobFocus clip{.owner = ui::controls::MidiFocus{.track = {2}, .clip = kClip}};
		const ui::controls::KnobFocus no_clip{.owner = ui::controls::MidiFocus{.track = {2}}};
		expect(ui::controls::knob_owner(clip)->owner).to_equal(project::OwnerId::clip(7));
		expect(ui::controls::knob_owner(no_clip).has_value()).to_be_false();
		expect(std::get<project::TrackAddress>(*ui::controls::knob_mode_owner(no_clip)).output_id).to_equal(2u);
		expect(int{ui::controls::knob_mode(song_with_modes(), no_clip, kDelugeControls)}).to_equal(3);
	});
	it("caps a mode past the board's last section at the last", _{
		const ui::controls::KnobFocus focus{.owner = ui::controls::SoundFocus{.slot = {project::SlotAddress::Kind::OUTPUT, 6}}};
		expect(int{ui::controls::knob_mode(song_with_modes(), focus, kDelugeControls)}).to_equal(kDelugeControls.sections - 1);
		const ui::controls::KnobFocus song{.owner = ui::controls::SongFocus{}};
		expect(int{ui::controls::knob_mode(song_with_modes(), song, {.sections = 4, .slots = 2})}).to_equal(1);
		expect(int{ui::controls::knob_mode(song_with_modes(), focus, {.sections = 4, .slots = 2})}).to_equal(3);
		expect(int{ui::controls::knob_mode(song_with_modes(), song, {.sections = 1, .slots = 2})}).to_equal(0);
	});
});
// clang-format on

CPPSPEC_SPEC(knob_focus)
