"""Pytest suite for nxp_simtemp CLI helpers."""

from __future__ import annotations

import argparse
import importlib.util
import os
import sys
import time
from pathlib import Path
from typing import Any, List, Optional, Tuple, Union

import pytest

# Load the CLI module without requiring a package install.
ROOT = Path(__file__).resolve().parents[1]
CLI_PATH = ROOT / "user" / "cli" / "main.py"
SPEC = importlib.util.spec_from_file_location("nxp_simtemp_cli", CLI_PATH)
cli = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = cli
assert SPEC.loader is not None
SPEC.loader.exec_module(cli)  # type: ignore[assignment]


# ---------------------------------------------------------------------------
# Boundary-value tests
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    "value",
    ["1", "32768"],
    ids=lambda v: f"positive_int_valid_{v}",
)
def test_positive_int_boundary_valid(value: str) -> None:
    """Accepts strictly positive integers including the lower bound (1)."""

    assert cli.positive_int(value) == int(value)


@pytest.mark.parametrize(
    "value",
    ["0", "-5"],
    ids=lambda v: f"positive_int_invalid_{v}",
)
def test_positive_int_boundary_invalid(value: str) -> None:
    """Rejects zero and negative inputs for positive_int()."""

    with pytest.raises(argparse.ArgumentTypeError):
        cli.positive_int(value)


@pytest.mark.parametrize(
    "value",
    ["0", "1024"],
    ids=lambda v: f"non_negative_valid_{v}",
)
def test_non_negative_int_boundary_valid(value: str) -> None:
    """Accepts zero and positive integers for non_negative_int()."""

    assert cli.non_negative_int(value) == int(value)


@pytest.mark.parametrize(
    "value",
    ["-1", "-100"],
    ids=lambda v: f"non_negative_invalid_{v}",
)
def test_non_negative_int_boundary_invalid(value: str) -> None:
    """Rejects negative inputs for non_negative_int()."""

    with pytest.raises(argparse.ArgumentTypeError):
        cli.non_negative_int(value)


# ---------------------------------------------------------------------------
# White-box tests (exercise internal behaviour of SimtempDevice helpers)
# ---------------------------------------------------------------------------


def _write_attrs(devdir: Path, *, sampling: int, threshold: int, mode: str) -> None:
    (devdir / "sampling_ms").write_text(f"{sampling}\n")
    (devdir / "sampling_us").write_text(f"{sampling * 1000}\n")
    (devdir / "threshold_mC").write_text(f"{threshold}\n")
    (devdir / "mode").write_text(f"{mode}\n")


def test_simtemp_device_snapshot_and_write(tmp_path: Path) -> None:
    """SimtempDevice reads/writes sysfs attributes for selected index."""

    sysfs_root = tmp_path
    devdir = sysfs_root / "simtemp0"
    devdir.mkdir()
    _write_attrs(devdir, sampling=100, threshold=45000, mode="normal")

    device = cli.SimtempDevice(sysfs_root, index=0, device_path=Path("/dev/fake"))
    snapshot = device.snapshot()
    assert snapshot.sampling_us == 100 * 1000
    assert snapshot.threshold_mc == 45000
    assert snapshot.mode == "normal"

    device.write("sampling_ms", "250")
    assert (devdir / "sampling_ms").read_text().strip() == "250"


def test_simtemp_device_index_out_of_range(tmp_path: Path) -> None:
    """Raises IndexError if requested index exceeds available directories."""

    sysfs_root = tmp_path
    (sysfs_root / "simtemp0").mkdir()

    with pytest.raises(IndexError):
        cli.SimtempDevice(sysfs_root, index=1, device_path=None)


def test_simtemp_device_chooses_sorted_entry(tmp_path: Path) -> None:
    """SimtempDevice picks directories in sorted order regardless of creation sequence."""

    sysfs_root = tmp_path
    dev1 = sysfs_root / "simtemp10"
    dev0 = sysfs_root / "simtemp0"
    dev1.mkdir()
    dev0.mkdir()  # intentionally created second to exercise alphabetical sort
    _write_attrs(dev0, sampling=50, threshold=40000, mode="normal")
    _write_attrs(dev1, sampling=75, threshold=41000, mode="noisy")

    first = cli.SimtempDevice(sysfs_root, index=0, device_path=None)
    assert first.sysfs_dir == dev0
    second = cli.SimtempDevice(sysfs_root, index=1, device_path=None)
    assert second.sysfs_dir == dev1


def test_write_sampling_prefers_microseconds(tmp_path: Path) -> None:
    sysfs_root = tmp_path
    devdir = sysfs_root / "simtemp0"
    devdir.mkdir()
    _write_attrs(devdir, sampling=100, threshold=45000, mode="normal")

    device = cli.SimtempDevice(sysfs_root, index=0, device_path=None)
    cli.write_sampling(device, sampling_us=500, sampling_ms=None)
    assert (devdir / "sampling_us").read_text().strip() == "500"


def test_write_sampling_fallbacks_to_ms(tmp_path: Path) -> None:
    sysfs_root = tmp_path
    devdir = sysfs_root / "simtemp0"
    devdir.mkdir()
    _write_attrs(devdir, sampling=100, threshold=45000, mode="normal")

    device = cli.SimtempDevice(sysfs_root, index=0, device_path=None)
    original_write = device.write

    def fake_write(name: str, value: str) -> None:
        if name == "sampling_us":
            raise FileNotFoundError("sampling_us missing")
        original_write(name, value)

    device.write = fake_write  # type: ignore[assignment]
    cli.write_sampling(device, sampling_us=500, sampling_ms=None)
    assert (devdir / "sampling_ms").read_text().strip() == "1"


