#!/usr/bin/env python3
"""Real Custom menu classes with a minimal UI shell; check grid bounds and independent edits."""
from pathlib import Path
import tempfile, subprocess, os
root=Path(__file__).resolve().parents[3]
header=(root/'src/deluge/gui/menu_item/snake_custom.h').read_text()
body=header[header.index('namespace deluge::gui::menu_item::snake_custom'):]
code='''#include "sequencer/snake.h"
#include <array>
#include <vector>
#include <string_view>
#include <initializer_list>
#include <cstdio>
#include <cassert>
#include <iostream>
constexpr int OLED_MAIN_TOPMOST_PIXEL=5, kTextSmallSpacingX=4, kTextSmallSizeY=5;
namespace deluge {
template<class T> using vector=std::vector<T>;
namespace l10n { enum class String { EMPTY_STRING }; }
namespace hid::display {
struct Canvas {
 unsigned texts=0, highlights=0;
 void drawStringCentered(const char* text,int x,int y,int spacing,int height,int width) {
  assert(x>=0 && x+width<=128 && y>=18 && y+height<=48);
  assert(spacing==4 && height==5 && text[0]); ++texts;
 }
 void invertArea(int x,int width,int y,int end) {
  assert(x>=0 && x+width<=128 && y>=18 && end<48); ++highlights;
 }
};
struct OLED { static inline Canvas main; };
}
namespace gui::menu_item {
class MenuItem { public:
 virtual ~MenuItem()=default;
 virtual std::string_view getName() const { return {}; }
 virtual std::string_view getTitle() const { return {}; }
};
class Submenu : public MenuItem { public: Submenu(l10n::String,std::initializer_list<MenuItem*>) {} };
class Integer : public MenuItem { public:
 int32_t value=0;
 void setValue(int32_t v) { value=v; } int32_t getValue() const { return value; }
 virtual int32_t getMinValue() const { return 0; } virtual int32_t getMaxValue() const { return 100; }
 virtual void readCurrentValue() {} virtual void writeCurrentValue() {} virtual void drawPixelsForOled() {}
};
class Selection : public Integer { public:
 enum class OptType { FULL, SHORT };
 virtual vector<std::string_view> getOptions(OptType=OptType::FULL)=0;
};
}}
struct Sound { bool snakeCustomEnabled=false; deluge::sequencer::SnakeConfig snakeCustomConfig; void updateSnakeCustom() {} };
struct Editor { Sound* currentSound=nullptr; } soundEditor;
'''
code+=body
code+='''int main() {
 using namespace deluge::gui::menu_item::snake_custom;
 Sound sound; soundEditor.currentSound=&sound;
 LaneMenu a(0,"Custom 1"), b(1,"Custom 2");
 a.path.setValue(4); a.path.writeCurrentValue();
 assert(sound.snakeCustomConfig.modulation[0].path==deluge::sequencer::SnakePath::SerpentineColumns);
 assert(sound.snakeCustomConfig.modulation[1].path==deluge::sequencer::SnakePath::Rows);
 a.step.setValue(16); a.step.writeCurrentValue();
 a.value.setValue(100); a.value.writeCurrentValue();
 assert(sound.snakeCustomConfig.modulation[0].values[15]==65535);
 assert(sound.snakeCustomConfig.modulation[1].values[15]==0);
 a.value.drawPixelsForOled();
 assert(deluge::hid::display::OLED::main.texts==16 && deluge::hid::display::OLED::main.highlights==1);
 a.phase.setValue(99); a.phase.writeCurrentValue();
 assert(sound.snakeCustomConfig.modulation[0].phase==15);
 a.enabled.setValue(0); a.enabled.writeCurrentValue();
 assert(!sound.snakeCustomConfig.modulation[0].enabled && sound.snakeCustomConfig.modulation[1].enabled);
 for (unsigned i=0;i<4;++i) assert(sound.snakeCustomConfig.mappings[i].target==deluge::sequencer::SnakeTarget::None);
 assert(a.path.getOptions().size()==5);
 std::cout << "Snake menu independent cell/path/phase edits and 48-pixel OLED grid bounds PASS\\n";
}'''
with tempfile.TemporaryDirectory() as tmp:
 src=Path(tmp)/'probe.cpp'; src.write_text(code); out=Path(tmp)/'probe'
 subprocess.run(['g++','-std=c++23','-O1','-fsanitize=address,undefined','-I'+str(root/'src/deluge'),str(src),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
