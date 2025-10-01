# Simtemp Test Plan

## T1 — Build & Load (x86)
**Commands**
- `./scripts/build.sh`
- `sudo insmod kernel/nxp_simtemp.ko force_create_dev=1`
- `ls /sys/class/simtemp`
- `ls -l /dev/nxp_simtemp`

**Expected**
- Build succeeds without errors (module signed if Secure Boot is enabled).
- `simtemp0` directory present under `/sys/class/simtemp`.
- `/dev/nxp_simtemp` character device exists.

## T2 — CLI Stream
**Commands**
- `sudo python3 user/cli/main.py stream --count 5`

**Expected**
- Five lines printed with ISO-8601 timestamps, temperatures (°C), and flags (`0x01`/`0x03`).
- Command exits cleanly; no errors.

## T3 — CLI Alert Self-Test
**Commands**
- `sudo python3 user/cli/main.py test`
- `cat /sys/class/simtemp/simtemp0/stats`

**Expected**
- CLI prints `PASS: alert observed ... flags=0x03` (or `FAIL` if alert missing).
- `stats` shows `updates` incremented, `errors` unchanged (unless negative tests follow).

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

## T6 — Demo Script
**Commands**
- `./scripts/run_demo.sh`

**Expected**
- Script loads module, runs CLI stream/test, prints stats, and unloads.
- Output ends with `Demo completed successfully.`

## T7 — Device Tree Defaults (ARM target)
**Commands**
- Deploy overlay derived from `kernel/dts/nxp-simtemp.dtsi` to target (board-specific).
- `make -C kernel KDIR=/path/to/headers`
- `sudo insmod kernel/nxp_simtemp.ko`
- `cat /sys/class/simtemp/simtemp0/{sampling_ms,threshold_mC,mode}`
- `sudo python3 user/cli/main.py stream --count 5`
- `sudo python3 user/cli/main.py test`

**Expected**
- Sysfs reflects DT defaults.
- CLI stream/test behave as on x86; alerts observed; stats update.
- Module unloads cleanly afterward.

## T8 — Optional Stress / Scaling
**Commands**
- `sudo python3 user/cli/main.py stream --sampling-ms 10 --duration 5`
- `cat /sys/class/simtemp/simtemp0/stats`

**Expected**
- Stream runs without errors for 5 seconds; `updates` climbs quickly; `errors` remains 0.
- Note any anomalies (missed samples, CPU load) for scaling discussion.

Record PASS/FAIL for each test and any observations (warnings, thresholds, anomalies) before submission.
