// A source's file finishing loading, or being taken off, is a fact the open menu hears: the holder raises SampleReady,
// an untargeted fact on which the shell repaints the OLED, and the page then lists the rows a loaded sample makes
// apply.

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/sound_osc.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "spec/shell/support/fake_surfaces.h"

#include "definitions_cxx.hpp"
#include "memory/heaps.h"
#include "model/edits/setting_edit.h"
#include "model/sample/sample.h"
#include "notify/dirty_set.h"
#include "processing/sound/sound.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/multi_range/multi_range.h"

#include "support/menu_engine.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
using deluge::ui::shell::SceneKind;
namespace bindings = deluge::scenes::menu::bindings;

namespace {

/// A bare shell over fake surfaces with the menu scene opened on one page.
struct PageRig {
	const deluge::ui::shell::BoardProfile& profile = deluge_profile();
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, deluge_profile()};
	deluge::ui::shell::Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, deluge::ui::shell::ShellConfig{}};

	PageRig() = default;
	PageRig(const PageRig&) = delete;
	PageRig& operator=(const PageRig&) = delete;
	~PageRig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}
	void open(const menu::Node& page, const MenuContext& ctx) {
		(void)menu::assign_node_ids(menu::nodes::kAllRoots);
		menu_scene().open(page, ctx);
		shell.init(deluge::ui::shell::SceneId{SceneKind::FirstApp, menu::node_id(page)});
		shell.tick(5);
	}
};

} // namespace

// clang-format off
describe sample_ready_redraws("a source's file finishing loading, or being taken off", ${
	it("repaints the open page, which then lists the rows a loaded sample makes apply", _ {
		spec::SongFixture f = spec::make_synth_song();
		f.song->setCurrentClip(f.clip);
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		deluge::edits::settingFor(*spec::menu_engine(ctx).sound, "osc1/type", deluge::serialization::Value::makeScalar("sample"));
		PageRig r;
		r.open(menu::nodes::source0, ctx);
		r.shell.tick(20);
		const size_t settled = r.fake.blits.size();
		r.shell.tick(40);
		expect(r.fake.blits.size()).to_equal(settled); // nothing changed: no repaint
		expect(bindings::relevant(menu::nodes::sample0_repeat, ctx)).to_be_false();

		auto* sample = static_cast<AudioFile*>(new (deluge::memory::alloc_fast(sizeof(Sample))) Sample());
		AudioFileHolder* holder = spec::menu_engine(ctx).sound->sources[0].getOrCreateFirstRange()->getAudioFileHolder();
		holder->AudioFileHolder::setAudioFile(sample, false, false, CLUSTER_DONT_LOAD);
		r.shell.tick(60);
		expect(r.fake.blits.size() > settled).to_be_true();
		expect(bindings::relevant(menu::nodes::sample0_repeat, ctx)).to_be_true();

		holder->audioFile = nullptr; // a fake file no project references: unload it as descriptor_legacy_spec does
	});

});

DELUGE_SPEC(sample_ready_spec, sample_ready_redraws)
