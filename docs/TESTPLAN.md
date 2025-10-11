# Simtemp Test Plan

## T1 — Build & Load (Fedora 42 x86)
**Commands**
- `./scripts/build.sh`
- `sudo insmod kernel/nxp_simtemp.ko force_create_dev=1`
- `ls /sys/class/simtemp`
- `ls -l /dev/nxp_simtemp`

**Expected**
- Build succeeds without errors (module signed if Secure Boot is enabled).
- `simtemp0` directory present under `/sys/class/simtemp`.
- `/dev/nxp_simtemp` character device exists.
**Result (2025-10-04)**
- `./scripts/build.sh` → PASS (Fedora 42, 6.16.8; module signed).
- `./scripts/run_demo.sh` → PASS (stream/test + stats).

## T1b — Build & Load (Orange Pi Zero3, Armbian 6.12.47)
**Prereq**
- Install kernel image/DTB/headers (`linux-{image,dtb,headers}-current-sunxi64_25.11.0-trunk_arm64__6.12.47-*.deb`) and reboot.

**Commands**
- `make -C /lib/modules/$(uname -r)/build M=$(pwd)/kernel modules`
- `sudo insmod kernel/nxp_simtemp.ko force_create_dev=1`
- `ls -l /dev/nxp_simtemp`
- `sudo cat /sys/class/simtemp/simtemp0/stats`

**Expected**
- Module builds against `/usr/src/linux-headers-6.12.47-current-sunxi64` without `.gnu.linkonce.this_module` errors.
- `/dev/nxp_simtemp` and `simtemp0` appear once loaded.
- Primary counters (`updates`, `alerts`) increment after CLI tests; `errors` stays zero.
**Result (2025-10-04)**
- `make -C /lib/modules/... modules` → PASS (Armbian 6.12.47).
- `./scripts/run_demo.sh` → PASS (overlay/stream/test/stats).

## T1c — Build (Ubuntu 24.04 LTS cloud VM)
**Setup summary**
- Launch Ubuntu 24.04 cloud image with libvirt (`virt-install --import --osinfo ubuntu24.04 ...`).
- Ensure apt sources use HTTPS (IPv4 in the VM) before updating.

**Commands**
- `sudo apt update`
- `sudo apt install -y build-essential linux-headers-$(uname -r) git python3`
- `git clone https://github.com/Syncdelic/nxp_simtemp.git`
- `cd nxp_simtemp`
- `./scripts/build.sh`

**Expected**
- Script completes with `Built: kernel/nxp_simtemp.ko` (signing not required in the VM).
- `modinfo kernel/nxp_simtemp.ko | grep vermagic` reports the running Ubuntu kernel (e.g. `6.8.0-85-generic`).

**Result (2025-10-04)**
- `./scripts/build.sh` → PASS (vermagic `6.8.0-85-generic`).
- `./scripts/run_demo.sh` → PASS (stream/test + stats).

## T2 — CLI Stream
**Commands**
- `sudo python3 user/cli/main.py stream --count 5`

**Expected**
- Five lines printed with ISO-8601 timestamps, temperatures (°C), and flags (`0x01`/`0x03`).
- Command exits cleanly; no errors (non-blocking reads recover from `EAGAIN`).
**Result (2025-10-09)**
- PASS (`stream --count 5` emitted 5 samples, flags toggled 0x03/0x01, dmesg clean).

## T3 — CLI Alert Self-Test
**Commands**
- `sudo python3 user/cli/main.py test`
- `cat /sys/class/simtemp/simtemp0/stats`

**Expected**
- CLI prints `PASS: alert observed ... flags=0x03`; restores original threshold/mode even on failure.
- `stats` shows `updates` incremented, `errors` unchanged (unless negative tests follow).
**Result (2025-10-09)**
- PASS (`test` observed alert after 65 samples; stats -> updates=785 alerts=43 errors=0).

## T4 — Mode & Stats Validation
**Commands**
- `sudo python3 user/cli/main.py stream --mode ramp --sampling-ms 200 --duration 3`
- `cat /sys/class/simtemp/simtemp0/mode`
- `echo invalid | sudo tee /sys/class/simtemp/simtemp0/mode`
- `cat /sys/class/simtemp/simtemp0/stats`
- `echo normal | sudo tee /sys/class/simtemp/simtemp0/mode`

**Expected**
- Stream output shows ramping temperatures; alerts trigger (`flags=0x03`).
- Mode reads back `ramp` during stream. Invalid write returns `Invalid argument`; `stats` `errors` increases by 1.
- Mode restored to `normal` afterward.
**Result (2025-10-09)**
- PASS (ramp stream produced threshold alerts; invalid `mode` write -> -EINVAL + dmesg warn; stats errors incremented to 1, mode restored to `normal`).

## T5 — Negative CLI Cases
**Commands**
- `sudo python3 user/cli/main.py --index 5 stream --count 1`
- `sudo rmmod nxp_simtemp`
- `sudo python3 user/cli/main.py stream --count 1`
- `sudo insmod kernel/nxp_simtemp.ko force_create_dev=1`

**Expected**
- First command prints parser error about index out of range.
- With module unloaded, CLI reports `sysfs root /sys/class/simtemp does not exist`.
- Module reloads cleanly for subsequent tests.
**Result (2025-10-09)**
- PASS (CLI rejected `--index 5`; missing sysfs reported with module removed; `insmod ... force_create_dev=1` reloaded without errors).

