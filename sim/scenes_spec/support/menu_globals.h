#pragma once

/// @file
/// @brief The `soundEditor` members app code outside the menu still reads for the sound a menu edits: set from a
///        fixture's context, as a menu open sets them.

#include "gui/ui/sound_editor.h"
#include "scenes/menu/context.h"
#include "support/menu_engine.h"

namespace deluge::scenes::spec {

/// @brief Point `soundEditor`'s context members at what @p ctx edits.
/// @param allows_note_tails What `SoundEditor::setup` caches for the sound (`sound_allows_note_tails`).
inline void install_editor_carrier(const deluge::scenes::menu::MenuContext& ctx, bool allows_note_tails) {
	const MenuEngine e = menu_engine(ctx);
	soundEditor.currentSound = e.sound;
	soundEditor.currentModControllable = e.mod;
	soundEditor.currentParamManager = e.params;
	soundEditor.currentSourceIndex = ctx.source_index;
	soundEditor.currentSource = e.source;
	soundEditor.currentMultiRange = e.range;
	soundEditor.currentMultiRangeIndex = ctx.range_index;
	soundEditor.currentArpSettings = e.arp;
	soundEditor.currentSidechain = e.mod != nullptr ? &e.mod->core().sidechain() : nullptr;
	soundEditor.allowsNoteTails = allows_note_tails;
	soundEditor.setupKitGlobalFXMenu = ctx.is_kit_global_fx;
}

} // namespace deluge::scenes::spec
