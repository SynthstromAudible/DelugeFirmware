// A source's file finishing loading is a fact the open menu hears: the holder raises SampleReady, an untargeted fact
// the shell repaints the OLED on, and the sample rows that apply only with a loaded file then show.

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/nodes/sound_osc.h"

#include "definitions_cxx.hpp"
#include "memory/heaps.h"
#include "model/edits/setting_edit.h"
#include "model/sample/sample.h"
#include "notify/dirty_set.h"
#include "processing/sound/sound.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/multi_range/multi_range.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
namespace bindings = deluge::scenes::menu::bindings;

// clang-format off
describe sample_ready_redraws("a source's file finishing loading", ${
	it("raises SampleReady, and the rows a loaded sample makes apply then show", _ {
		spec::SongFixture f = spec::make_synth_song();
		f.song->setCurrentClip(f.clip);
		MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		deluge::edits::settingFor(*ctx.sound, "osc1/type", deluge::serialization::Value::makeScalar("sample"));
		expect(bindings::relevant(menu::nodes::sample0_repeat, ctx)).to_be_false();
		(void)deluge::notify::dirty().drain();

		auto* sample = static_cast<AudioFile*>(new (deluge::memory::alloc_fast(sizeof(Sample))) Sample());
		AudioFileHolder* holder = ctx.sound->sources[0].getOrCreateFirstRange()->getAudioFileHolder();
		holder->AudioFileHolder::setAudioFile(sample, false, false, CLUSTER_DONT_LOAD);
		expect(deluge::notify::dirty().drain().has(deluge::notify::Dirty::SampleReady)).to_be_true();
		expect(bindings::relevant(menu::nodes::sample0_repeat, ctx)).to_be_true();

		holder->audioFile = nullptr; // a fake file no project references: unload it as descriptor_legacy_spec does
	});
});

DELUGE_SPEC(sample_ready_spec, sample_ready_redraws)
