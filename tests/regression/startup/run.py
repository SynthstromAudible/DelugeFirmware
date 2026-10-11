from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[3]
s=(root/'src/deluge/deluge.cpp').read_text();a=s.index('void setupStartupSong()');b=s.index('\nvoid setupOLED()',a);function=s[a:b]
scaffold=r'''
#include <string>
#include <cstring>
#include <cassert>
#include <iostream>
using namespace std;
enum class StartupSongMode {BLANK,TEMPLATE,LASTOPENED,LASTSAVED};
enum class PopupType {GENERAL}; enum class TimerName {DISPLAY};
int now=0,expiry=0,greetings=0,loaded=0;bool popup=false,loadError=false,openOk=true,canary=false,writeOk=true;
namespace FlashStorage {StartupSongMode defaultStartupSongMode;}
struct Settings {const char* getStartupSong(){return "SONGS/TEST.XML";}} runtimeFeatureSettings;
struct String {string value;void concatenate(const char* p){value+=p;}const char* get(){return value.c_str();}};
void replace_char(char* out,const char* in,char from,char to){strcpy(out,in);for(;*out;out++)if(*out==from)*out=to;}
namespace StorageManager {bool fileExists(const char* p){return strstr(p,"__STARTUP_OFF_CHECK_")?canary:true;}}
struct FIL{};constexpr int FA_CREATE_ALWAYS=1,FA_WRITE=2,FR_OK=0;
int f_open(FIL*,const char*,int){return writeOk?FR_OK:1;}void f_close(FIL*){}void f_unlink(const char*){}
struct Song {string name;void writeTemplateSong(const char*){}void setSongFullPath(const char*){}} song,*currentSong=&song;
struct Load {void performLoad(){popup=true;now+=7000;loaded++;popup=loadError;}} loadSongUI;
bool openUI(Load*){return openOk;}
struct Display {bool oled=true;bool haveOLED(){return oled;}void consoleText(const char*){}} device,*display=&device;
namespace deluge::hid::display {struct OLED {static bool isPopupPresent(){return popup;}static void popupText(const char* text,bool,PopupType){assert(string(text)=="Holdontoentropy");popup=true;greetings++;}};}
struct Timer {void setTimer(TimerName,int ms){expiry=now+ms;}} uiTimerManager;
'''
tests=r'''
void reset(){now=expiry=greetings=loaded=0;popup=loadError=canary=false;openOk=writeOk=device.oled=true;}
int main(){
 for(auto mode:{StartupSongMode::BLANK,StartupSongMode::TEMPLATE,StartupSongMode::LASTOPENED,StartupSongMode::LASTSAVED}){
  reset();FlashStorage::defaultStartupSongMode=mode;setupStartupSong();
  assert(greetings==1 && popup && expiry-now==4000);
  assert(loaded==(mode==StartupSongMode::BLANK?0:1));
 }
 reset();FlashStorage::defaultStartupSongMode=StartupSongMode::LASTOPENED;loadError=true;setupStartupSong();assert(greetings==0 && popup);
 reset();openOk=false;setupStartupSong();assert(greetings==0);
 reset();canary=true;setupStartupSong();assert(greetings==0);
 reset();writeOk=false;setupStartupSong();assert(greetings==0);
 reset();device.oled=false;setupStartupSong();assert(greetings==0);
 std::cout<<"PASS startup: four-second greeting after slow load / blank startup; load errors, canary, open/write failure and 7-segment guards\n";
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'probe.cpp').write_text(scaffold+function+tests)
 subprocess.run(['g++','-std=c++23','-fsanitize=undefined',str(p/'probe.cpp'),'-o',str(p/'probe')],check=True)
 subprocess.run([str(p/'probe')],check=True)
