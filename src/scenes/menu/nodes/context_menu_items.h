#pragma once

/// @file
/// @brief `ContextMenuItem` -- the one `CustomItem` behind every menu item whose select press opens
///        `context_menu::ContextMenuScene`.
///
// clang-format off
/// | node | legacy item | legacy context menu | `DescriptorId` |
/// |---|---|---|---|
/// | `reset_flash` .. `reset_all` | `reset_settings::Reset` (`gui/menu_item/reset_settings/reset.h:29-33`) | `context_menu::resetSettings` | `ResetFlash` .. `ResetAll` |
/// | `audio_source_selector` | `audio_clip::AudioSourceSelector` (`gui/menu_item/audio_clip/audio_source_selector.h:27-32`) | `context_menu::audioInputSelector` | `AudioSource` |
// clang-format on
///
/// Both legacy classes are the same three statements -- point the context menu at what it acts on,
/// `setupAndCheckAvailability()`, `openUI()` -- and both return `NO_NAVIGATION`. `CustomItem::on_focus`
/// IS `beginSession` (`node.h`), so that is where this does the same thing, except that a scene may
/// not touch the `Shell` stack itself: it records the push in `MenuState::pending_scene` and
/// `MenuScene::after_render` turns it into an `Action`. `EditNameItem` (`nodes/edit_name.h`) has the
/// identical shape for `rename::RenameScene`.
///
/// @note **This was `nodes::ResetItem`, in `settings.h`.** E2 task 11b widened it: the sixth
///       descriptor (`audio_source_selector`) is declared in `roots.h`, which `settings.h` includes
///       rather than the other way round, so the class had to move somewhere both can see. One class
///       and six instances beats two identical classes in two headers; nothing about the five reset
///       items changed but the name.
///
/// @note Spelled with a plain `uint16_t` rather than `context_menu::DescriptorId`, for the same
///       reason `EditNameItem::target_` is a `uint16_t` rather than a `rename::TargetId`: a
///       `SceneId::arg` carries a `uint16_t`, and this header should not have to pull
///       `scenes/context_menu/` into every table that names an item. The numbering is pinned by
///       `static_assert` in the one translation unit that sees both (`context_menu_items.cpp`).

#include "scenes/menu/node.h"

#include <cstdint>

namespace deluge::scenes::menu::nodes {

/// @brief One menu item that opens a context menu on a named descriptor.
class ContextMenuItem final : public CustomItem {
public:
	/// @param descriptor_id The `context_menu::DescriptorId` this item opens, as the plain
	///                      `uint16_t` a `SceneId` carries.
	explicit ContextMenuItem(uint16_t descriptor_id) : descriptor_id_{descriptor_id} {}

	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	void on_focus(const MenuContext& ctx, MenuState& state) const override;

	/// @brief Which descriptor this item opens. Exposed so a spec can assert the item-to-descriptor
	///        mapping by identity rather than re-deriving it.
	uint16_t descriptor_id() const { return descriptor_id_; }

private:
	uint16_t descriptor_id_;
};

/// @name The six instances, one per `context_menu::DescriptorId`.
///
/// `inline const`, not `constexpr` (they have a vtable); only their addresses need to be constant
/// expressions, which is all `custom()` needs.
/// @{
inline const ContextMenuItem kResetFlashItem{0};             ///< `DescriptorId::ResetFlash`
inline const ContextMenuItem kResetCommunityFeaturesItem{1}; ///< `DescriptorId::ResetCommunityFeatures`
inline const ContextMenuItem kResetMidiFollowItem{2};        ///< `DescriptorId::ResetMidiFollow`
inline const ContextMenuItem kResetMidiDevicesItem{3};       ///< `DescriptorId::ResetMidiDevices`
inline const ContextMenuItem kResetAllItem{4};               ///< `DescriptorId::ResetAll`
inline const ContextMenuItem kAudioSourceItem{5};            ///< `DescriptorId::AudioSource`
/// @}

} // namespace deluge::scenes::menu::nodes
