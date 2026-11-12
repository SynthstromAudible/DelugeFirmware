/// @file
/// @brief Each piece of state the legacy editor still spells as its own member is the ported menu's one copy.
///
/// The legacy editor reaches the kit-global-FX latch, the shortcut blink, flash byte 60 and the DX7 bank through
/// reference members, so the two menus can never hold different values. This file goes with the legacy editor.

#include "gui/ui/shell_host_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/song/song.h"
#include "util/shell_menu_entry.h"
#include "util/sound_menu_open.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

namespace entry = deluge::gui::ui;
namespace spec = deluge::scenes::spec;

// clang-format off
describe legacy_shared_latch("the legacy editor's setupKitGlobalFXMenu is the ported menu's kit-global-FX latch", $ {
	it("reads what an opening of the ported menu latched", _ {
		entry::clearShellKitGlobalFx();
		spec::SongFixture kit = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(kit);
		static_cast<InstrumentClip*>(kit.clip)->affectEntire = true;
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openShellMenu(kit.clip, 0, false, false, false, /*kitGlobalFx=*/true)).to_be_true();
		expect(soundEditor.setupKitGlobalFXMenu).to_be_true();
		nullifyUIs();
		entry::clearShellKitGlobalFx();
		expect(soundEditor.setupKitGlobalFXMenu).to_be_false();
	});

	it("is what the ported menu reads after the legacy editor wrote it", _ {
		entry::clearShellKitGlobalFx();
		soundEditor.setupKitGlobalFXMenu = true;
		expect(entry::shellKitGlobalFx()).to_be_true();
		expect(entry::soundMenuKitGlobalFx()).to_be_true();
		entry::clearShellKitGlobalFx();
	});

	it("is cleared for both when the ported menu closes", _ {
		entry::clearShellKitGlobalFx();
		spec::SongFixture kit = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(kit);
		static_cast<InstrumentClip*>(kit.clip)->affectEntire = true;
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openShellMenu(kit.clip, 0, false, false, false, /*kitGlobalFx=*/true)).to_be_true();
		(void)shellHostUI.exitUI();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(soundEditor.setupKitGlobalFXMenu).to_be_false();
		expect(entry::shellKitGlobalFx()).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(legacy_shared_state_spec, legacy_shared_latch)
