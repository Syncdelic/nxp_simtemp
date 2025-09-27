## 2025-09-26 — Bootstrap & Secure Boot
- Added `force_create_dev` to self-register a platform_device on x86 so probe runs without DT.
- Fixed modern `platform_driver.remove` signature (void) on 6.16.x.
- Secure Boot: generated MOK, enrolled via mokutil/MOK Manager, signed .ko with scripts/sign-file.
- Rebuilt after kernel update to fix vermagic; verified load → probe → remove.
- Prepared for next step: add sysfs attrs (sampling_ms, threshold_mC).
