/// @file
/// @brief Spec for `menu::bindings`: read/write/range/names/learn against a REAL model.

#include "gui/ui/menu_view_of.h"
#include "scenes/menu/bindings.h"
#include "support/menu_engine.h"

#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/sound_filters.h"

#include "gui/l10n/l10n.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "notify/dirty_set.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <string>

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
namespace params = deluge::modulation::params;
namespace bindings = deluge::scenes::menu::bindings;

namespace {

/// @brief An ordinary patched param -- legacy `patched_param::Integer` (`patched_param/integer.cpp`).
constexpr Node lpf_freq_node = menu::param(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_FREQUENCY),
                                           {params::Kind::PATCHED, params::LOCAL_LPF_FREQ});

/// @brief An unpatched (sound) param -- legacy `menu_item::UnpatchedParam` (`unpatched_param.cpp`).
constexpr Node bass_node = menu::param(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_BASS),
                                       {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_BASS});

/// @brief The `Kind::Enum` names table -- two real l10n ids.
constexpr uint16_t kEnumNames[] = {
    static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_DISABLED),
    static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_ENABLED),
};

/// @brief Storage the `toggle_item` / `enumeration` get/set hooks below drive, since a `GetFn` /
///        `SetFn` is a plain function pointer and cannot capture.
bool g_toggle_state = false;
int32_t g_enum_state = 0;

constexpr Node toggle_node = menu::toggle_item(
    static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_ENABLED),
    [](const MenuContext&) { return static_cast<int32_t>(g_toggle_state); },
    [](const MenuContext&, int32_t v) { g_toggle_state = v != 0; });

constexpr Node enum_node = menu::enumeration(
    static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_LPF), kEnumNames,
    [](const MenuContext&) { return g_enum_state; }, [](const MenuContext&, int32_t v) { g_enum_state = v; });

/// @brief Drop whatever earlier specs raised, so a `has()` below is about this write only.
void clear_dirty() {
	(void)deluge::notify::dirty().drain();
}

MenuContext synth_context(spec::SongFixture& f) {
	return menu::context_for(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false,
	                         /*kit_global_fx=*/false);
}

} // namespace

// clang-format off
describe menu_bindings_value("scenes::menu::bindings read/write", $ {
	before_all([] { deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english; });

	it("round-trips a toggle_item through its own get/set", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		g_toggle_state = false;

		expect(bindings::read(toggle_node, ctx)).to_equal(0);
		bindings::write(toggle_node, ctx, 1);
		expect(g_toggle_state).to_be_true();
		expect(bindings::read(toggle_node, ctx)).to_equal(1);
	});
});

describe menu_bindings_notify("scenes::menu::bindings notify", $ {
	it("raises ParamAutomation, ModTarget and GridMain for a param write", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		clear_dirty();

		bindings::write(deluge::scenes::menu::nodes::lpf_freq, ctx, 22);

		const deluge::notify::DirtySet d = deluge::notify::dirty().drain();
		expect(d.has(deluge::notify::Dirty::ParamAutomation)).to_be_true();
		expect(d.has(deluge::notify::Dirty::ModTarget)).to_be_true();
		// The pad half of `possiblyRefreshAutomationEditorGrid` (`automation_view.cpp:625`'s
		// `uiNeedsRendering(&automationView)`): `ParamAutomation`/`ModTarget` reach only `r.oled`
		// (`notify_drain_mapping.cpp:20`), so without GridMain the automation grid would stop
		// repainting.
		expect(d.has(deluge::notify::Dirty::GridMain)).to_be_true();
	});

	it("raises nothing for a param write the app's sink refuses", _ {
		auto f = spec::make_synth_song();
		MenuContext ctx = synth_context(f);
		ctx.addresses.clip = deluge::project::ClipAddress{f.clip->id() + 1000}; // a clip the song does not hold
		clear_dirty();

		bindings::write(deluge::scenes::menu::nodes::lpf_freq, ctx, 22);

		expect(deluge::notify::dirty().drain().any()).to_be_false();
	});

	it("raises Oled for a get/set write", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		clear_dirty();

		bindings::write(toggle_node, ctx, 1);

		const deluge::notify::DirtySet d = deluge::notify::dirty().drain();
		expect(d.has(deluge::notify::Dirty::Oled)).to_be_true();
		// The get/set path is not a param write: no grid, no automation.
		expect(d.has(deluge::notify::Dirty::GridMain)).to_be_false();
		expect(d.has(deluge::notify::Dirty::ParamAutomation)).to_be_false();
	});

	it("raises nothing for a node with neither a param nor a set hook", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		const Node unwritable = menu::submenu(1, {}); // no bind.param, no bind.set
		clear_dirty();

		bindings::write(unwritable, ctx, 1);

		expect(deluge::notify::dirty().drain().any()).to_be_false();
	});

});

