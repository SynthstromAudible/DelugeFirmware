#!/usr/bin/env python3
"""Tests for deluge_jlink.py: `python3 -m unittest test_deluge_jlink` (from tools/)."""

import unittest

import deluge_jlink

# CHCTRL as the RZ/A1L TRM gives it: channels 0-7 from 0xE8200000 every 0x40, channels 8-15 from
# 0xE8200400, each channel's CHCTRL at +0x28. CLREN (bit 1) | SWRST (bit 3).
CHCTRL = [0xE8200028 + ch * 0x40 for ch in range(8)] + [
    0xE8200428 + ch * 0x40 for ch in range(8)
]


class LoadCommandsTests(unittest.TestCase):
    def test_every_dma_channel_is_stopped_after_the_mmu_is_off_and_before_the_load(
        self,
    ):
        # A firmware still running when the core is halted keeps its DMA going; a ring would
        # write into the image while gdb loads it.
        cmds = deluge_jlink.load_commands("0x20020000", [])
        mmu_off = cmds.index("monitor cp15 1, 0, 0, 0 = 0x00C50078")
        load = cmds.index("load")
        for addr in CHCTRL:
            stop = cmds.index(f"monitor memU32 {addr:#010x} = 0x0000000a")
            self.assertTrue(mmu_off < stop < load, f"{addr:#x} stopped at {stop}")

    def test_the_entry_and_the_extra_commands_follow_the_load(self):
        cmds = deluge_jlink.load_commands(
            "0x20020000", ["restore x.bin binary 0x60000000"]
        )
        load = cmds.index("load")
        self.assertLess(load, cmds.index("set $pc = 0x20020000"))
        self.assertEqual(cmds[-1], "restore x.bin binary 0x60000000")
        self.assertNotIn("set $pc = None", deluge_jlink.load_commands(None, []))


if __name__ == "__main__":
    unittest.main()
