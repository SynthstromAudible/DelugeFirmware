"""Exercise the production immediate-load method with a delayed SD completion."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class ClusterReadWaitTest(unittest.TestCase):
    def test_pending_read_services_audio(self):
        source = (ROOT / "src/deluge/storage/audio/audio_file_manager.cpp").read_text()
        method = (
            "bool AudioFileManager::loadEnqueuedClusterNow("
            + source.split("bool AudioFileManager::loadEnqueuedClusterNow(", 1)[
                1
            ].split("\nvoid AudioFileManager::removeReasonFromCluster", 1)[0]
        )
        harness = r"""
#include <cassert>
#include <cstdint>
#include <optional>
using Priority = uint32_t;
struct Cluster { bool loaded = false; };
struct CriticalSectionGuard {};
bool currentlyAccessingCard = false;
bool allowSomeUserActionsEvenWhenInCardRoutine = false;
namespace AudioEngine { bool audioRoutineLocked = false; }
struct Queue {
    std::optional<Priority> take(Cluster*, const CriticalSectionGuard&) { return {}; }
    void enqueueCluster(Cluster&, Priority, const CriticalSectionGuard&) { assert(false); }
};
struct AudioFileManager {
    Cluster* clusterBeingLoaded = nullptr;
    Cluster* clusterBeingRead_ = nullptr;
    Cluster* completed = nullptr;
    Queue loadingQueue;
    bool isClusterBeingRead(Cluster* c, const CriticalSectionGuard&) {
        return c == clusterBeingRead_ || c == completed;
    }
    void finishClusterReads() { assert(completed); completed->loaded = true; }
    bool loadCluster(Cluster&, int) { assert(false); return false; }
    bool loadEnqueuedClusterNow(Cluster&);
} manager;
int polls = 0;
int audioCalls = 0;
void sd_async_check_timeout() {
    if (++polls == 4) {
        manager.completed = manager.clusterBeingRead_;
        manager.clusterBeingRead_ = nullptr;
    }
}
void routineForSD() { ++audioCalls; }
"""
        checks = r"""
int main() {
    Cluster cluster;
    manager.clusterBeingRead_ = &cluster;
    assert(manager.loadEnqueuedClusterNow(cluster));
    assert(cluster.loaded);
    assert(audioCalls >= 3); // Audio must be serviced throughout the wait.
    assert(polls == 4);
    int previousAudioCalls = audioCalls;
    assert(manager.loadEnqueuedClusterNow(cluster)); // Already completed: no wait.
    assert(audioCalls == previousAudioCalls);
    AudioEngine::audioRoutineLocked = true;
    assert(!manager.loadEnqueuedClusterNow(cluster)); // No recursive audio load.
}
"""
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "test.cpp"
            exe = Path(directory) / "test"
            cpp.write_text(harness + method + checks)
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
