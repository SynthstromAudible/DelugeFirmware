#! /usr/bin/env python3
"""
FX Benchmark Collection Tool

End-to-end workflow for collecting FX benchmark data from Deluge:
1. Build firmware with ENABLE_FX_BENCHMARK=ON
2. Upload via sysex
3. Collect benchmark data via MIDI sysex
4. Write to CSV

Usage:
  dbt fx-benchmark              # Full workflow (build, upload, collect)
  dbt fx-benchmark --build      # Build only
  dbt fx-benchmark --upload     # Upload only
  dbt fx-benchmark --collect    # Collect data only
"""

import argparse
import csv
import time
import sys
import util


def argparser():
    parser = argparse.ArgumentParser(
        prog="fx-benchmark",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description="Collect FX benchmark data from Deluge",
        epilog="""\nWorkflow:
  1. dbt fx-benchmark --build    # Build with benchmarking enabled
  2. dbt fx-benchmark --upload   # Upload firmware via sysex
  3. dbt fx-benchmark --collect  # Collect data to CSV
  4. dbt fx-benchmark            # All of the above""",
        exit_on_error=False,
    )
    parser.group = "Development"
    parser.add_argument("--build", action="store_true", help="Build firmware only")
    parser.add_argument("--upload", action="store_true", help="Upload firmware only")
    parser.add_argument("--collect", action="store_true", help="Collect data only")
    parser.add_argument(
        "-o",
        "--output",
        default="fx_benchmark.csv",
        help="Output CSV file (default: fx_benchmark.csv)",
    )
    parser.add_argument(
        "-d",
        "--duration",
        type=int,
        default=None,
        help="Collection duration in seconds (default: run until Ctrl+C or Enter)",
    )
    parser.add_argument(
        "-p",
        "--port",
        type=int,
        default=None,
        help="MIDI port number (auto-detect if not specified)",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Show all debug messages (not just benchmarks)",
    )
    return parser


def build_firmware():
    """Build firmware with ENABLE_FX_BENCHMARK=ON"""
    util.note("")
    util.note("=" * 60)
    util.note("FIRMWARE BUILD")
    util.note("=" * 60)
    util.note("")
    util.note("Building firmware with FX benchmarking enabled.")
    util.note("Using 'relwithdebinfo' target (required for debug output).")
    util.note("")

    # Configure with benchmark flag and sysex loading
    util.note("[1/2] Configuring CMake...")
    util.note("      Flags: ENABLE_FX_BENCHMARK=ON, ENABLE_SYSEX_LOAD=YES")
    util.note("      This may take 1-2 minutes...")
    util.note("")
    ret = util.run(
        ["./dbt", "configure", "-DENABLE_FX_BENCHMARK=ON", "-DENABLE_SYSEX_LOAD=YES"]
    )
    if ret != 0:
        raise RuntimeError("Configure failed")

    util.note("")
    util.note("[2/2] Building firmware...")
    util.note("      This typically takes 5-10 minutes for a full build.")
    util.note("      (Incremental builds are faster)")
    util.note("")

    # Build relwithdebinfo - required for ENABLE_TEXT_OUTPUT which Debug::println needs
    ret = util.run(["./dbt", "build", "relwithdebinfo"])
    if ret != 0:
        raise RuntimeError("Build failed")

    util.note("")
    util.note("Build complete!")


def upload_firmware(port=None):
    """Upload firmware via sysex"""
    util.note("")
    util.note("=" * 60)
    util.note("FIRMWARE UPLOAD")
    util.note("=" * 60)
    util.note("")
    util.note("Please ensure your Deluge is:")
    util.note("  1. Connected via USB")
    util.note("  2. Configured to accept sysex firmware updates")
    util.note("     (Settings > MIDI > Firmware Update > Enabled)")
    util.note("")
    input("Press Enter when ready...")

    util.note("")
    util.note("Uploading firmware via sysex...")
    util.note("(This takes about 1-2 minutes)")
    util.note("")

    args = ["./dbt", "loadfw", "relwithdebinfo"]
    if port is not None:
        args.extend(["-p", str(port)])

    ret = util.run(args)
    if ret != 0:
        raise RuntimeError("Firmware upload failed")

    util.note("")
    util.note("Upload complete! Waiting for Deluge to restart...")
    util.note("(10 seconds)")
    time.sleep(10)


