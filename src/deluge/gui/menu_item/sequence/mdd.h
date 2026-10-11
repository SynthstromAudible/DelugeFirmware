#pragma once
#include "gui/menu_item/menu_item.h"
#include "sequencer/mdd/controller.hpp"

namespace deluge::gui::menu_item::sequence::mdd {
extern MenuItem* settingsMenu;
extern MenuItem* valueMenu;
bool owns(MenuItem* item);
// Uses the same SoundEditor menu stack as OSC Source / Plaits Engine.
void open(MenuItem* item);
void control(snake::Control control);
void refreshValue();
void editStep(int offset);
void selectFirstPageColumn(int column);
void armTimer();
ActionResult serviceTimer();
} // namespace deluge::gui::menu_item::sequence::mdd
