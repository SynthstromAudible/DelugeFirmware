// The app's write sink for the scenes' target nodes (gui/ui/shell_bind_host.cpp): a target node's write becomes its
// Edit on the sound's slot, fans out over a kit's sound drums where the row applies, and is refused for a slot that
// does not resolve. Against legacy ::Song fixtures, as the sink applies Edits to a running song.

#include "scenes/bind/dispatch.h"
#include "scenes/bind/write_sink.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/sound_osc.h"

#include "engines/deluge/codec.h"
#include "engines/deluge/sound_state.h"
#include "gui/ui/shell_bind_host.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <string>

using namespace deluge::scenes;
using deluge::scenes::bind::Target;
using deluge::scenes::menu::Kind;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::serialization::Value;
namespace bindings = deluge::scenes::menu::bindings;

namespace {

constexpr Node kMaxVoices = menu::setting_item(1, Kind::Int, Target{.kind = Target::Kind::SOUND, .path = "maxVoices"});
constexpr Node kOsc1Type = menu::setting_item(2, Kind::Enum, Target{.kind = Target::Kind::SOUND, .path = "osc1/type"});

void set_polyphony(::Sound& sound, const char* word) {
	deluge::edits::settingFor(sound, "polyphonic", Value::makeScalar(word));
}

/// A kit row with no note row: its sound has a slot, but legacy's menu refuses to open on it.
SoundDrum* add_drum_without_note_row(spec::SongFixture& f) {
	auto* kit = static_cast<Kit*>(f.instrument);
	auto* drum = spec::detail::place<SoundDrum>();
	ParamManagerForTimeline params;
	params.setupWithPatching();
	Sound::initParams(&params);
	drum->setupAsSample(&params);
	f.song->live.param_backup.backUp(drum, f.clip, &params, true);
	kit->addDrum(drum);
	kit->selectedDrum = drum;
	return drum;
}

} // namespace

