"""The OpenCSD snapshot a capture is decoded from, and the J-Trace byte order it undoes."""

import unittest

import decode

PTM = {"ETMCR": 0x1000, "ETMIDR": 0x411CF301, "ETMCCER": 0x8EA, "ETMTRACEIDR": 0x10}
PORT_FSYNC = b"\xff\xff\xff\x7f"


class SnapshotFiles(unittest.TestCase):
    def setUp(self):
        self.files = decode.snapshot_files(
            PTM,
            [(0x2008F9C0, "mem_0.bin"), (0x20300000, "mem_1.bin")],
            "frames.bin",
            0x2008F9C0,
        )

    def test_names_every_file(self):
        self.assertEqual(
            sorted(self.files), ["PTM_0.ini", "cpu_0.ini", "snapshot.ini", "trace.ini"]
        )

    def test_snapshot_lists_the_core_and_the_source(self):
        text = self.files["snapshot.ini"]
        self.assertIn("[snapshot]\nversion=1.0\n", text)
        self.assertIn("device0=cpu_0.ini\ndevice1=PTM_0.ini\n", text)
        self.assertIn("[trace]\nmetadata=trace.ini\n", text)

    def test_core_is_a_cortex_a9_with_the_code_as_memory(self):
        text = self.files["cpu_0.ini"]
        self.assertIn("name=cpu_0\nclass=core\ntype=Cortex-A9\n", text)
        self.assertIn("R15=0x2008F9C0\n", text)
        self.assertIn("[dump1]\nspace=S\naddress=0x2008F9C0\nfile=mem_0.bin\n", text)
        self.assertIn("[dump2]\nspace=S\naddress=0x20300000\nfile=mem_1.bin\n", text)

    def test_source_is_a_pft_1_0_ptm_with_the_registers_used(self):
        self.assertEqual(
            self.files["PTM_0.ini"],
            "[device]\nname=PTM_0\nclass=trace_source\ntype=PFT1.0\n\n[regs]\n"
            "ETMCR(0x000)=0x00001000\n"
            "ETMIDR(0x079)=0x411CF301\n"
            "ETMCCER(0x07A)=0x000008EA\n"
            "ETMTRACEIDR(0x080)=0x00000010\n",
        )

    def test_trace_buffer_is_coresight_frames_feeding_the_ptm(self):
        text = self.files["trace.ini"]
        self.assertIn(
            "[buffer0]\nname=TPIU_0\nfile=frames.bin\nformat=coresight\n", text
        )
        self.assertIn("[source_buffers]\nPTM_0=TPIU_0\n", text)
        self.assertIn("[core_trace_sources]\ncpu_0=PTM_0\n", text)

    def test_needs_every_register(self):
        with self.assertRaises(KeyError):
            decode.snapshot_files({"ETMCR": 0}, [], "frames.bin", 0)


class Normalize(unittest.TestCase):
    def frame(self, seed: int) -> bytes:
        return bytes((seed + i) & 0xFF for i in range(16))

    def test_reverses_the_capture_and_drops_frame_syncs(self):
        f1, f2, f3 = self.frame(0x10), self.frame(0x40), self.frame(0x80)
        port = b"\x11\x22\x33" + PORT_FSYNC + f1 + f2 + PORT_FSYNC + f3 + b"\x44" * 5
        frames, stats = decode.normalize(port[::-1])
        self.assertEqual(frames, f1 + f2 + f3)
        self.assertEqual(
            stats, {"fsyncs": 2, "frames": 3, "lead_bytes": 3, "ragged_bytes": 5}
        )

    def test_capture_without_a_frame_sync_has_no_frames(self):
        frames, stats = decode.normalize(bytes(64))
        self.assertEqual(frames, b"")
        self.assertEqual(stats["lead_bytes"], 64)
        self.assertEqual(stats["frames"], 0)

    def test_capture_as_read_has_its_syncs_reversed(self):
        """The bytes RAWTRACE_Read returned on the rig began 7F FF FF FF: the reversed sync."""
        raw = b"\x7f\xff\xff\xff" + self.frame(0)[::-1]
        frames, stats = decode.normalize(raw)
        self.assertEqual(stats["fsyncs"], 1)
        self.assertEqual(stats["lead_bytes"], 16)
        self.assertEqual(frames, b"")


if __name__ == "__main__":
    unittest.main()
