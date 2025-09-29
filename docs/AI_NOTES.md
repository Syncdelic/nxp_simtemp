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