describe menu_bindings_range_names("scenes::menu::bindings range/names/learn", $ {
	before_all([] { deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english; });

	it("gives an enum node 0..names-1", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		const menu::IntRange r = bindings::range(enum_node, ctx);
		expect(r.min).to_equal(0);
		expect(r.max).to_equal(1);
	});

	it("gives a toggle node 0..1", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		const menu::IntRange r = bindings::range(toggle_node, ctx);
		expect(r.min).to_equal(0);
		expect(r.max).to_equal(1);
	});

	it("prefers fixed_range over the per-kind default", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		const Node n = menu::int_item(1, [](const MenuContext&) { return 0; }, [](const MenuContext&, int32_t) {},
		                              menu::IntRange{3, 9, 2});
		const menu::IntRange r = bindings::range(n, ctx);
		expect(r.min).to_equal(3);
		expect(r.max).to_equal(9);
		expect(r.step).to_equal(2);
	});

	it("prefers a dynamic range over fixed_range", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);
		Node n = menu::int_item(1, [](const MenuContext&) { return 0; }, [](const MenuContext&, int32_t) {},
		                        menu::IntRange{3, 9, 2});
		n.bind.range = [](const MenuContext&) { return menu::IntRange{-1, 1, 1}; };
		const menu::IntRange r = bindings::range(n, ctx);
		expect(r.min).to_equal(-1);
		expect(r.max).to_equal(1);
	});

	it("resolves enum names through the profile's text_or_digits", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);

		std::string_view out[4]{};
		deluge::ui::shell::TextScratch scratch[4]{};
		const size_t n = bindings::names(enum_node, ctx, out, scratch);

		expect(static_cast<int>(n)).to_equal(2);
		expect(std::string(out[0]))
		    .to_equal(std::string(deluge::l10n::getView(deluge::l10n::String::STRING_FOR_DISABLED)));
		expect(std::string(out[1]))
		    .to_equal(std::string(deluge::l10n::getView(deluge::l10n::String::STRING_FOR_ENABLED)));
	});

	it("fills a dynamic_names node from its own hook", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);

		const Node n = menu::dynamic_enumeration(
		    1,
		    [](const MenuContext&, std::span<std::string_view> out) -> size_t {
			    out[0] = "one";
			    out[1] = "two";
			    out[2] = "three";
			    return 3;
		    },
		    [](const MenuContext&) { return 0; }, [](const MenuContext&, int32_t) {});

		std::string_view out[4]{};
		deluge::ui::shell::TextScratch scratch[4]{};
		expect(static_cast<int>(bindings::names(n, ctx, out, scratch))).to_equal(3);
		expect(std::string(out[2])).to_equal(std::string("three"));
	});

	it("learns the param legacy's descriptor names, patched and unpatched, on the edited sound's controls", _ {
		auto f = spec::make_synth_song();
		const MenuContext ctx = synth_context(f);

		const deluge::modulation::KnobTarget patched = deluge::modulation::knobParam(params::LOCAL_LPF_FREQ); // patched_param.cpp:51-55
		expect(deluge::scenes::spec::learns_as(bindings::learn_target(lpf_freq_node, ctx), patched, ctx)).to_be_true();

		const deluge::modulation::KnobTarget unpatched = deluge::modulation::knobParam(params::UNPATCHED_BASS + params::UNPATCHED_START); // unpatched_param.cpp:88-92
		expect(deluge::scenes::spec::learns_as(bindings::learn_target(bass_node, ctx), unpatched, ctx)).to_be_true();

		expect(bindings::learn_target(toggle_node, ctx).has_value()).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(bindings_spec, menu_bindings_value, menu_bindings_notify, menu_bindings_range_names)
