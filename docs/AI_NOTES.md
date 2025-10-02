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
- Updated DESIGN.md (portability status, next steps), TESTPLAN.md (Armbian flow, stress note, DKMS optional), and README with cross-platform guidance.
