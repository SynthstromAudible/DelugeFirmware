#pragma once

/// @file
/// @brief The horizontal-menu chains: which page a named node lands on, which pages list it, and whether a node
///        renders as a page.

#include "scenes/menu/context.h"
#include "scenes/menu/node.h"

#include <span>

namespace deluge::scenes::menu {

/// @brief The horizontal page a named @p target opens on under @p root, or `nullptr` when it opens
///        alone -- `SoundEditor::maybeGetParentMenu`.
///
/// Null for the two sample start markers, the audio clip's waveform and the two file selectors,
/// which open their own scenes. Otherwise the first page of @p root's horizontal-menu chain that
/// lists it, the chain transcribed from `SoundEditor::getCurrentHorizontalMenusChain`'s (each group
/// as its member pages; pinned against legacy's search by `shortcuts_spec`).
/// @param root   The menu root.
/// @param target The named node.
/// @return The page, or `nullptr`.
const Node* shortcut_landing_page(MenuContext::Root root, const Node& target);

/// @brief Does @p node render and edit as a horizontal page: a `Layout::Horizontal` submenu with
///        `MenuContext::horizontal_menus` on (`HorizontalMenu::renderingStyle()`, `horizontal_menu.cpp:592-597`)?
/// @param node The node.
/// @param ctx  The menu context.
/// @return Whether it is a horizontal page.
bool renders_horizontal(const Node& node, const MenuContext& ctx);

/// @brief `shortcut_landing_page` for @p ctx's root, and null with `MenuContext::horizontal_menus` off
///        (`getCurrentHorizontalMenusChain` answers nothing).
/// @param ctx    The menu context.
/// @param target The named node.
/// @return The page, or `nullptr`.
const Node* shortcut_landing_page(const MenuContext& ctx, const Node& target);

/// @brief Does @p page list @p column among its children -- a group (`Node::is_group`) through its member pages'
///        children, never the member pages themselves?
/// @param page   A submenu.
/// @param column The child to look for.
/// @return Whether it is listed, relevant or not.
bool page_lists(const Node& page, const Node& column);

/// @brief The `Layout::Horizontal` submenu in @p node's subtree that lists @p item, or null.
///
/// `SoundEditor::maybeGetParentMenu`'s one statement of substance
/// (`gui/ui/sound_editor.cpp`): `find_if(chain, [](HorizontalMenu* menu) { return
/// menu->hasItem(item); })`, over the chain `getCurrentHorizontalMenusChain(false)` picks for the
/// current root (`:2079`, the dispatch at `:2108-2130`).
///
/// **The chain is spelled as reachability rather than as a list**, and that is the one liberty
/// taken. Legacy's chains are curated `deluge::vector`s -- `horizontalMenusChainForSound` is
/// `{&sourceMenuGroup, &voiceMenuGroup, ...}` (`gui/ui/menus.cpp:2270-2272`) -- and there is no
/// port of them, nor of `HorizontalMenuGroup` (`gui/menu_item/horizontal_menu_group.h:26`), whose
/// five members this port models as five ordinary sibling submenus. What both spellings answer for
/// the two items this is reached for is the same page: `file0_selector` is listed by `source0`
/// under the Sound and Drum roots (`nodes/sound_osc.h`'s `kSource0Children`) and by
/// `audio_clip_sample` under the audio-clip root (`nodes/roots.h`'s `kAudioClipSampleChildren`),
/// which is `sourceMenuGroup` focused on `source0Menu` (`menus.cpp:1748-1749`, `:333-349`) and
/// `audioClipSampleMenu` (`:1186-1196`) respectively.
///
/// Static `children` only: a `dynamic_children` node produces its list from a `MenuContext` and
/// none of the roots reaches either file selector through one, so resolving them here would be
/// machinery with no caller.
///
/// The feature guard `getCurrentHorizontalMenusChain` opens with (`gui/ui/sound_editor.cpp:2112-2114`) is the
/// caller's: `reroot_to_scene_opener_page` reads `MenuContext::horizontal_menus` before calling this.
const Node* horizontal_page_listing(const Node& node, const Node& item);

/// @brief The pages of @p root's horizontal-menu chain in order, each group spelled as its member pages --
///        `SoundEditor::getCurrentHorizontalMenusChain`'s (`gui/ui/menus.cpp`, `horizontalMenusChainFor*`).
/// @param root The menu root.
/// @return The chain; empty for a root with none.
std::span<const Node* const> chain_for(MenuContext::Root root);

/// @brief Whether a shortcut pad's @p target may open in @p ctx, the question `SoundEditor::setup`
///        asks of a named item before it opens anything (`checkPermissionToBeginSession`; on `NO`
///        it shows "PARAMETER NOT APPLICABLE" and opens nothing).
///
/// The base rule is relevance. The file selectors answer for themselves and open on any
/// subtractive sound whatever their relevance (`FileSelector::checkPermissionToBeginSession`), so
/// they are let through here and the browser's own check decides.
/// @param target The node the pad names.
/// @param ctx    What the menu would edit.
/// @return False where legacy refuses.
bool shortcut_applies(const Node& target, const MenuContext& ctx);

} // namespace deluge::scenes::menu
