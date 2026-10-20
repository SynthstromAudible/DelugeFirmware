# USB audio: 4x4 multichannel - working notes

Status: **parked**. Branch `feat/multitrack`, on top of the stereo USB audio in #5055. Works in delugemu (QEMU) in all
modes, including under heavy load since the double-buffer fix below; not yet confirmed on hardware after that fix.
Matching emulator work is in delugemu, branch `experiment/usb_audio` (commit 4aaeaf7 and on).

## What it does

UAC1, full speed, 44.1kHz, one 4-channel stream each way (replaces stereo - there's no stereo mode left):

| Channels | From the computer                                     | To the computer                         |
| -------- | ----------------------------------------------------- | --------------------------------------- |
| 1/2      | Mixed straight into the Deluge's outputs              | Line/mic input, raw from the codec      |
| 3/4      | `AudioInputChannel::USB` - audio tracks, and the sampler with `SETTINGS > RECORDING > SAMPLE FROM USB 3/4` | Main mix (as `toUSB` was in stereo) |

Described as quad (`wChannelConfig` 0x0033, L/R/Ls/Rs) so Windows treats it as an ordinary device. bcdDevice 0x0303.

## Where things are

- `drivers/usb/userdef/r_usb_paudio_config.h` - format and packet sizes, with the bandwidth reasoning
- `drivers/usb/usb_audio_pcm.h` - NEON (argon) pack/unpack, 2 stereo q31 buffers <-> 4ch packed 24-bit.
  Spec: `tests/spec/usb_audio_pcm_spec.cpp`
- `drivers/usb/usb_audio.cpp` - rings, rate steering, DMA/ISR. Channels 1/2 to the host are read straight from the SSI
  RX buffer in the SOF interrupt (`toHostRxOffset` maps ring frame counts to RX frames), so the audio routine does no
  extra work for them. Channels 3/4 from the host only get copied into their ring while something might read them
  (`AudioEngine::usbInputInUse`, set from `Song::renderAudio()`, or the sampler setting)
- `RZA1/.../r_usb_peptable.c` - pipe buffer layout (blocks 8 / 26 / MIDI in at 44)
- `io/midi/r_usb_pmidi_descriptor.c` - descriptors
- User docs: `website/src/content/docs/features/usb_audio.mdx`

## Things learned the hard way

**Full-speed isochronous bandwidth includes bit stuffing.** The host reserves each endpoint at its max packet size
using USB 2.0 5.11.3: 7268ns + 83.54ns x floor(3.167 + 7 x 8 x bytes / 6), against a 900us periodic budget. 4-byte
subframes don't fit at all. Packed 24-bit at 48 frames (576 bytes) was 456.6us each - 913us for both - and macOS
selected the host->Deluge alternate setting ~1850 times, never got a packet, then stopped trying, with no log message.
45 frames (540 bytes, 428.6us each, 857us) is both the most that fits and the fewest 44.1kHz allows, so
`framesForNextPacket()` caps at 45 and only speeds up on 44-frame packets.

**Interrupts are off for >1ms while a heavy output renders.** `Song::renderAudio()` wraps each `renderOutput()` in
`ENTER_CRITICAL_SECTION()` (a full `CPSID i`). With a 127-voice synth (`001 Test.XML` from the problem songs backup)
that measured up to 1.47ms in QEMU at `--icount 2`, so ~5% of USB SOF interrupts were missed. Each one cost a to-host
packet; the backlog ran away (to 717 frames) and skipped ahead, and with implicit feedback the host sent ~5% less too,
so host->Deluge underran ~27x/s and re-primed with silence - "desyncs and garbles at high load". The stereo version
had the same exposure, with slightly more headroom (could send 46).
Fix: keep both buffers of the double-buffered to-host pipe full (`queueToHostPacket()`, called from the SOF and from
the to-host DMA-complete interrupt). A late SOF then still has a packet waiting. Covers interrupts off for up to ~2ms,
at +1ms latency to the host. The host->Deluge pipe is double buffered in hardware already, so it rides out the same.
**Not fixed:** anything holding interrupts off for over ~2ms will still lose audio (and delays MIDI etc). The real fix
is a shorter critical section in `Song::renderAudio()` - core code from the original firmware, not looked into yet.

**Debugger reads on hardware can be stale.** Reading cached SRAM variables over JTAG gave counters that didn't match
what the CPU had (write-back L1). Read through the uncached mirror (+0x40000000) or use QEMU, which has no cache.
And make sure the debugger's ELF matches what's flashed.

## Testing in delugemu

Needs delugemu `experiment/usb_audio` or later. The model now has two buffers per pipe like the hardware, and
`--line-in tone:<hz>` feeds a sine into the codec input.

- Routing/format regression: `DELUGE_FIRMWARE=build/Release/deluge.elf ./tests/usb-peripheral.sh` (in delugemu) -
  checks each route with distinct tones (host 1/2 440Hz, host 3/4 660Hz, line in 1000Hz) and that nothing echoes.
- Load: `load_repro.sh` here - FAM1 isn't on delugemu's card image, so build one with a heavy song
  (`scripts/mksd.sh <dir with SONGS/001 Test.XML> /tmp/test_sd.img`), then
  `SD=/tmp/test_sd.img SEQ="6.0:_wait 0:l:tap 3.0:ret:tap 12.0:spc:tap 30.0:_quit" ./load_repro.sh <elf> <outdir> <host source wav> 2`
  and `python linein_continuity.py <outdir>` (needs numpy). The line-in tone on to-host ch1 has to be a perfect sine
  whatever the load; any discontinuity is a dropped/padded/skipped packet. Under `--icount` the SSIF production stats
  always read ~100%, so they don't show overload.
- Don't attach gdb while the key sequence is running - the song silently fails to load.
- RX alignment (to-host 1/2 sample-exact against the RX ring): run with `-- -s`, then `rx_alignment_dump.gdb` and
  `rx_alignment_check.py`. Symbol addresses in both are for one particular build - regenerate them with
  `arm-none-eabi-nm`. The last packet may be 44 frames, so a match one frame later than the 45-frame guess is fine.
- For counters: temporarily add a global struct, bump it in the recovery paths (to-host pad / skip-ahead / zero-length
  packet; from-host underrun / prime / drift drop / repeat; RX gap) and the SOF gap (via `getSystemTicks()`), then read
  it with `arm-none-eabi-gdb -batch` against QEMU's gdb stub. That's how the interrupt problem was found.
- The outputs (`DELUGEMU_SSIF_DUMP`) aren't a reliable reference under load: the render-head clamp turns overruns into
  gaps, and a loud song clips the host's direct-monitored audio.

## Open items

- Confirm the double-buffer fix on hardware, with a heavy song, both directions
- Windows: confirm it picks the adaptive alternate setting with both now at 540 bytes (it used to be the smaller one)
- Real host controllers / hubs accepting ~1170 bytes of isochronous reservation (fits the spec, but tight)
- Internal mic is mono on L only, so to-host ch2 is near silent with nothing plugged in. Option: copy L to R when
  neither jack's plugged in, as the Deluge's own monitoring does (about 3 more NEON ops per 4 frames)
- `Song::renderAudio()` critical section (above)
- Host 3/4 into an audio track / the sampler isn't covered by an automated test (needs UI driving)
- Songs saved with the stereo build's `USB` input load as channels 3/4 now - fine, but worth noting in release notes
