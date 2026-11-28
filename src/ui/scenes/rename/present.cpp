// What the rename editor's two surfaces show, as the plain structs the views draw. Const members: nothing here writes
// the scene or reads a frame.
#include "ui/scenes/profile.h"
#include "ui/scenes/rename/rename_scene.h"
#include "ui/widgets/grid/keyboard.h"

namespace deluge::ui::scenes::rename {

views::oled::RenameWindow RenameScene::present_window(shell::TextScratch& title) const {
	return {.title = deluge_profile().text_or_digits(target(target_).title, title), .text = text_};
}

views::grid::KeyboardPads RenameScene::present_keys() const {
	return {.layout = layout_, .home_row = widgets::grid::kTextKeyboardHomeRow, .dark = false};
}

} // namespace deluge::ui::scenes::rename