// clang-format off
describe scenes_edit_sink_writes("a target node's write through the app's sink", ${
	it("sends one request to the menu's sink, and none for a stored choice that no longer applies", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		deluge::edits::settingFor(*drum, "osc1/type", Value::makeScalar("dx7"));
		MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		bind::RecordingSink recorded;
		ctx.sink = &recorded;
		bindings::write(kMaxVoices, ctx, 4);
		expect(recorded.requests.size()).to_equal(size_t(1));
		expect(recorded.requests[0].menu_value).to_equal(4);
		const auto shown = bind::view(kOsc1Type.bind.target, *ctx.model, ctx.addresses, *ctx.facts);
		expect(shown.has_value() && shown->stale.has_value()).to_be_true();
		bindings::write(kOsc1Type, ctx, *shown->stale);
		expect(recorded.requests.size()).to_equal(size_t(1));
	});

	it("applies a synth's voice count through its Edit, record and runtime alike", _ {
		spec::SongFixture f = spec::make_synth_song();
		f.song->setCurrentClip(f.clip);
		MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		set_polyphony(*ctx.sound, "poly");
		bindings::write(kMaxVoices, ctx, 5);
		expect(static_cast<int>(ctx.sound->boundState()->max_voice_count)).to_equal(5);
		expect(static_cast<int>(ctx.sound->maxVoiceCount)).to_equal(5);
		expect(bindings::read(kMaxVoices, ctx)).to_equal(5);
	});

	it("writes a kit's sound drums where the row applies, and no MIDI or gate drum, under affect-entire", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* poly = spec::add_drum<SoundDrum>(f);
		SoundDrum* mono = spec::add_drum<SoundDrum>(f);
		MIDIDrum* midi = spec::add_drum<MIDIDrum>(f);
		GateDrum* gate = spec::add_drum<GateDrum>(f);
		static_cast<Kit*>(f.instrument)->selectedDrum = poly;
		set_polyphony(*poly, "poly");
		set_polyphony(*mono, "mono");
		const int before = mono->boundState()->max_voice_count;
		const auto midi_channel = midi->channel();
		const auto gate_channel = gate->channel();
		MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		ctx.affect_entire_held = true;
		ctx.addresses.affect_entire = bindings::writes_whole_kit(ctx);
		expect(ctx.addresses.affect_entire).to_be_true();
		bindings::write(kMaxVoices, ctx, 3);
		expect(static_cast<int>(poly->boundState()->max_voice_count)).to_equal(3);
		expect(static_cast<int>(mono->boundState()->max_voice_count)).to_equal(before);
		expect(midi->channel() == midi_channel && gate->channel() == gate_channel).to_be_true();
	});

	it("writes only the selected drum, under affect-entire, for a setting legacy never writes kit-wide", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* other = spec::add_drum<SoundDrum>(f);
		SoundDrum* selected = spec::add_drum<SoundDrum>(f);
		static_cast<Kit*>(f.instrument)->selectedDrum = selected;
		const auto before = other->boundState()->sources[0].osc_type;
		MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		ctx.affect_entire_held = true;
		ctx.addresses.affect_entire = bindings::writes_whole_kit(ctx);
		bindings::write(menu::nodes::osc0_type, ctx, 4); // the fifth listed type: saw
		expect(selected->boundState()->sources[0].osc_type == deluge::instrument::OscType::SAW).to_be_true();
		expect(other->boundState()->sources[0].osc_type == before).to_be_true();
	});

	it("binds nothing through the app for a song it does not play", _ {
		spec::SongFixture other = spec::make_synth_song();
		spec::SongFixture playing = spec::make_synth_song(); // the fixture makes the last song built the current one
		const MenuContext ctx = menu::context_for(*other.song, other.clip, 0, false, false, false, false);
		expect(ctx.facts == nullptr && ctx.sink == nullptr).to_be_true();
		expect(bindings::relevant(kMaxVoices, ctx)).to_be_false();
		(void)playing;
	});

	it("keeps each sound's slot and typed state equal after its menus write it", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		bindings::write(kMaxVoices, ctx, 7);
		bindings::write(menu::nodes::osc0_type, ctx, 4);
		const deluge::project::InstrumentSlot* slot = f.song->project.resolveSlot(*ctx.addresses.slot);
		expect(slot != nullptr).to_be_true();
		const auto decoded = deluge::instrument::decode_onto(slot->state, *drum->boundState(),
		                                                     {.input_tick_magnitude = slot->tick_magnitude});
		expect(decoded.has_value()).to_be_true();
		expect(deluge::instrument::dump_state(*decoded) == deluge::instrument::dump_state(*drum->boundState()))
		    .to_be_true();
	});

	it("refuses a request whose slot does not resolve, and changes nothing", _ {
		spec::SongFixture f = spec::make_synth_song();
		f.song->setCurrentClip(f.clip);
		MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		const int before = ctx.sound->boundState()->max_voice_count;
		const uint32_t refused = deluge::gui::shellEditRefusals();
		menu::host_write_sink().write(bind::WriteRequest{
		    .target = kMaxVoices.bind.target,
		    .where = bind::Addresses{.slot = deluge::project::SlotAddress{deluge::project::SlotAddress::Kind::DRUM, 9999}},
		    .menu_value = 2});
		expect(deluge::gui::shellEditRefusals()).to_equal(refused + 1);
		expect(static_cast<int>(ctx.sound->boundState()->max_voice_count)).to_equal(before);
	});

	it("shows no target node on a kit row with no note row", _ {
		spec::SongFixture f = spec::make_kit_song();
		add_drum_without_note_row(f);
		MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		expect(ctx.resolved).to_be_false();
		expect(bind::view(kMaxVoices.bind.target, *ctx.model, ctx.addresses, *ctx.facts).has_value()).to_be_false();
		expect(bindings::relevant(kMaxVoices, ctx)).to_be_false();
	});
});

DELUGE_SPEC(scenes_edit_sink_spec, scenes_edit_sink_writes)
