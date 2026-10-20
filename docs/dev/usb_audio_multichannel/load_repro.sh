#!/usr/bin/env bash
# High-load USB audio repro in delugemu: a heavy song playing, both USB streams running.
# Env: DELUGEMU (delugemu checkout, default ~/code/delugemu), SD (card image), SEQ (dz_play.py key sequence),
# QEMU_EXTRA (extra QEMU args, e.g. -s for the gdb stub). See NOTES.md.
# Usage: load_repro.sh <elf> <outdir> <from-host source> [icount shift]
set -u
ELF="$1"; OUT="$2"; SRC="$3"; IC="${4:-2}"
mkdir -p "$OUT"; rm -f "$OUT"/*
SOCK="$OUT/qmp.sock"
RH="0x$(arm-none-eabi-nm "$ELF" | awk '/i2sTXBufferPos/ {print $1; exit}')"
cd "${DELUGEMU:-$HOME/code/delugemu}"
DELUGEMU_SSIF_DUMP="$OUT/out.raw" DELUGEMU_SSIF_STATS="$OUT/stats.txt" ./scripts/run.sh "$ELF" \
  --display none --audio none --icount "$IC" --tx-render-head "$RH" --sd "${SD:-build/deluge_sd.img}" \
  --usb-audio "$OUT/th.wav" --usb-audio-in "$SRC" --usb-audio-in-capture "$OUT/fh.wav" \
  --line-in tone:1000 -- -qmp "unix:$SOCK,server,nowait" ${QEMU_EXTRA:-} </dev/null >"$OUT/run.log" 2>&1 &
QP=$!
python3 scripts/dz_play.py "$SOCK" "${SEQ:-6.0:_wait 0:1:hold 0:2:hold 0:3:hold 0:4:hold 0:5:hold 0:6:hold 0:7:hold 0:8:hold 25.0:_quit}" >>"$OUT/run.log" 2>&1 || kill $QP
wait $QP 2>/dev/null
ls -la "$OUT"; grep -h "ssif-stats" "$OUT/stats.txt" | tail -25
