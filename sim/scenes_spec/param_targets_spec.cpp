// A sound's param menus show where their owner's rows say, as legacy's items' `isRelevant` do, in the states the batch
// tables' fixtures do not visit: the randomizer's columns only while the arp is on (a synth's chord columns not on a
// kit row), and an oscillator's pulse width only for a type that has one.

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/nodes/sound_arp.h"
#include "scenes/menu/nodes/sound_osc.h"
#include "scenes/menu/nodes/sound_randomizer.h"

#include "model/edits/setting_edit.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;

namespace {

namespace nodes = menu::nodes;

MenuContext at(Song& song, Clip* clip) {
	song.setCurrentClip(clip);
	return menu::context_for(song, clip, 0, false, false, false, false);
}

/// Turns the arp of @p ctx's root on or off, through its menu.
void set_arp(const MenuContext& ctx, bool on) {
	menu::bindings::write(nodes::arp_mode, ctx, on ? 1 : 0);
}

} // namespace

// clang-format off
describe param_targets("a sound's param menus show where their owner's rows say", ${
	it("shows a randomizer column only while the arp is on", _ {
		spec::SongFixture f = spec::make_synth_song();
		const MenuContext ctx = at(*f.song, f.clip);
		set_arp(ctx, false);
		expect(menu::bindings::relevant(nodes::spread_gate, ctx)).to_be_false();
		set_arp(ctx, true);
		expect(menu::bindings::relevant(nodes::spread_gate, ctx)).to_be_true();
	});

	it("shows a synth's chord column on a synth with the arp on, and never on a kit row", _ {
		spec::SongFixture synth = spec::make_synth_song();
		const MenuContext on_synth = at(*synth.song, synth.clip);
		set_arp(on_synth, true);
		expect(menu::bindings::relevant(nodes::chord_polyphony, on_synth)).to_be_true();

		spec::SongFixture kit = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(kit);
		const MenuContext on_row = at(*kit.song, kit.clip);
		set_arp(on_row, true);
		expect(menu::bindings::relevant(nodes::spread_gate, on_row)).to_be_true();
		expect(menu::bindings::relevant(nodes::chord_polyphony, on_row)).to_be_false();
	});

	it("shows an oscillator's pulse width for a waveform, not for a sample or an unloaded wavetable", _ {
		spec::SongFixture f = spec::make_synth_song();
		const MenuContext ctx = at(*f.song, f.clip);
		Sound& sound = *static_cast<SoundInstrument*>(f.instrument);
		deluge::edits::settingFor(sound, "osc1/type", deluge::serialization::Value::makeScalar("square"));
		expect(menu::bindings::relevant(nodes::osc0_pulse_width, ctx)).to_be_true();
		deluge::edits::settingFor(sound, "osc1/type", deluge::serialization::Value::makeScalar("sample"));
		expect(menu::bindings::relevant(nodes::osc0_pulse_width, ctx)).to_be_false();
		deluge::edits::settingFor(sound, "osc1/type", deluge::serialization::Value::makeScalar("wavetable"));
		expect(menu::bindings::relevant(nodes::osc0_pulse_width, ctx)).to_be_false();
		expect(menu::bindings::relevant(nodes::source0_wave_index, ctx)).to_be_false();
	});
});

CPPSPEC_SPEC(param_targets)
