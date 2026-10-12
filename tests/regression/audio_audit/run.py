import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[3]
src = (root / "src/deluge/processing/engines/audio_engine.cpp").read_text()
start = src.index("void killOneVoice(")
end = src.index("\n/// Force a voice to release", start)
scaffold = r"""
#include <algorithm>
#include <cassert>
#include <memory>
#include <ranges>
#include <vector>
struct Sound;
struct Voice { Sound& sound; int priority; bool keep; int calls=0;
 int getPriorityRating() const { return priority; }
 bool doImmediateRelease() { ++calls; return keep; }
};
struct Sound {
 using ActiveVoice=std::shared_ptr<Voice>;
 std::vector<ActiveVoice> voices;
 int freed=0;
 bool hasActiveVoices() const { return !voices.empty(); }
 ActiveVoice getLowestPriorityVoice() const { return *std::ranges::max_element(voices, {}, &Voice::getPriorityRating); }
 void freeActiveVoice(const ActiveVoice&) { ++freed; }
};
struct Song { int culled=0; void cullAudioClipVoice() { ++culled; } };
std::vector<Sound*> sounds;
Song* currentSong=nullptr;
void logAction(const char*) {}
#define D_PRINTLN(...) ((void)0)
"""
main = r"""
int main() {
 killOneVoice(128); // no song, no voices
 Song song; currentSong=&song;
 killOneVoice(128); assert(song.culled==1);
 Sound empty; sounds={&empty};
 killOneVoice(128); assert(song.culled==2);
 Sound a,b;
 auto va=std::make_shared<Voice>(Voice{a,1,true});
 auto vb=std::make_shared<Voice>(Voice{b,9,false});
 a.voices={va}; b.voices={vb}; sounds={&empty,&a,&b};
 killOneVoice(128);
 assert(vb->calls==1 && b.freed==1 && va->calls==0 && song.culled==2);
 sounds={&a}; killOneVoice(128); assert(va->calls==1 && a.freed==0);
}
"""
with tempfile.TemporaryDirectory() as d:
    p = Path(d) / "cull.cpp"
    p.write_text(scaffold + src[start:end] + main)
    subprocess.run(
        [
            "g++",
            "-std=c++23",
            "-D_GLIBCXX_DEBUG",
            "-fsanitize=undefined",
            "-fno-sanitize-recover=all",
            str(p),
            "-o",
            d + "/cull",
        ],
        check=True,
    )
    subprocess.run([d + "/cull"], check=True)
print(
    "PASS actual killOneVoice body: empty ranges, audio fallback, priority and release paths"
)
