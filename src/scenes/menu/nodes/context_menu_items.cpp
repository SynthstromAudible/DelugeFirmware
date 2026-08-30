/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

/// @file
/// @brief `ContextMenuItem`'s three members. See `context_menu_items.h`.

#include "scenes/menu/nodes/context_menu_items.h"

#include "scenes/context_menu/context_menu_scene.h" // on_focus opens this
#include "scenes/menu/menu_scene.h"
#include "scenes/profile.h"

#include "shell/profile.h"
#include "ui/oled/text.h"

namespace deluge::scenes::menu::nodes {

// `context_menu_items.h` constructs the six items from raw `uint16_t` literals because including
// `context_menu_scene.h` there would pull the scene into every consumer of this header. Those
// literals are only correct while `DescriptorId` keeps its current numbering, so the numbering is
// pinned here, in the one translation unit that already sees both. Reorder the enum and this fires,
// instead of six menu items silently opening the wrong menu.
namespace {
namespace cm = deluge::scenes::context_menu;
static_assert(static_cast<uint16_t>(cm::DescriptorId::ResetFlash) == 0);
static_assert(static_cast<uint16_t>(cm::DescriptorId::ResetCommunityFeatures) == 1);
static_assert(static_cast<uint16_t>(cm::DescriptorId::ResetMidiFollow) == 2);
static_assert(static_cast<uint16_t>(cm::DescriptorId::ResetMidiDevices) == 3);
static_assert(static_cast<uint16_t>(cm::DescriptorId::ResetAll) == 4);
static_assert(static_cast<uint16_t>(cm::DescriptorId::AudioSource) == 5);
static_assert(static_cast<uint16_t>(cm::DescriptorId::Count) == 6,
              "every DescriptorId must have an item here, or a descriptor exists that no menu opens");
} // namespace

void ContextMenuItem::render(deluge::ui::oled::OledFrame& f, const MenuContext&, MenuState& state) const {
	// Drawn for at most the one tick between `on_focus` asking for the push and the router applying
	// it -- `EditNameItem::render`'s own reasoning (`nodes/edit_name.cpp`). Legacy never draws these
	// items at all: `selectButtonPress` opens the context menu directly and returns `NO_NAVIGATION`
	// before `SoundEditor` gets to render anything.
	const Node* node = state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
	                                           : std::string_view{});
}

deluge::ui::shell::Result ContextMenuItem::handle(const deluge::ui::shell::UiEvent&,
                                                  const deluge::ui::shell::UiContext&, const MenuContext&,
                                                  MenuState&) const {
	// The context menu owns every input while it is up, and this item is only ever focused for the
	// tick before it appears. Nothing to consume.
	return {};
}

void ContextMenuItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `Reset::selectButtonPress` (`gui/menu_item/reset_settings/reset.h:29-33`) and
	// `AudioSourceSelector::selectButtonPress` (`audio_clip/audio_source_selector.h:27-32`) are the
	// same three statements: point the context menu at what it acts on, `setupAndCheckAvailability()`,
	// `openUI()`. `open()` is the first -- legacy's `resetSettings.setAction(...)` /
	// `audioInputSelector.audioOutput = getCurrentOutput()`, generalised: the descriptor reads
	// whichever member of the context it needs. `on_enter` is the second (the seeding walk, or
	// `initial_option`). The third is the `pending_scene` push.
	deluge::scenes::context_menu::context_menu_scene().open(ctx);
	state.pending_scene =
	    deluge::ui::shell::SceneId{deluge::scenes::context_menu::kContextMenuSceneKind, descriptor_id_};
}

} // namespace deluge::scenes::menu::nodes
