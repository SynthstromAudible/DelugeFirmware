"""Exercise production MDD menu classes through native encoder write paths.
Hardware-independent doubles replace display/model services; menu/edit logic is
read from the firmware sources, including Value/Integer/Enumeration methods.
"""

import subprocess
import tempfile
from pathlib import Path

r = Path(__file__).resolve().parents[3]


def method(path, name):
    s = (r / path).read_text()
    a = s.index(name)
    i = s.index("{", a) + 1
    depth = 1
    while depth:
        depth += (s[i] == "{") - (s[i] == "}")
        i += 1
    return s[a:i]


native = (r / "src/deluge/gui/menu_item/sequence/mdd.cpp").read_text()
native = "\n".join(
    line for line in native.splitlines() if not line.startswith("#include")
)
mode = (r / "src/deluge/gui/menu_item/sequence/mode.h").read_text()
mode = "\n".join(line for line in mode.splitlines() if not line.startswith("#"))
root = (r / "src/deluge/gui/views/instrument_clip_view.cpp").read_text()
a = root.index("ActionResult InstrumentClipView::padAction(")
b = root.index("\n\t// Drum Randomizer", a)
pad = root[a:b] + "return ActionResult::NOT_DEALT_WITH;\n}"
a = root.index("ActionResult InstrumentClipView::buttonAction(")
b = root.index("\tmaybeStartShortcutOverview", a)
button = root[a:b] + "return ActionResult::NOT_DEALT_WITH;\n}"
a = root.index("ActionResult InstrumentClipView::timerCallback()")
b = root.index("\n\tusing namespace", a)
timer = root[a:b] + "return ActionResult::NOT_DEALT_WITH;\n}"
editor = (r / "src/deluge/gui/ui/sound_editor.cpp").read_text()
a = editor.index("ActionResult SoundEditor::padAction(")
b = editor.index("\n\tif (!inSettingsMenu()", a)
editor_pad = editor[a:b] + "++stockShortcuts; return ActionResult::NOT_DEALT_WITH;\n}"
a = editor.index("ActionResult SoundEditor::buttonAction(")
b = editor.index("\n\tusing namespace", a)
editor_button = editor[a:b] + "return ActionResult::NOT_DEALT_WITH;\n}"
a = editor.index("bool SoundEditor::getGreyoutColsAndRows(")
b = editor.index("\tbool doGreyout = true;", a)
greyout = editor[a:b] + "return true;\n}"