## T6 — Demo Script
**Commands**
- `./scripts/run_demo.sh`

**Expected**
- Script loads module, runs CLI stream/test, prints stats, and unloads.
- Output ends with `Demo completed successfully.`
**Result (2025-10-09)**
- PASS (`run_demo.sh` rebuilt, ran stream/test, printed stats updates=8 alerts=4 errors=0, and unloaded cleanly).
**Result (2025-10-09, Raspberry Pi 4B)**
- PASS (`run_demo.sh` built against 6.12.44-current-bcm2711; stream/test PASS, stats updates=7 alerts=1 errors=0).

## T7 — Device Tree Defaults (ARM target)
- Mount configfs once (if not already): `sudo mount -t configfs none /sys/kernel/config`.
- Build overlay: `dtc -@ -I dts -O dtb -o /tmp/nxp-simtemp.dtbo kernel/dts/nxp-simtemp-overlay.dts`.
- Apply overlay: `sudo mkdir -p /sys/kernel/config/device-tree/overlays/nxp-simtemp` then `sudo sh -c 'cat /tmp/nxp-simtemp.dtbo > /sys/kernel/config/device-tree/overlays/nxp-simtemp/dtbo'`.
- Load module built against Armbian headers: `sudo insmod kernel/nxp_simtemp.ko` (or `sudo modprobe nxp_simtemp` after installing the .ko into `/lib/modules`).
- `ls -l /dev/nxp_simtemp` and `cat /sys/class/simtemp/simtemp0/{sampling_ms,threshold_mC,mode}`.
- Run CLI: `sudo python3 user/cli/main.py stream --count 5` and `sudo python3 user/cli/main.py test`.
- Clean up: `sudo rmmod nxp_simtemp; sudo rmdir /sys/kernel/config/device-tree/overlays/nxp-simtemp`.

**Expected**
- Overlay registers `simtemp0` automatically; `sampling_ms`, `threshold_mC`, and `mode` reflect DT defaults.
- CLI stream/test behave as on x86; stats update; no `force_create_dev` needed.
- Module unloads/overlay removal leave sysfs/dev nodes clean.
**Result (2025-10-04)**
- Overlay applied on Orange Pi Zero3; `./scripts/run_demo.sh` → PASS (stream/test).
- `stats` after demo: updates=9 alerts=9 errors=0.
**Result (2025-10-09)**
- PASS (Orange Pi Zero3 with DT overlay: `insmod` sans force flag, stream/test succeeded, stats=updates=357 alerts=349 errors=0, clean unload/overlay removal).

## T8 — CLI Unit Tests (desktop/Linux)
**Commands**
- `python3 -m venv .venv`
- `source .venv/bin/activate`
- `pip install -r requirements-dev.txt`
- `pytest -vv`
- `deactivate`

**Expected**
- Unit suite (boundary/white-box/black-box cases) in `tests/test_cli.py` passes; no external dependencies.

**Result (2025-10-09)**
- PASS (`pytest -vv` reported 15/15 tests in 0.14s on Fedora 42).

## T9 — Optional Stress / Scaling (Legacy Timer)
**Commands**
- `echo 5 | sudo tee /sys/class/simtemp/simtemp0/sampling_ms`
- `sudo python3 user/cli/main.py stream --duration 5`
- `cat /sys/class/simtemp/simtemp0/stats`

**Expected**
- Stream runs without errors for 5 seconds at ~200 Hz (5 ms clamp); `updates` climbs quickly; `errors` remains 0.
- Note CPU utilisation; document limitations and plan for hrtimer-based enhancement before attempting ≥1 kHz.

## T10 — High-rate Worker (sampling_us=100)
**Commands**
- `echo 100 | sudo tee /sys/class/simtemp/simtemp0/sampling_us`
- `sudo python3 user/cli/main.py stream --duration 5`
- `sudo python3 user/cli/main.py test --sampling-us 100 --max-periods 5`
- `cat /sys/class/simtemp/simtemp0/stats`

**Expected**
- Worker thread produces ~50k samples/s without overruns (`errors=0`).
- CLI stream/test remain stable; timestamps monotonic and alerts raised promptly.

**Result (2025-10-10, Fedora 42 / 6.16.9)**
- `sampling_us` → `updates` 561,215 / `alerts` 313,118 / `errors` 0 after 5 s; high-rate self-test PASS.

**Result (2025-10-10, Ubuntu 24.04 VM / 6.8.0-85)**
- `sampling_us` → `updates` 309,721 / `alerts` 186,396 / `errors` 0 after 5 s; high-rate self-test PASS.

**Result (2025-10-10, Orange Pi Zero3 / Armbian 6.12.47)**
- Overlay + worker thread produced `updates` 281,082 / `alerts` 175,522 / `errors` 0; high-rate self-test PASS.

**Result (2025-10-11, Raspberry Pi 4B / Armbian 6.12.44)**
- `force_create_dev=1`; `updates` 267,950 / `alerts` 184,490 / `errors` 0; high-rate self-test PASS.

Record PASS/FAIL for each test and any observations (warnings, thresholds, anomalies) before submission.
