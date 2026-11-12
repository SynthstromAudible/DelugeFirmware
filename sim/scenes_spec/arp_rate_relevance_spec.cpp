/// @file
/// @brief The sound arpeggiator's rate is relevant where main's `arpeggiator::Rate::isRelevant` says: not on a MIDI or
///        CV clip, not on a MIDI or gate drum row, and not on a kit's own effects.

#include "gui/ui/menu_view_of.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/chains.h"
#include "scenes/menu/context.h"
#include "scenes/menu/nodes/sound_arp.h"
#include "scenes/menu/nodes/sound_randomizer.h"
#include "scenes/menu/shortcut_overlay.h"
#include "scenes/menu/shortcuts.h"

#include "model/clip/instrument_clip.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "processing/sound/sound_drum.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

namespace menu = deluge::scenes::menu;
namespace spec = deluge::scenes::spec;
using menu::MenuContext;

namespace {

/// @brief What a menu opened over @p f's clip edits; @p kit_global_fx as `context_for` takes it.
MenuContext context_of(const spec::SongFixture& f, bool kit_global_fx = false) {
	return menu::context_for(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, kit_global_fx);
}

bool arp_rate_relevant(const MenuContext& ctx) {
	return menu::bindings::relevant(menu::nodes::arp_rate, ctx);
}

} // namespace

// clang-format off
describe arp_rate_relevance("the sound arpeggiator's rate: where it is relevant", $ {
	it("is relevant on a synth", _ {
		expect(arp_rate_relevant(context_of(spec::make_synth_song()))).to_be_true();
	});

	it("is relevant on a kit row with a sound drum", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		expect(arp_rate_relevant(context_of(f))).to_be_true();
	});

	it("is not relevant on a kit's own effects", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		f.clip->affectEntire = true;
		expect(arp_rate_relevant(context_of(f, /*kit_global_fx=*/true))).to_be_false();
	});

	it("is the kit's own table's (11,0), which the menu's overlay paints black and Shift + pad refuses", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		f.clip->affectEntire = true;
		const MenuContext ctx = context_of(f, /*kit_global_fx=*/true);
		expect(menu::shortcut_node(ctx.root, false, 11, 0) == &menu::nodes::arp_rate).to_be_true();
		const RGB pad = menu::shortcut_colour(ctx, 11, 0);
		expect(pad.r == 0 && pad.g == 0 && pad.b == 0).to_be_true();
		expect(menu::shortcut_applies(menu::nodes::arp_rate, ctx)).to_be_false();
	});

	it("is relevant on a kit's own effects asked with the kit-global latch clear", _ {
		// The clip view's overview right after a menu close: the kit's own table and chain, but legacy's
		// `editingKitAffectEntire()` reads the latch, not the clip.
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		f.clip->affectEntire = true;
		MenuContext ctx = context_of(f, /*kit_global_fx=*/true);
		ctx.is_kit_global_fx = false;
		expect(arp_rate_relevant(ctx)).to_be_true();
	});

	it("is not relevant on a kit's own effects asked with the latch clear and a MIDI drum selected", _ {
		// `editingNonAudioDrumRow()` reads the kit's selected drum, whatever the menu edits.
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<MIDIDrum>(f);
		f.clip->affectEntire = true;
		MenuContext ctx = context_of(f, /*kit_global_fx=*/true);
		ctx.is_kit_global_fx = false;
		expect(ctx.non_audio_drum_row).to_be_true();
		expect(arp_rate_relevant(ctx)).to_be_false();
	});

	it("is not relevant on a MIDI drum row", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<MIDIDrum>(f);
		expect(arp_rate_relevant(context_of(f))).to_be_false();
	});

	it("is not relevant on a gate drum row", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<GateDrum>(f);
		expect(arp_rate_relevant(context_of(f))).to_be_false();
	});

	it("is not relevant on a kit's own effects asked with the latch clear and a gate drum selected", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<GateDrum>(f);
		f.clip->affectEntire = true;
		MenuContext ctx = context_of(f, /*kit_global_fx=*/true);
		ctx.is_kit_global_fx = false;
		expect(ctx.non_audio_drum_row).to_be_true();
		expect(arp_rate_relevant(ctx)).to_be_false();
	});

	it("is not relevant on a MIDI clip", _ {
		expect(arp_rate_relevant(context_of(spec::make_midi_song()))).to_be_false();
	});

	it("is not relevant on a CV clip", _ {
		expect(arp_rate_relevant(context_of(spec::make_cv_song()))).to_be_false();
	});
});

/// @brief @p f's kit, affect-entire on, as the clip view's overview asks it: the kit's own chain, with whether it
///        edits the kit's own effects the latch's @p latched.
MenuContext kit_table_context(spec::SongFixture& f, bool latched) {
	f.clip->affectEntire = true;
	MenuContext ctx = context_of(f, /*kit_global_fx=*/true);
	ctx.is_kit_global_fx = latched;
	return ctx;
}

describe arp_kit_table_relevance("the kit's own shortcut table's arp and randomizer items: where they are relevant", $ {
	it("asks the arp octaves of the latch: relevant with it clear, not with it set", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		expect(menu::bindings::relevant(menu::nodes::arp_octaves, kit_table_context(f, false))).to_be_true();
		expect(menu::bindings::relevant(menu::nodes::arp_octaves, kit_table_context(f, true))).to_be_false();
		// The kit's own menu is latched: its overlay paints (11,3) black and Shift + pad refuses it.
		const MenuContext menu_ctx = kit_table_context(f, true);
		expect(menu::shortcut_node(menu_ctx.root, false, 11, 3) == &menu::nodes::arp_octaves).to_be_true();
		const RGB pad = menu::shortcut_colour(menu_ctx, 11, 3);
		expect(pad.r == 0 && pad.g == 0 && pad.b == 0).to_be_true();
		expect(menu::shortcut_applies(menu::nodes::arp_octaves, menu_ctx)).to_be_false();
	});

	it("does not ask the arp octaves on a gate drum row, latched or not", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<GateDrum>(f);
		expect(menu::bindings::relevant(menu::nodes::arp_octaves, kit_table_context(f, false))).to_be_false();
		expect(menu::bindings::relevant(menu::nodes::arp_octaves, context_of(f))).to_be_false();
	});

	it("asks the arp octaves on a MIDI drum row with the latch clear", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<MIDIDrum>(f);
		expect(menu::bindings::relevant(menu::nodes::arp_octaves, kit_table_context(f, false))).to_be_true();
	});

	it("keeps the arp gate and the velocity spread off with a MIDI drum selected", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<MIDIDrum>(f);
		expect(menu::bindings::relevant(menu::nodes::arp_gate, kit_table_context(f, true))).to_be_false();
		expect(menu::bindings::relevant(menu::nodes::spread_velocity, kit_table_context(f, true))).to_be_false();
	});

	it("keeps the arp gate and the velocity spread on with a sound drum selected", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		expect(menu::bindings::relevant(menu::nodes::arp_gate, kit_table_context(f, true))).to_be_true();
		expect(menu::bindings::relevant(menu::nodes::spread_velocity, kit_table_context(f, true))).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(arp_rate_relevance_spec, arp_rate_relevance, arp_kit_table_relevance)