stubs = r"""
#include "sequencer/mdd/controller.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <iterator>
namespace deluge {template<class T> using vector=std::vector<T>; namespace hid {using Button=int; namespace button {constexpr int SELECT_ENC=0,BACK=1,SHIFT=2,SYNTH=3,KIT=4,MIDI=5,CV=6;}} namespace l10n {enum class String{EMPTY_STRING,STRING_FOR_TYPE};}}
enum class ActionResult {DEALT_WITH,NOT_DEALT_WITH,REMIND_ME_OUTSIDE_CARD_ROUTINE};
enum class ClipType{INSTRUMENT}; enum class OutputType{SYNTH,KIT,MIDI};
enum class PatchSource{SNAKE_CUSTOM_1}; enum class PatchCableAcceptance{DISALLOWED,ALLOWED};
enum Scale{MAJOR,MINOR,NUM_PRESET_SCALES,USER_SCALE=NUM_PRESET_SCALES};
constexpr int UI_MODE_NONE=0, MODEL_STACK_MAX_SIZE=64; int currentUIMode=0;
struct ModControllableAudio{};
enum class Error{NONE,INSUFFICIENT_RAM};
struct Display { bool haveOLED(){return true;} bool have7SEG(){return false;} std::string popup;void displayPopup(const char*s){popup=s;}void cancelPopup(){popup.clear();}void displayError(Error){} } displayObject,*display=&displayObject;
struct Buttons {static inline bool held=false, shift=false,selectButtonPressUsedUp=false;static bool isButtonPressed(int){return held;}static bool isShiftButtonPressed(){return shift;}};
struct UI{virtual ~UI()=default;}; UI *activeUI=nullptr,*rootUI=nullptr;
using RootUI=UI;
UI* getCurrentUI(){return activeUI;}UI* getRootUI(){return rootUI;}
namespace PadLEDs {void reassessGreyout(){}void clearTickSquares(){}}
void uiNeedsRendering(UI*){} void renderUIsForOled(){}
enum class TimerName{UI_SPECIFIC}; struct TimerManager{void setTimer(TimerName,int){}}uiTimerManager;
bool sdRoutineLock=false;
struct StringBuf{std::string value;void append(const char*s){value+=s;}};
struct SlotPosition{int start_x,start_y,width,height;};
namespace deluge::hid::display {struct Canvas{std::string text;void drawStringCentered(const char*s,int,int,int,int,int width){text=s;assert(text.size()*6<=unsigned(width));}};struct OLED{static inline Canvas main;static void setupSideScroller(int,std::string_view,int,int,int,int,int,int,bool,bool){}};}
class MenuItem {public:virtual void renderOLED(){}virtual int32_t getOccupiedSlots()const{return 1;}virtual void getColumnLabel(StringBuf&){}virtual void renderInHorizontalMenu(const SlotPosition&){} virtual ~MenuItem()=default;virtual std::string_view getName()const{return "";}virtual std::string_view getTitle()const{return "";}virtual bool isRelevant(ModControllableAudio*,int32_t)const{return true;} virtual void beginSession(MenuItem*){}virtual ActionResult timerCallback(){return ActionResult::DEALT_WITH;}virtual void readCurrentValue(){}virtual void readValueAgain(){readCurrentValue();}virtual bool isSubmenu(){return false;}virtual bool selectEncoderActionIsPermitted(){return true;}virtual void selectEncoderAction(int32_t){}virtual MenuItem* selectButtonPress(){return nullptr;}virtual bool shouldEnterSubmenu(){return true;}};
#define NO_NAVIGATION ((MenuItem*)0xFFFFFFFF)
struct TestPatched{int getValue(int){return 0;}};struct TestParams{TestPatched p;TestPatched*getPatchedParamSet(){return &p;}}testParams;
struct SoundEditor : UI {TestParams*currentParamManager=&testParams;MenuItem* current=nullptr;int opened=0,stockShortcuts=0; uint8_t navigationDepth=0;MenuItem* menuItemNavigationRecord[16]{};
 MenuItem* getCurrentMenuItem(){return current;}
 bool setup(void*,const MenuItem* item=nullptr,int=0){current=const_cast<MenuItem*>(item);navigationDepth=0;menuItemNavigationRecord[0]=current;++opened;return true;}
 bool beginScreen(MenuItem* from=nullptr){current->beginSession(from);return true;}
 bool getGreyoutColsAndRows(uint32_t*,uint32_t*);
 void focusRegained(){Buttons::selectButtonPressUsedUp=true;beginScreen();}
 ActionResult exitCompletely(){activeUI=rootUI;return ActionResult::DEALT_WITH;}
 ActionResult padAction(int32_t,int32_t,int32_t);ActionResult buttonAction(deluge::hid::Button,bool,bool);
} soundEditor;
void openUI(SoundEditor* ui){activeUI=ui;ui->beginScreen();}
namespace deluge::gui::menu_item {
template<class T=int32_t> class Value:public MenuItem {T value_{};public:void setValue(T v){value_=v;}T getValue(){return value_;}void beginSession(MenuItem*)override;void selectEncoderAction(int32_t)override;protected:virtual void writeCurrentValue(){}virtual void drawValue(){} };
class Number:public Value<int32_t>{protected:virtual int32_t getMaxValue()const=0;virtual int32_t getMinValue()const{return 0;}};
class Integer:public Number{public:virtual const char* getUnit(){return "";}void selectEncoderAction(int32_t)override;};
namespace patched_param{class Integer:public menu_item::Integer{int p_;public:Integer(l10n::String,int p):p_(p){}int getP(){return p_;}virtual int32_t getFinalValue(){return 0;}int32_t getMaxValue()const override{return 100;}};}
class Enumeration:public Value<int32_t>{public:virtual size_t size()=0;bool wrapAround(){return false;}void selectEncoderAction(int32_t)override;};
class Selection:public Enumeration{public:enum class OptType{FULL,SHORT};virtual deluge::vector<std::string_view>getOptions(OptType=OptType::FULL)=0;size_t size()override{return getOptions().size();}MenuItem* selectButtonPress()override{return nullptr;}};
class Submenu:public MenuItem{public:enum RenderingStyle{VERTICAL,HORIZONTAL};virtual RenderingStyle renderingStyle()const{return VERTICAL;}bool isSubmenu()override{return true;}std::vector<MenuItem*> items;Submenu(l10n::String,std::initializer_list<MenuItem*>i):items(i){}void beginSession(MenuItem*)override{} };
class HorizontalMenu:public Submenu {MenuItem* selected{};public:int lastSelectedItemPosition=-1;enum Layout{FIXED,DYNAMIC};HorizontalMenu(l10n::String n,std::initializer_list<MenuItem*> i,Layout):Submenu(n,i),selected(*i.begin()){}decltype(items)&getItems(){return items;}MenuItem*getCurrentItem(){return selected;}bool focusChild(const MenuItem*p){selected=const_cast<MenuItem*>(p);selected->readCurrentValue();return true;}void selectEncoderAction(int32_t)override;double calcNextKnobSpeed(int){return 1;}void displayNotification(MenuItem*){} void chooseColumn(int n){focusChild(items.at(n));}};
namespace sequence::mdd {extern MenuItem*settingsMenu;extern MenuItem*valueMenu;bool owns(MenuItem*);void open(MenuItem*);void control(snake::Control);void refreshValue();void editStep(int);void selectFirstPageColumn(int);void armTimer();ActionResult serviceTimer();}
}
int mod(int a,int b){return (a%b+b)%b;}
struct Sound:ModControllableAudio{PatchCableAcceptance maySourcePatchToParam(PatchSource,int p,void*){return p<4?PatchCableAcceptance::ALLOWED:PatchCableAcceptance::DISALLOWED;}}sound;
struct Output {ModControllableAudio*toModControllable(){return &sound;}}output;
struct Clip {ClipType type=ClipType::INSTRUMENT;snake::Controller mdd;bool mddRequested=true,mddEnabled=true,inScaleMode=false;uint8_t mddBakedCustom=0;Output*output=&::output;int paramManager=0;bool failRoute=false;int routeCalls=0;
 int writes=0;bool failWrite=false;Error writeMddToNormal(void*){++writes;if(failWrite)return Error::INSUFFICIENT_RAM;mddRequested=mddEnabled=false;return Error::NONE;}
 void requestMdd(bool b){mddRequested=mddEnabled=b;}
 bool applyMddRoute(void*,int,const snake::Route&,const snake::Route&){++routeCalls;return !failRoute;}
}clipObject;
using InstrumentClip=Clip;
Clip*getCurrentClip(){return &clipObject;}Clip*getCurrentInstrumentClip(){return &clipObject;}
OutputType outputType=OutputType::SYNTH;OutputType getCurrentOutputType(){return outputType;}
struct Song{struct{int rootNote=0;}key;bool failScale=false;Scale scale=MAJOR;int getQuarterNoteLength(){return 24;}Scale getCurrentScale(){return scale;}void transposeAllScaleModeClips(int n){key.rootNote+=n;}Scale setScale(Scale s){if(failScale)return USER_SCALE;scale=s;return s;}void*setupModelStackWithCurrentClip(char*){return this;}}song,*currentSong=&song;
const char*getScaleName(Scale s){return s==MAJOR?"Major":s==MINOR?"Minor":"User";}
namespace deluge::modulation::params {enum class Kind{PATCHED};constexpr int GLOBAL_MDD_CUSTOM_1=4,GLOBAL_NONE=8,LOCAL_PLAITS_A_ENGINE=2,LOCAL_PLAITS_B_ENGINE=3;const char* getParamDisplayName(Kind,int){return "Target";}}
constexpr int kDisplayWidth=16,kHorizontalMenuSlotYOffset=5,kTextSpacingX=6,kTextSpacingY=7,OLED_MAIN_WIDTH_PIXELS=128,OLED_MAIN_TOPMOST_PIXEL=0,kTextTitleSizeY=10,kTextTitleSpacingX=9;
uint64_t nowMs=0;uint64_t mddTimeMs(){return nowMs;}
struct InstrumentClipView:UI{bool mddGridWasRendered=false;bool mddControls()const{return clipObject.mddEnabled||clipObject.mddRequested;}void refreshMdd(){}ActionResult timerCallback();ActionResult padAction(int32_t,int32_t,int32_t);ActionResult buttonAction(deluge::hid::Button,bool,bool);}instrumentClipView;
"""
value_begin = method("src/deluge/gui/menu_item/value.h", "void Value<T>::beginSession")
value_turn = method(
    "src/deluge/gui/menu_item/value.h", "void Value<T>::selectEncoderAction"
)
integer = method(
    "src/deluge/gui/menu_item/integer.cpp", "void Integer::selectEncoderAction"
)
enumeration = method(
    "src/deluge/gui/menu_item/enumeration.cpp", "void Enumeration::selectEncoderAction"
)
horizontal_turn = method(
    "src/deluge/gui/menu_item/horizontal_menu.cpp",
    "void HorizontalMenu::selectEncoderAction",
).replace("MenuItem* child = *current_item_;", "MenuItem* child = getCurrentItem();")
methods = (
    "namespace deluge::gui::menu_item {template<class T>\n"
    + value_begin
    + "\ntemplate<class T>\n"
    + value_turn
    + "\n"
    + integer
    + "\n"
    + enumeration
    + "\n"
    + horizontal_turn
    + "\n}\n"
)
checks = r"""
int main(){using namespace deluge::gui::menu_item::sequence::mdd;
 // All four Custom lanes share exclusive target ownership.
 auto& ui=state();ui.choose(3);ui.routes[1][0].target=0;
 assert(!targetMenu.allowed(0));assert(targetMenu.allowed(1));ui.routes[1][0].target=-1;
 ui.choose(1);ui.selectedStep=2;tieMenu.readCurrentValue();tieMenu.selectEncoderAction(1);
 assert(ui.core.ties==(1u<<2));overlapMenu.readCurrentValue();overlapMenu.selectEncoderAction(25);assert(snake::percent(ui.core.overlap)==25);
 ui.core.ties=ui.core.overlap=0;ui.choose(0);ui.selectedStep=0;
 clipObject.mddRequested=clipObject.mddEnabled=false;clipObject.mddBakedCustom=15;
 assert(writtenMenu.isRelevant(&sound,0));written1.readCurrentValue();assert(written1.getValue()==0);
 clipObject.mddBakedCustom=0;clipObject.mddRequested=clipObject.mddEnabled=true;
 rootUI=activeUI=&instrumentClipView; auto&s=clipObject.mdd;
 // Native encoder edits authoritative clip fields, not a private UI copy.
 stepsMenu.beginSession(nullptr); stepsMenu.selectEncoderAction(-9);assert(lane().length==7);
 shiftMenu.beginSession(nullptr);shiftMenu.selectEncoderAction(5);assert(lane().shift==5);
 directionMenu.beginSession(nullptr);directionMenu.selectEncoderAction(2);assert(lane().direction==snake::Direction::Pendulum);
 shapeMenu.beginSession(nullptr);shapeMenu.selectEncoderAction(4);assert(lane().shape==snake::Shape::Spiral);
 speedMenu.beginSession(nullptr);speedMenu.selectEncoderAction(2);assert(s.speedIndex(lane())==4);
 s.selectedStep=3;stepValueMenu.beginSession(nullptr);stepValueMenu.selectEncoderAction(12);assert(lane().values[3]==72);
 s.choose(1);stepValueMenu.beginSession(nullptr);stepValueMenu.selectEncoderAction(-25);assert(snake::percent(lane().values[3])==25);
 s.choose(2);stepValueMenu.beginSession(nullptr);stepValueMenu.selectEncoderAction(17);assert(lane().values[3]==117);
 customMenu.beginSession(nullptr);Buttons::held=true;customMenu.selectEncoderAction(2);assert(s.lane==5&&s.custom==2&&Buttons::selectButtonPressUsedUp);Buttons::held=false;
 song.key.rootNote=60;rootMenu.beginSession(nullptr);assert(rootMenu.getValue()==0);rootMenu.selectEncoderAction(7);assert(song.key.rootNote==67&&clipObject.inScaleMode);
 scaleMenu.beginSession(nullptr);scaleMenu.selectEncoderAction(1);assert(song.scale==MINOR);
 song.failScale=true;scaleMenu.selectEncoderAction(-1);assert(song.scale==MINOR&&scaleMenu.getValue()==MINOR&&display->popup=="Scale cannot fit notes");song.failScale=false;
 targetMenu.beginSession(nullptr);targetMenu.selectEncoderAction(3);assert(s.routes[2][0].target==2&&clipObject.routeCalls==1);
 stepValueMenu.beginSession(nullptr);stepValueMenu.selectEncoderAction(2);assert(lane().values[3]==65535&&stepValueMenu.getValue()==3);
 depthMenu.beginSession(nullptr);depthMenu.selectEncoderAction(-50);assert(s.routes[2][0].depth==16383);
 auto old=s.routes[2][0].target;clipObject.failRoute=true;targetMenu.beginSession(nullptr);targetMenu.selectEncoderAction(-1);assert(s.routes[2][0].target==old);clipObject.failRoute=false;
 // Real foreground SoundEditor pad dispatch runs BEFORE stock shortcut routing.
 open(settingsMenu);uint32_t cols=0,rows=0;assert(!soundEditor.getGreyoutColsAndRows(&cols,&rows));nowMs=10;s.choose(0);
 soundEditor.padAction(1,7,1);nowMs=60;soundEditor.padAction(1,7,0);assert(lane().muted&(1<<1));assert(soundEditor.stockShortcuts==0);
 // Long hold enters native value menu and reads that exact cell.
 soundEditor.padAction(2,7,1);nowMs=500;soundEditor.getCurrentMenuItem()->timerCallback();assert(soundEditor.current==valueMenu&&s.selectedStep==2);
 valueMenu->selectEncoderAction(5);assert(lane().values[2]==65);
 editStep(4);assert(lane().values[2]==69&&displayObject.popup=="Notes Step 3: 69");
 soundEditor.padAction(2,7,0);assert(!(lane().muted&(1<<2)));
 // A release just past 350ms must open the editor even between timer callbacks.
 nowMs=510;soundEditor.padAction(3,7,1);nowMs=861;soundEditor.padAction(3,7,0);assert(s.selectedStep==3&&soundEditor.current==valueMenu&&!(lane().muted&(1<<3)));
 // Length chord still works while native menu is foreground.
 nowMs=600;soundEditor.padAction(0,7,1);soundEditor.padAction(2,6,1);assert(lane().length==7);
 soundEditor.padAction(2,6,0);soundEditor.padAction(0,7,0);assert(!(lane().muted&1));
 // Changing lane from a map leaf cannot index routes with Notes lane -3.
 s.choose(3);open(&depthMenu);soundEditor.padAction(0,3,1);assert(s.lane==0&&soundEditor.current==valueMenu);soundEditor.padAction(0,3,0);
 // Held control pad + SELECT still routes while the native editor is foreground.
 soundEditor.padAction(3,3,1);soundEditor.buttonAction(deluge::hid::button::SELECT_ENC,true,false);assert(soundEditor.current==&customValuesMenu&&s.lane==s.custom+3);soundEditor.padAction(3,3,0);
 Buttons::shift=true;assert(soundEditor.padAction(1,7,1)==ActionResult::NOT_DEALT_WITH);assert(soundEditor.stockShortcuts==1);
 assert(instrumentClipView.buttonAction(deluge::hid::button::SELECT_ENC,true,false)==ActionResult::NOT_DEALT_WITH);Buttons::shift=false;
 // Native mode returns into settings without recursing the navigation stack.
 soundEditor.setup(&clipObject,&sequencerModeMenu);assert(sequencerModeMenu.selectButtonPress()==settingsMenu);
 soundEditor.navigationDepth=1;soundEditor.menuItemNavigationRecord[0]=settingsMenu;assert(sequencerModeMenu.selectButtonPress()==nullptr);
 open(settingsMenu);rootSettings.chooseColumn(0);rootSettings.selectEncoderAction(1);assert(lane().direction==snake::Direction::Reverse);
 rootSettings.chooseColumn(2);int previous=lane().length;rootSettings.selectEncoderAction(-1);assert(lane().length==previous-1);
 soundEditor.padAction(4,3,1);assert(soundEditor.current==&randomPage);soundEditor.padAction(4,3,0);
 assert(!randomMenu.shouldEnterSubmenu());
 for(auto* action:{&randomMenu,&randomAllMenu,&resetMenu,&resetAllMenu}){assert(action->getOccupiedSlots()==2);action->renderInHorizontalMenu({0,14,63,18});assert(deluge::hid::display::OLED::main.text==action->getName());}
 s.gestures.down(0,1000);s.down(snake::Control::Root);
 soundEditor.buttonAction(deluge::hid::button::SHIFT,true,false);assert(s.held==snake::Control::None&&s.gestures.held==-1);
 clipObject.mddRequested=clipObject.mddEnabled=false;assert(soundEditor.getGreyoutColsAndRows(&cols,&rows));
 clipObject.mddRequested=clipObject.mddEnabled=true;
 assert(instrumentClipView.buttonAction(deluge::hid::button::SELECT_ENC,true,true)==ActionResult::NOT_DEALT_WITH);
 assert(instrumentClipView.buttonAction(deluge::hid::button::SYNTH,true,false)==ActionResult::NOT_DEALT_WITH);
 for(int i=0;i<4;++i){soundEditor.padAction(i,3,1);soundEditor.padAction(i,3,0);int selected=s.lane;int before=lane().length;auto*window=static_cast<Menu*>(soundEditor.current);window->focusChild(&stepsMenu);window->selectEncoderAction(before==16?-1:1);assert(s.lane==selected&&lane().length==(before==16?15:before+1));}
 soundEditor.padAction(5,3,1);assert(soundEditor.current==&resetPage);soundEditor.padAction(5,3,0);
 soundEditor.padAction(6,3,1);assert(soundEditor.current==&keyMenu);soundEditor.padAction(6,3,0);
 soundEditor.padAction(7,3,1);assert(soundEditor.current==&mapMenu);soundEditor.padAction(7,3,0);
 soundEditor.padAction(8,3,1);assert(soundEditor.current==settingsMenu&&rootSettings.getCurrentItem()==&directionMenu&&std::string(s.title())=="MDD Snake");soundEditor.padAction(8,3,0);
 soundEditor.padAction(9,3,1);assert(soundEditor.current==&writeConfirmation);soundEditor.padAction(9,3,0);
 for(int i=10;i<16;++i){soundEditor.padAction(i,3,1);soundEditor.padAction(i,3,0);assert(soundEditor.current==&writeConfirmation&&s.held==snake::Control::None);}
 control(snake::Control::Write);assert(soundEditor.current==&writeConfirmation&&writeConfirmation.getValue()==0);
 assert(writeConfirmation.selectButtonPress()==nullptr&&clipObject.writes==0&&clipObject.mddEnabled);
 writeConfirmation.selectEncoderAction(1);clipObject.failWrite=true;assert(writeConfirmation.selectButtonPress()==NO_NAVIGATION&&clipObject.mddEnabled);
 clipObject.failWrite=false;assert(writeConfirmation.selectButtonPress()==NO_NAVIGATION&&!clipObject.mddEnabled);
 std::cout<<"PASS native MDD: encoder writes, routes/rollback, root/scale, foreground pad forwarding, mute/hold/length, lane safety, held controls, stock Shift and mode navigation\n";
}
"""
code = (
    stubs
    + methods
    + mode
    + "\ndeluge::gui::menu_item::sequence::Mode sequencerModeMenu;\n"
    + native
    + "\n"
    + pad
    + "\n"
    + button
    + "\n"
    + timer
    + "\n"
    + editor_pad
    + "\n"
    + editor_button
    + "\n"
    + greyout
    + "\n"
    + checks
)
with tempfile.TemporaryDirectory() as d:
    p = Path(d) / "probe.cpp"
    p.write_text(code)
    exe = Path(d) / "probe"
    subprocess.run(
        [
            "g++",
            "-std=c++23",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-parameter",
            "-Wno-misleading-indentation",
            "-fsanitize=undefined",
            "-D_GLIBCXX_DEBUG",
            "-I" + str(r / "src/deluge"),
            str(p),
            "-o",
            str(exe),
        ],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
