#pragma once

/// @file
/// @brief The patching questions the cable pages and the shortcut blinks both ask: may a source modulate a
///        destination, is it patched to it, and which source a patching-source pad names.

#include "scenes/menu/context.h"
#include "synth/modulation/cable.h"

#include <cstdint>

namespace deluge::scenes::menu::patching {

/// @brief `SourceSelection::sourceIsAllowed` (`source_selection.cpp:178-216`): may @p source modulate
///        @p destination on the sound @p ctx edits? The volume triple is one destination.
bool source_is_allowed(const MenuContext& ctx, PatchSource source, deluge::synth::CableTarget destination);

/// @brief `PatchCableSet::isSourcePatchedToDestinationDescriptorVolumeInspecific`: is a cable from @p source to
///        @p destination on the sound @p ctx edits? The volume triple is one destination.
bool source_is_patched(const MenuContext& ctx, PatchSource source, deluge::synth::CableTarget destination);

/// @brief The source the patching-source pad in column 14 + @p sx, row @p y names -- main's `modSourceShortcuts`, or
///        `modSourceShortcutsSecondLayer` for @p second_layer, under the shortcut layout in force
///        (`SHORTCUTS_VERSION_1` names no X/Y pads).
/// @return The source, or `PatchSource::NOT_AVAILABLE` for a pad with none.
PatchSource mod_source_shortcut(uint8_t sx, uint8_t y, bool second_layer);

} // namespace deluge::scenes::menu::patching
