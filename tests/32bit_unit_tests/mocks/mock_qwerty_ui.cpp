#include "gui/ui/qwerty_ui.h"
bool QwertyUI::predictionInterrupted;
String QwertyUI::enteredText{};
// entered text edit position is the first difference from
// the previously seen name while browsing/editing
int16_t QwertyUI::enteredTextEditPos;
int32_t QwertyUI::scrollPosHorizontal;
