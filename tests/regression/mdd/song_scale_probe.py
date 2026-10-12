"""Exercise the production Song scale-note scan across stock and MDD clips."""

import subprocess
import tempfile
from pathlib import Path

r = Path(__file__).resolve().parents[3]
s = (r / "src/deluge/model/song/song.cpp").read_text()
a = s.index("NoteSet Song::notesInScaleModeClips()")
i = s.index("{", a) + 1
d = 1
while d:
    d += (s[i] == "{") - (s[i] == "}")
    i += 1
src = (
    """#include <cassert>
#include <vector>
#include <iostream>
struct NoteSet{unsigned bits=0;};
struct InstrumentClip{bool scale,mddEnabled;unsigned stockNotes;bool mddRequested=false;bool isScaleModeClip(){return scale;}void seeWhatNotesWithinOctaveArePresent(NoteSet&n,int){n.bits|=stockNotes;}};
struct Song{int key=0;std::vector<InstrumentClip*> clips;NoteSet notesInScaleModeClips();};
struct InstrumentClips{static auto&everywhere(Song*s){return s->clips;}};
"""
    + s[a:i]
    + """
int main(){InstrumentClip stock{true,false,0xab5},mdd{true,true,0xfff},chromatic{false,false,0xfff};Song song; song.clips={&stock,&mdd,&chromatic};assert(song.notesInScaleModeClips().bits==0xab5);mdd.mddEnabled=false;assert(song.notesInScaleModeClips().bits==0xfff);mdd.mddRequested=true;assert(song.notesInScaleModeClips().bits==0xab5);mdd.mddRequested=false;stock.scale=false;mdd.mddEnabled=true;assert(song.notesInScaleModeClips().bits==0);std::cout<<"PASS production Song scale fitting: active MDD dormant rows excluded, Normal and other project clips retained\\n";}
"""
)
with tempfile.TemporaryDirectory() as td:
    p = Path(td) / "probe.cpp"
    p.write_text(src)
    exe = Path(td) / "probe"
    subprocess.run(
        ["g++", "-std=c++23", "-fsanitize=undefined", str(p), "-o", str(exe)],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