# ---------------------------------------------------------------------------
# White-box tests for wait_for_alert (exercise internal polling behaviour)
# ---------------------------------------------------------------------------


def test_wait_for_alert_success(monkeypatch: pytest.MonkeyPatch) -> None:
    """wait_for_alert returns a sample when poll/read deliver data before timeout."""

    fd = 99
    opened: List[Path] = []

    def fake_open(path: Union[str, Path], flags: int) -> int:
        opened.append(Path(path))
        return fd

    monkeypatch.setattr(os, "open", fake_open)
    monkeypatch.setattr(os, "close", lambda _: None)

    sample_bytes = cli.SIMTEMP_SAMPLE_STRUCT.pack(
        1234567890,
        42000,
        cli.SIMTEMP_FLAG_ALERT,
    )
    reads = [sample_bytes]

    def fake_read(handle: int, size: int) -> bytes:
        assert handle == fd
        assert size == cli.SIMTEMP_SAMPLE_STRUCT.size
        return reads.pop(0) if reads else b""

    monkeypatch.setattr(os, "read", fake_read)

    class FakePoll:
        def __init__(self) -> None:
            self.registered: Optional[Tuple[int, int]] = None
            self.calls = 0

        def register(self, handle: int, events: int) -> None:
            self.registered = (handle, events)

        def poll(self, timeout: int) -> List[Tuple[int, int]]:
            self.calls += 1
            assert timeout > 0
            return [(fd, 0)]  # signal data immediately

    fake_poll = FakePoll()
    monkeypatch.setattr(cli.select, "poll", lambda: fake_poll)

    ticks = [0.0]

    def fake_monotonic() -> float:
        ticks[0] += 0.01
        return ticks[0]

    monkeypatch.setattr(time, "monotonic", fake_monotonic)
    monkeypatch.setattr(cli.time, "monotonic", fake_monotonic)

    success, sample, count = cli.wait_for_alert(Path("/dev/nxp_simtemp"), sampling_us=100_000, max_periods=1)
    assert success is True
    assert count == 1
    assert sample == (1234567890, 42000, cli.SIMTEMP_FLAG_ALERT)
    assert opened[0] == Path("/dev/nxp_simtemp")


def test_wait_for_alert_timeout(monkeypatch: pytest.MonkeyPatch) -> None:
    """wait_for_alert times out gracefully when poll() yields no events."""

    fd = 55

    monkeypatch.setattr(os, "open", lambda path, flags: fd)
    monkeypatch.setattr(os, "close", lambda _: None)

    # No data ever arrives.
    monkeypatch.setattr(os, "read", lambda handle, size: b"")

    class IdlePoll:
        def register(self, handle: int, events: int) -> None:
            assert handle == fd

        def poll(self, timeout: int) -> List[Tuple[int, int]]:
            return []

    monkeypatch.setattr(cli.select, "poll", lambda: IdlePoll())

    ticks = [0.0]

    def fake_monotonic() -> float:
        ticks[0] += 0.3
        return ticks[0]

    monkeypatch.setattr(time, "monotonic", fake_monotonic)
    monkeypatch.setattr(cli.time, "monotonic", fake_monotonic)

    success, sample, count = cli.wait_for_alert(Path("/dev/nxp_simtemp"), sampling_us=100_000, max_periods=1)
    assert success is False
    assert sample is None
    assert count == 0


# ---------------------------------------------------------------------------
# Black-box tests (exercise CLI entry points via main())
# ---------------------------------------------------------------------------


def test_main_test_command_success(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    """Invoking `nxp_simtemp main test` reports PASS and restores original settings."""

    writes: List[Tuple[str, str]] = []

    class FakeDevice:
        def __init__(self, sysfs_root: Path, index: int, device_path: Optional[Path]) -> None:
            self.sysfs_root = sysfs_root
            self.index = index
            self.device_path = device_path
            self.char_device = Path("/dev/nxp_simtemp")

        def write(self, name: str, value: str) -> None:
            writes.append((name, value))

        def snapshot(self) -> cli.SimtempConfig:
            return cli.SimtempConfig(sampling_us=100_000, threshold_mc=46000, mode="normal")

    monkeypatch.setattr(cli, "SimtempDevice", FakeDevice)
    monkeypatch.setattr(
        cli,
        "wait_for_alert",
        lambda *_args, **_kwargs: (True, (111, 47000, cli.SIMTEMP_FLAG_ALERT), 1),
    )

    rc = cli.main(["test", "--max-periods", "1"])
    out = capsys.readouterr().out

    assert rc == 0
    assert "PASS: alert observed" in out
    # Expect threshold lowered for the test, then restored to original snapshot value.
    assert writes[:2] == [
        ("threshold_mC", str(cli.DEFAULT_TEST_THRESHOLD_MC)),
        ("threshold_mC", "46000"),
    ]


def test_main_handles_device_errors(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    """CLI surfaces device discovery errors as parser failures (black-box)."""

    def fake_device(*_args: Any, **_kwargs: Any) -> None:  # pragma: no cover - exercised via exception path
        raise FileNotFoundError("sysfs root missing")

    monkeypatch.setattr(cli, "SimtempDevice", fake_device)

    with pytest.raises(SystemExit) as excinfo:
        cli.main(["stream", "--count", "1"])

    captured = capsys.readouterr().err
    assert excinfo.value.code == 2  # argparse exits with code 2 on parser errors
    assert "sysfs root missing" in captured