def collect_data(output_file, duration, port=None, verbose=False):
    """Collect benchmark data via MIDI sysex"""
    try:
        import rtmidi
    except ImportError:
        util.install_rtmidi()
        import rtmidi

    import select
    import sys

    util.note("")
    util.note("=" * 60)
    util.note("FX BENCHMARK DATA COLLECTION")
    util.note("=" * 60)
    util.note("")
    util.note("Please use your Deluge and invoke effects to benchmark:")
    util.note("  - Play audio clips with effects enabled")
    util.note("  - Try different effect combinations")
    util.note("  - Adjust effect parameters")
    util.note("")

    midiout = rtmidi.MidiOut()
    midiin = rtmidi.MidiIn()

    try:
        outport = util.ensure_midi_port("output", midiout, port)
        inport = util.ensure_midi_port("input ", midiin, port)

        midiout.open_port(outport)
        midiin.open_port(inport)
    except Exception as e:
        util.note(f"ERROR: {e}")
        util.report_available_midi_ports("output", midiout)
        util.report_available_midi_ports("input", midiin)
        raise

    midiin.ignore_types(False, True, True)

    # Enable sysex logging on Deluge
    with midiout:
        data = bytearray(
            [
                0xF0,  # sysex start
                0x00,
                0x21,
                0x7B,  # Deluge manufacturer ID
                0x01,  # protocol version
                0x03,  # debug namespace
                0x00,  # sysex logging config command
                0x01,  # enable
                0xF7,  # sysex end
            ]
        )
        midiout.send_message(data)
        util.note("Enabled sysex logging on Deluge")

    # Sysex header for debug log messages
    debug_log_header = [0xF0, 0x00, 0x21, 0x7B, 0x01, 0x03, 0x40, 0x00]

    # Collect data
    samples = []
    start_time = time.time()
    last_report = start_time

    util.note("")
    if duration:
        util.note(
            f"Collecting for {duration} seconds... (press Ctrl+C or Enter to stop early)"
        )
    else:
        util.note("Collecting... (press Ctrl+C or Enter to stop)")

    def check_for_enter():
        """Non-blocking check if Enter was pressed"""
        if sys.stdin.isatty():
            try:
                ready, _, _ = select.select([sys.stdin], [], [], 0)
                if ready:
                    sys.stdin.readline()
                    return True
            except (ValueError, OSError):
                pass
        return False

    try:
        while True:
            # Check for Enter key
            if check_for_enter():
                util.note("\nCollection stopped by user (Enter)")
                break

            # Check for duration timeout
            if duration and (time.time() - start_time >= duration):
                util.note(f"\nCollection completed ({duration}s)")
                break

            msg_and_dt = midiin.get_message()
            if msg_and_dt:
                msg, _ = msg_and_dt
                # Check for debug log message
                if len(msg) > 8 and msg[0:8] == debug_log_header:
                    try:
                        # Decode message - firmware just masks to 7-bit ASCII
                        text_bytes = bytearray(b & 0x7F for b in msg[8:-1])
                        text = text_bytes.decode("ascii", errors="ignore").strip()

                        # Verbose: show all debug messages
                        if verbose and text:
                            util.note(f"  [DBG] {text}")

                        # Look for CSV benchmark output: B,fx,cycles,ts,tag1,tag2,...
                        if text.startswith("B,"):
                            parts = text.split(",")
                            if len(parts) >= 4:
                                bench_data = {
                                    "fx": parts[1],
                                    "cycles": int(parts[2]),
                                    "ts": int(parts[3]),
                                    "tags": parts[4:] if len(parts) > 4 else [],
                                }
                                samples.append(bench_data)
                                if verbose:
                                    tags_str = ",".join(bench_data["tags"])
                                    util.note(
                                        f"  [{len(samples):4d}] {bench_data['fx']}: {bench_data['cycles']:,} [{tags_str}]"
                                    )
                    except (ValueError, UnicodeDecodeError, IndexError) as e:
                        if verbose:
                            util.note(f"  [ERR] Parse error: {e}")
            else:
                time.sleep(0.01)

            # Progress report every 30 seconds
            now = time.time()
            if now - last_report >= 30:
                elapsed = int(now - start_time)
                util.note(f"  ... {elapsed}s elapsed, {len(samples)} samples ...")
                last_report = now

    except KeyboardInterrupt:
        util.note("\nCollection stopped by user (Ctrl+C)")

    # Disable sysex logging
    with midiout:
        data = bytearray([0xF0, 0x00, 0x21, 0x7B, 0x01, 0x03, 0x00, 0x00, 0xF7])
        midiout.send_message(data)

    midiout.close_port()
    midiin.close_port()

    # Write CSV
    util.note("")
    if samples:
        with open(output_file, "w", newline="") as f:
            fieldnames = ["fx", "cycles", "ts", "tags"]
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            for sample in samples:
                row = {
                    "fx": sample.get("fx", ""),
                    "cycles": sample.get("cycles", 0),
                    "ts": sample.get("ts", 0),
                    "tags": ",".join(sample.get("tags", [])),
                }
                writer.writerow(row)

        util.note(f"Wrote {len(samples)} samples to {output_file}")
        util.note("")
        util.note("Summary (totals only - see CSV for subsections):")
        # Print summary by effect - only show total-level benchmarks (reverb_*, delay_*, etc.)
        # Subsection benchmarks (feather_fdn, etc.) are written to CSV but not shown here
        fx_counts = {}
        for sample in samples:
            fx = sample.get("fx", "unknown")
            # Only aggregate top-level benchmarks (pattern: category_name, not subsections)
            # Top-level: reverb_feather, reverb_mutable, delay_analog, etc.
            # Subsections would be: feather_fdn, feather_cascade (if we add them)
            is_total = (
                fx.startswith("reverb_")
                or fx.startswith("delay_")
                or fx.startswith("fx_")
            )
            if not is_total:
                continue
            if fx not in fx_counts:
                fx_counts[fx] = {"count": 0, "total_cycles": 0, "tags": {}}
            fx_counts[fx]["count"] += 1
            fx_counts[fx]["total_cycles"] += sample.get("cycles", 0)
            # Track tag distribution for mode breakdown
            for tag in sample.get("tags", []):
                if tag:
                    fx_counts[fx]["tags"][tag] = fx_counts[fx]["tags"].get(tag, 0) + 1

        for fx, stats in sorted(fx_counts.items()):
            avg = stats["total_cycles"] // stats["count"] if stats["count"] > 0 else 0
            tag_info = ""
            if stats["tags"]:
                tag_parts = [f"{t}:{c}" for t, c in sorted(stats["tags"].items())]
                tag_info = f" [{', '.join(tag_parts)}]"
            util.note(f"  {fx}: {stats['count']} samples, avg {avg:,} cycles{tag_info}")
    else:
        util.note("No samples collected!")
        util.note("Make sure effects are active and producing audio.")

    return samples


def main():
    parser = argparser()

    try:
        args = parser.parse_args()
    except Exception as e:
        util.note(f"ERROR: {e}")
        return 1

    # Default: do everything
    do_all = not (args.build or args.upload or args.collect)

    try:
        if args.build or do_all:
            build_firmware()

        if args.upload or do_all:
            upload_firmware(args.port)

        if args.collect or do_all:
            collect_data(args.output, args.duration, args.port, args.verbose)

    except Exception as e:
        util.note(f"ERROR: {e}")
        return 1

    util.note("")
    util.note("Done!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
