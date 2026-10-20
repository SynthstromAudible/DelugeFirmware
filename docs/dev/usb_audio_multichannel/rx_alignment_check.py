"""Check that USB to-host channels 1/2 are a sample-exact copy of the SSI RX buffer, at the frames the firmware maps
them to.

Reads the dumps rx_alignment_dump.gdb writes to /tmp/q_*.bin while delugemu is paused. Usage:
    python rx_alignment_check.py [ssiRxBuffer address]
The address is build specific - get it with `arm-none-eabi-nm deluge.elf | grep ssiRxBuffer`.
"""

import struct
import sys
from pathlib import Path


def u32(path):
    return struct.unpack("<I", Path(path).read_bytes())[0]


def s24(b):
    v = b[0] | b[1] << 8 | b[2] << 16
    return v - (1 << 24) if v & 0x800000 else v


rx_buffer = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x0C009B80
RXBASE = rx_buffer + 0x40000000  # The uncached mirror the firmware addresses it through

read, written, rxoff, pos = (
    u32("/tmp/q_read.bin"),
    u32("/tmp/q_written.bin"),
    u32("/tmp/q_rxoff.bin"),
    u32("/tmp/q_pos.bin"),
)
streaming = Path("/tmp/q_streaming.bin").read_bytes()[0]
rx = struct.unpack("<4096i", Path("/tmp/q_rx.bin").read_bytes())
st = Path("/tmp/q_staging.bin").read_bytes()
print(
    f"streaming={streaming} read={read:#x} written={written:#x} rxOffset={rxoff:#x} "
    f"i2sRXpos frame={(pos - RXBASE) // 8:#x}"
)

# The last packet, as 45 frames of 4 channels. It may have been 44, in which case the match below is a frame later
pk = [
    [s24(st[f * 12 + c * 3 : f * 12 + c * 3 + 3]) for c in range(4)] for f in range(45)
]
first = (read - 45 + rxoff) & 2047
print("expected RX start frame", hex(first))
rxL = [rx[2 * i] >> 8 for i in range(2048)]
rxR = [rx[2 * i + 1] >> 8 for i in range(2048)]
exp = [(rxL[(first + i) & 2047], rxR[(first + i) & 2047]) for i in range(45)]
match = sum(1 for i in range(45) if (pk[i][0], pk[i][1]) == exp[i])
print("frames matching computed RX position:", match, "/45")

# Search for where the packet's ch1/ch2 actually came from
best = [
    (
        sum(
            1
            for i in range(45)
            if (pk[i][0], pk[i][1]) == (rxL[(s + i) & 2047], rxR[(s + i) & 2047])
        ),
        s,
    )
    for s in range(2048)
]
best.sort(reverse=True)
print(
    "best match RX start",
    [(hex(s), n) for n, s in best[:3]],
    "offset from expected",
    (best[0][1] - first + 1024) % 2048 - 1024,
)
print("pkt ch1 first 6:", [p[0] for p in pk[:6]])
print("rx at expected:", [e[0] for e in exp[:6]])
