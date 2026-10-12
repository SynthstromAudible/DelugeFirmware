import re
import subprocess
import tempfile
from pathlib import Path

r = Path(__file__).resolve().parents[3]


def extract(s, key):
    a = s.index(key)
    b = s.index("{", a) + 1
    level = 1
    while level:
        level += (s[b] == "{") - (s[b] == "}")
        b += 1
    return s[a:b]


enum = extract((r / "src/definitions_cxx.hpp").read_text(), "enum class OscType") + ";"
s = (r / "src/deluge/gui/menu_item/osc/type.h").read_text()
methods = "\n".join(
    extract(s, x).replace(" override", "")
    for x in [
        "deluge::vector<OscType> availableTypes()",
        "deluge::vector<std::string_view> getOptions(",
    ]
)
labels = sorted(set(re.findall(r"STRING_FOR_\w+", methods)))
f = (r / "src/deluge/util/functions.cpp").read_text()
code = (
    """#include <cstdint>
#include <vector>
#include <string_view>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <iostream>
namespace deluge {template<class T>using vector=std::vector<T>;}
enum class SynthMode{SUBTRACTIVE,RINGMOD};enum class OptType{FULL};
struct Sound{SynthMode mode;SynthMode getSynthMode(){return mode;}};
struct Editor{Sound* currentSound;}soundEditor;
struct Display{bool oled=true;bool haveOLED(){return oled;}}screen,*display=&screen;
namespace AudioEngine{bool micPluggedIn=false,lineInPluggedIn=false;}
"""
    + enum
    + "\nnamespace l10n {enum class String{"
    + ",".join(labels)
    + '};std::string_view getView(String){return "label";}}\n'
)
code += "struct Type{bool dx;bool mayUseDx()const{return dx;}" + methods + "};\n"
code += (
    extract(f, "char const* oscTypeToString(")
    + "\n"
    + extract(f, "OscType stringToOscType(")
)
code += """
int main(){Sound sound;soundEditor.currentSound=&sound;
for(bool dx:{false,true})for(bool plugged:{false,true})for(bool ring:{false,true})for(bool oled:{false,true}){
 Type t{dx};AudioEngine::lineInPluggedIn=plugged;screen.oled=oled;sound.mode=ring?SynthMode::RINGMOD:SynthMode::SUBTRACTIVE;
 auto types=t.availableTypes();auto labels=t.getOptions(OptType::FULL);
 assert(types.size()==labels.size());assert(types.size()==(ring?7:10+dx+2*plugged));
 for(size_t i=0;i<types.size();i++){
 assert(stringToOscType(oscTypeToString(types[i]))==types[i]);
 for(size_t j=i+1;j<types.size();j++)assert(types[i]!=types[j]);
 }
}
std::cout<<"PASS menu: 16 configurations; unique options and saved oscillator type round trips\\n";
}"""
with tempfile.TemporaryDirectory() as d:
    p = Path(d)
    (p / "probe.cpp").write_text(code)
    subprocess.run(
        [
            "g++",
            "-std=c++23",
            "-fsanitize=undefined",
            "-D_GLIBCXX_DEBUG",
            str(p / "probe.cpp"),
            "-o",
            str(p / "probe"),
        ],
        check=True,
    )
    subprocess.run([str(p / "probe")], check=True)
