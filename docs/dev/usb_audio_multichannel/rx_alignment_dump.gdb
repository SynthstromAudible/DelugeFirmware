set pagination off
target remote :1234
dump binary memory /tmp/q_read.bin 0x20031f58 0x20031f58+4
dump binary memory /tmp/q_written.bin 0x20031f54 0x20031f54+4
dump binary memory /tmp/q_rxoff.bin 0x2005ba7c 0x2005ba7c+4
dump binary memory /tmp/q_pos.bin 0x2005ba64 0x2005ba64+4
dump binary memory /tmp/q_streaming.bin 0x20030004 0x20030004+1
dump binary memory /tmp/q_staging.bin 0x20030a00+0x40000000 0x20030a00+0x40000000+540
dump binary memory /tmp/q_rx.bin 0x0c009ba0+0x40000000 0x0c009ba0+0x40000000+16384
detach
quit
