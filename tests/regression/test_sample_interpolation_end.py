"""Run the production interpolation-fill method with a controlled end-of-sample.

The firmware translation unit depends on ARM hardware. Extract just this method
and stub cluster transitions, while retaining its actual control flow and reads.
Run with: python3 -m unittest discover -s tests/regression -v
"""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class InterpolationEndTest(unittest.TestCase):
    def test_sample_ending_during_fill(self):
        source = (
            ROOT / "src/deluge/model/sample/sample_low_level_reader.cpp"
        ).read_text()
        method = source.split(
            "bool SampleLowLevelReader::fillInterpolationBufferForward(", 1
        )[1]
        method = (
            "bool SampleLowLevelReader::fillInterpolationBufferForward("
            + method.split("\nvoid SampleLowLevelReader::jumpBackSamples", 1)[0]
        )
        # The firmware stores the tail counter in a 32-bit pointer. Keep the cast
        # pointer-sized on a 64-bit host; no branch or memory access is replaced.
        method = method.replace("(uint32_t)currentPlayPos", "(uintptr_t)currentPlayPos")
        harness = r"""
#include <cassert>
#include <cstdint>
struct SamplePlaybackGuide { int playDirection = 1; };
struct Sample { int numChannels = 2; int byteDepth = 2; };
struct History {
    int16_t left[16]{}, right[16]{};
    void setL(int i, int16_t v) { left[i] = v; }
    void setR(int i, int16_t v) { right[i] = v; }
};
struct SampleLowLevelReader {
    int token = 0;
    int* clusters[1] = {&token};
    int16_t samples[4] = {0, 123, 456, 0};
    char* currentPlayPos = reinterpret_cast<char*>(samples);
    History interpolator_;
    int changes = 0;
    bool changeClusterIfNecessary(SamplePlaybackGuide*, Sample*, bool, int) {
        assert(clusters[0] != nullptr); // Must not revisit a released cluster.
        if (++changes == 1) return true;
        clusters[0] = nullptr;
        currentPlayPos = nullptr;
        return false;
    }
    bool fillInterpolationBufferForward(SamplePlaybackGuide*, Sample*, int32_t, bool, int32_t, int32_t);
};
"""
        checks = r"""
int main() {
    SamplePlaybackGuide guide;
    Sample sample;
    for (int channels : {1, 2}) {
        sample.numChannels = channels;
        SampleLowLevelReader reader;
        assert(reader.fillInterpolationBufferForward(&guide, &sample, 16, false, 4, 0));
        assert(reader.interpolator_.left[3] == 123);
        if (channels == 2) assert(reader.interpolator_.right[3] == 456);
        for (int i = 0; i < 3; ++i) {
            assert(reader.interpolator_.left[i] == 0);
            assert(reader.interpolator_.right[i] == 0);
        }
        assert(reader.changes == 2);
        assert(reinterpret_cast<uintptr_t>(reader.currentPlayPos) == 3);
        assert(reader.fillInterpolationBufferForward(&guide, &sample, 16, false, 4, 0));
        assert(reinterpret_cast<uintptr_t>(reader.currentPlayPos) == 7);
        assert(!reader.fillInterpolationBufferForward(&guide, &sample, 16, false, 16, 0));
        assert(reader.changes == 2);
    }
}
"""
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "test.cpp"
            exe = Path(directory) / "test"
            cpp.write_text("#include <initializer_list>\n" + harness + method + checks)
            subprocess.run(
                [os.environ.get("CXX", "c++"), "-std=c++17", str(cpp), "-o", str(exe)],
                check=True,
            )
            result = subprocess.run(
                [str(exe)], capture_output=True, text=True, check=False
            )
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
