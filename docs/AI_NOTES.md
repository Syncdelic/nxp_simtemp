## 2025-09-26 — Bootstrap & Secure Boot
- Added `force_create_dev` to self-register a platform_device on x86 so probe runs without DT.
- Fixed modern `platform_driver.remove` signature (void) on 6.16.x.
- Secure Boot: generated MOK, enrolled via mokutil/MOK Manager, signed .ko with scripts/sign-file.
- Rebuilt after kernel update to fix vermagic; verified load → probe → remove.
- Prepared for next step: add sysfs attrs (sampling_ms, threshold_mC).

## 2025-09-28 — Sysfs scaffolding & docs refresh
- Prompted AI to refactor driver into header-backed layout, add sysfs class device, and introduce DT snippet + shared ioctl header.
- Iteratively fixed Fedora 6.16 build break (`class_create` signature) and validated clamp warnings via manual sysfs writes.
- Updated demo script to automate the load → verify → clamp → unload flow; captured instructions for cross-arch portability.
- Regenerated README with the new workflow and drafted DESIGN.md (Mermaid diagram) outlining kernel/user-space architecture.
- Confirmed build/test steps locally before planning multi-commit history (kernel vs scripts vs docs).
## 2025-09-29 — Character device & pollable data path
- Added ring buffer + miscdevice implementation so `/dev/nxp_simtemp` streams `struct simtemp_sample`; validated blocking reads and `poll()`/`POLLPRI` alerts by lowering the threshold.
- Updated generator to synthesize temperatures with jitter and ensured timers rebuild on sampling changes.
- Ran manual tests (`dd`, Python poll snippet) after rebuild/sign to confirm event bits and alert path.

## 2025-09-29 — Mode profiles, stats, and DT defaults
- Introduced `mode` sysfs knob (`normal|noisy|ramp`) with stats counters and error tracking; verified invalid writes bump `errors` and recover gracefully.
- Parsed `sampling-ms`, `threshold-mC`, and `mode` from DT, clamping with warnings where needed.
- Refreshed README/DESIGN docs with char-device usage, poll workflow, and updated roadmap before proceeding to CLI work.
- Exercised CLI `stream`/`test` commands (ramp/noisy overrides, invalid modes, missing device handling) to ensure alerts fire and sysfs state is restored.
- Restructured README to focus on build → load → CLI flow, leaving raw polling snippets as optional references.

## 2025-10-02 — CLI robustness & Armbian bring-up
- Prompted AI to guard non-blocking reads in the CLI (`BlockingIOError` -> retry) so the test mode stops failing with `EAGAIN` on the Orange Pi.
- Captured Fedora vs Armbian build notes in README; documented the need to reboot into the 6.12.47 Armbian kernel before rebuilding the module.
- Ran CLI `stream`/`test` on Orange Pi Zero3 (6.12.47-current-sunxi64) and recorded stats output for portability evidence.
- Updated DESIGN.md (portability status, next steps), TESTPLAN.md (Armbian flow, stress note), and README with cross-platform guidance.
- Added `kernel/dts/nxp-simtemp-overlay.dts` plus README/TESTPLAN instructions so `/dev/nxp_simtemp` appears without `force_create_dev` on DT-based boards.
```synced-commands
dtc -@ -I dts -O dtb -o /tmp/nxp-simtemp.dtbo kernel/dts/nxp-simtemp-overlay.dts
sudo mount -t configfs none /sys/kernel/config
sudo mkdir -p /sys/kernel/config/device-tree/overlays/nxp-simtemp
sudo sh -c 'cat /tmp/nxp-simtemp.dtbo > /sys/kernel/config/device-tree/overlays/nxp-simtemp/dtbo'
make -C /lib/modules/$(uname -r)/build M=$(pwd)/kernel clean modules
sudo insmod kernel/nxp_simtemp.ko
sudo python3 user/cli/main.py stream --count 5
sudo python3 user/cli/main.py test
sudo rmmod nxp_simtemp
sudo rmdir /sys/kernel/config/device-tree/overlays/nxp-simtemp
```

## 2025-10-04 — Ubuntu 24.04 LTS validation
- Launched the Ubuntu 24.04.3 LTS cloud image under libvirt (`virt-install --import --osinfo ubuntu24.04 ...`).
- Forced apt to use HTTPS/IPv4 inside the VM, installed toolchain/headers, and ran `./scripts/build.sh` successfully (vermagic `6.8.0-85-generic`).
- Adjusted `simtemp_remove` to return `int` (Ubuntu 6.8 headers treat the pointer mismatch as an error).
- Documented the Ubuntu build path in README/TESTPLAN.

## 2025-10-04 — Multi-platform validation
- Verified Fedora 42 build/sign + demo (`./scripts/build.sh`, `./scripts/run_demo.sh`).
- Rebuilt on Orange Pi Zero3 with overlay applied; demo script confirmed stats (updates=9, alerts=9, errors=0).
- Pulled latest code into Ubuntu 24.04.3 VM, reran build/demo sequence (stream/test PASS).
- Ran `./scripts/run_demo.sh` on Raspberry Pi 4B (6.12.44-current-bcm2711); stream/test succeeded without requiring an overlay.

## 2025-10-09 — CLI unit test coverage
- Added pytest suite (`tests/test_cli.py`) covering boundary, white-box, and black-box cases for the Python CLI helpers.
- Introduced `requirements-dev.txt` plus README instructions for creating a venv and running `pytest -vv`.
- Captured the pytest workflow in `docs/TESTPLAN.md` (new T8) with recorded PASS results on Fedora 42.

## 2025-10-10 — High-rate worker validation
- Replaced the planned hrtimer path with a kthread-based sampler capable of `sampling_us=100`.
- Validated 5 s, 100 µs runs on Fedora 42, Ubuntu 24.04 VM, Orange Pi Zero3, and Raspberry Pi 4B (Armbian) with `errors=0` and PASSing self-tests.
- Updated README, DESIGN, and TESTPLAN with cross-platform stress results and new testing instructions.
