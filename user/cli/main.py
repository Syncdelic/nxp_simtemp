#!/usr/bin/env python3
"""nxp_simtemp command line interface.

Provides: 
  * stream – configure the device and print samples until interrupted (default)
  * test   – lower the threshold and ensure an alert fires within a few periods

All configuration is performed via sysfs; samples are read from `/dev/nxp_simtemp`.
Run as root (or with sudo) so writes to sysfs and reads from the character device
succeed.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import os
import select
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

SIMTEMP_SAMPLE_STRUCT = struct.Struct("<QiI")
SIMTEMP_FLAG_ALERT = 1 << 1
DEFAULT_CHAR_DEVICE = Path("/dev/nxp_simtemp")
DEFAULT_SYSFS_ROOT = Path("/sys/class/simtemp")
DEFAULT_TEST_THRESHOLD_MC = 20000
DEFAULT_TEST_MAX_PERIODS = 2
DEFAULT_POLL_TIMEOUT_MS = 1000
MICROS_PER_SEC = 1_000_000


@dataclass
class SimtempConfig:
    sampling_us: int
    threshold_mc: int
    mode: str


class SimtempDevice:
    """Helper that abstracts sysfs access for a single simtemp instance."""

    def __init__(self, sysfs_root: Path, index: int, device_path: Optional[Path]):
        if not sysfs_root.exists():
            raise FileNotFoundError(f"sysfs root {sysfs_root} does not exist")

        devices = sorted(p for p in sysfs_root.glob("simtemp*") if p.is_dir())
        if not devices:
            raise FileNotFoundError(f"no simtemp devices under {sysfs_root}")
        if index < 0 or index >= len(devices):
            raise IndexError(f"requested device index {index} out of range (0-{len(devices)-1})")

        self.sysfs_dir = devices[index]
        self.char_device = device_path or DEFAULT_CHAR_DEVICE

    def _attr_path(self, name: str) -> Path:
        return self.sysfs_dir / name

    def read_int(self, name: str) -> int:
        return int(self._attr_path(name).read_text().strip())

    def read_str(self, name: str) -> str:
        return self._attr_path(name).read_text().strip()

    def write(self, name: str, value: str) -> None:
        self._attr_path(name).write_text(f"{value}\n")

    def snapshot(self) -> SimtempConfig:
        try:
            sampling_us = self.read_int("sampling_us")
        except FileNotFoundError:
            sampling_us = self.read_int("sampling_ms") * 1000

        return SimtempConfig(
            sampling_us=sampling_us,
            threshold_mc=self.read_int("threshold_mC"),
            mode=self.read_str("mode"),
        )


def iso8601_from_ns(ns: int) -> str:
    dt = _dt.datetime.fromtimestamp(ns / 1_000_000_000, tz=_dt.timezone.utc)
    return dt.isoformat(timespec="milliseconds")


def write_sampling(device: SimtempDevice, *, sampling_us: Optional[int], sampling_ms: Optional[int]) -> None:
    if sampling_us is not None:
        try:
            device.write("sampling_us", str(sampling_us))
            return
        except FileNotFoundError:
            # Fallback to millisecond attribute on older kernels.
            sampling_ms = max(1, sampling_us // 1000)

    if sampling_ms is not None:
        device.write("sampling_ms", str(sampling_ms))

def stream_command(args: argparse.Namespace) -> int:
    device = SimtempDevice(args.sysfs_root, args.index, args.device)

    write_sampling(device, sampling_us=args.sampling_us, sampling_ms=args.sampling_ms)
    if args.threshold_mc is not None:
        device.write("threshold_mC", str(args.threshold_mc))
    if args.mode is not None:
        device.write("mode", args.mode)

    count_limit = args.count
    deadline = time.monotonic() + args.duration if args.duration else None

    fd = os.open(device.char_device, os.O_RDONLY | os.O_NONBLOCK)
    poller = select.poll()
    poller.register(fd, select.POLLIN | select.POLLPRI)

    samples = 0
    try:
        while True:
            if deadline is not None and time.monotonic() >= deadline:
                break
            if count_limit is not None and samples >= count_limit:
                break

            events = poller.poll(DEFAULT_POLL_TIMEOUT_MS)
            if not events:
                continue

            try:
                data = os.read(fd, SIMTEMP_SAMPLE_STRUCT.size)
            except BlockingIOError:
                continue
            if len(data) < SIMTEMP_SAMPLE_STRUCT.size:
                continue

            timestamp_ns, temp_mc, flags = SIMTEMP_SAMPLE_STRUCT.unpack(data)
            ts = iso8601_from_ns(timestamp_ns)
            temp_c = temp_mc / 1000.0
            alert = 1 if flags & SIMTEMP_FLAG_ALERT else 0
            print(f"{ts} temp={temp_c:.1f}C alert={alert} flags=0x{flags:02x}")
            samples += 1
    except KeyboardInterrupt:
        pass
    finally:
        os.close(fd)

    return 0


def wait_for_alert(char_device: Path, sampling_us: int, max_periods: int) -> tuple[bool, Optional[tuple[int, int, int]], int]:
    fd = os.open(char_device, os.O_RDONLY | os.O_NONBLOCK)
    poller = select.poll()
    poller.register(fd, select.POLLIN | select.POLLPRI)

    timeout_s = max_periods * sampling_us / MICROS_PER_SEC
    deadline = time.monotonic() + max(timeout_s, 0.5)
    samples = 0
    try:
        while time.monotonic() < deadline:
            remaining_ms = max(int((deadline - time.monotonic()) * 1000), 50)
            events = poller.poll(remaining_ms)
            if not events:
                continue
            try:
                data = os.read(fd, SIMTEMP_SAMPLE_STRUCT.size)
            except BlockingIOError:
                continue
            if len(data) < SIMTEMP_SAMPLE_STRUCT.size:
                continue
            samples += 1
            unpacked = SIMTEMP_SAMPLE_STRUCT.unpack(data)
            _, _, flags = unpacked
            if flags & SIMTEMP_FLAG_ALERT:
                return True, unpacked, samples
        return False, None, samples
    finally:
        os.close(fd)


def test_command(args: argparse.Namespace) -> int:
    device = SimtempDevice(args.sysfs_root, args.index, args.device)
    original = device.snapshot()
    changed_sampling = args.sampling_ms is not None or args.sampling_us is not None
    changed_mode = args.mode is not None

    try:
        write_sampling(device, sampling_us=args.sampling_us, sampling_ms=args.sampling_ms)
        if args.mode is not None:
            device.write("mode", args.mode)

        test_threshold = args.threshold_mc if args.threshold_mc is not None else DEFAULT_TEST_THRESHOLD_MC
        device.write("threshold_mC", str(test_threshold))

        effective_sampling_us = (
            args.sampling_us
            if args.sampling_us is not None
            else args.sampling_ms * 1000 if args.sampling_ms is not None
            else original.sampling_us
        )
        success, sample, count = wait_for_alert(
            device.char_device,
            sampling_us=effective_sampling_us,
            max_periods=args.max_periods,
        )

        if success and sample is not None:
            ts_ns, temp_mc, flags = sample
            ts = iso8601_from_ns(ts_ns)
            temp_c = temp_mc / 1000.0
            print(f"PASS: alert observed after {count} sample(s) at {ts} temp={temp_c:.1f}C flags=0x{flags:02x}")
            return 0

        print(
            "FAIL: no threshold alert within "
            f"{args.max_periods} period(s) (sampling_us={effective_sampling_us})"
        )
        return 1
    finally:
        device.write("threshold_mC", str(original.threshold_mc))
        if changed_sampling:
            write_sampling(device, sampling_us=original.sampling_us, sampling_ms=None)
        if changed_mode:
            device.write("mode", original.mode)


def positive_int(value: str) -> int:
    ivalue = int(value)
    if ivalue <= 0:
        raise argparse.ArgumentTypeError("value must be > 0")
    return ivalue


def non_negative_int(value: str) -> int:
    ivalue = int(value)
    if ivalue < 0:
        raise argparse.ArgumentTypeError("value must be >= 0")
    return ivalue


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="nxp_simtemp CLI")
    parser.add_argument(
        "--sysfs-root",
        type=Path,
        default=DEFAULT_SYSFS_ROOT,
        help="Root path for simtemp sysfs class (default: /sys/class/simtemp)",
    )
    parser.add_argument(
        "--device",
        type=Path,
        default=None,
        help="Character device to read (default: /dev/nxp_simtemp)",
    )
    parser.add_argument(
        "--index",
        type=non_negative_int,
        default=0,
        help="Device index under sysfs root (default: 0)",
    )

    subparsers = parser.add_subparsers(dest="command")

    stream = subparsers.add_parser("stream", help="Stream samples to stdout (default)")
    stream.add_argument("--count", type=positive_int, default=None, help="Stop after N samples")
    stream.add_argument(
        "--duration",
        type=float,
        default=None,
        help="Stop after D seconds (default: run until interrupted)",
    )
    stream.add_argument("--sampling-ms", type=positive_int, default=None, help="Update sampling period")
    stream.add_argument("--sampling-us", type=positive_int, default=None, help="Update sampling period in microseconds")
    stream.add_argument("--threshold-mc", type=int, default=None, help="Update threshold in milli °C")
    stream.add_argument("--mode", choices=["normal", "noisy", "ramp"], default=None, help="Select mode")
    stream.set_defaults(func=stream_command)

    test = subparsers.add_parser("test", help="Run threshold alert self-test")
    test.add_argument("--sampling-ms", type=positive_int, default=None, help="Override sampling period for the test")
    test.add_argument("--sampling-us", type=positive_int, default=None, help="Override sampling period (microseconds)")
    test.add_argument(
        "--threshold-mc",
        type=int,
        default=None,
        help=f"Threshold to use during the test (default: {DEFAULT_TEST_THRESHOLD_MC})",
    )
    test.add_argument("--mode", choices=["normal", "noisy", "ramp"], default=None, help="Optional mode override")
    test.add_argument(
        "--max-periods",
        type=positive_int,
        default=DEFAULT_TEST_MAX_PERIODS,
        help="Fail if no alert after this many sampling periods",
    )
    test.set_defaults(func=test_command)

    parser.set_defaults(func=stream_command)
    return parser


def main(argv: Optional[list[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (FileNotFoundError, PermissionError, IndexError) as exc:
        parser.error(str(exc))
    except OSError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
