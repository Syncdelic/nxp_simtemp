# simtemp

## Overview
`nxp_simtemp` is an out-of-tree Linux platform driver that synthesizes temperature samples and exposes them via a pollable misc character device. A Python CLI configures sampling and threshold knobs through sysfs, streams binary records (`struct simtemp_sample`), and verifies alert behaviour. Helper scripts handle module builds (including Secure Boot signing) and wrap the end-to-end demo.

Key folders:
- `kernel/`: driver sources, Makefile, DT snippet.
- `user/cli/`: Python CLI entry point.
- `scripts/`: automation (`build.sh`, `run_demo.sh`).
- `docs/`: design, test plan, AI notes, README.

## Prerequisites
- GCC toolchain (`build-essential`, `kernel-devel`, etc.)
- Matching kernel headers (`/lib/modules/$(uname -r)/build` must exist)
- Python 3.8+ for the CLI
- Root privileges for module load/unload, sysfs writes, and `/dev/nxp_simtemp` reads

## Build workflow
```bash
./scripts/build.sh
```
The script cleans `kernel/`, rebuilds `nxp_simtemp.ko`, and (when Secure Boot is enabled) signs it with the enrolled MOK keys under `/var/lib/shim-signed/keys/`.

### Fedora 42 (x86_64)
System headers live under `/usr/src/kernels/$(uname -r)`. No extra steps beyond `./scripts/build.sh` are required; ensure Secure Boot keys are enrolled if your machine enforces signature checks.

### Armbian 25 (Orange Pi Zero3)
Armbian’s stock kernel (6.12.43) adds fields to `struct module`. Install the matching 6.12.47 image, DTB, and headers produced by the Armbian build tree, reboot, then rebuild:
```bash
# once on the board
sudo dpkg -i linux-image-current-sunxi64_25.11.0-trunk_arm64__6.12.47-*.deb \
             linux-dtb-current-sunxi64_25.11.0-trunk_arm64__6.12.47-*.deb \
             linux-headers-current-sunxi64_25.11.0-trunk_arm64__6.12.47-*.deb
sudo reboot

# after reboot (uname -r -> 6.12.47-current-sunxi64)
cd ~/nxp_simtemp
make -C /lib/modules/$(uname -r)/build M=$(pwd)/kernel modules
```
The rebuilt module’s vermagic now matches the running kernel, eliminating `.gnu.linkonce.this_module` size errors.

## Load & unload
```bash
sudo insmod kernel/nxp_simtemp.ko force_create_dev=1
# … interact with the device …
sudo rmmod nxp_simtemp
```
`force_create_dev=1` registers a temporary platform device for hosts without a Device Tree node (Fedora, pre-overlay Armbian). Once a DT overlay instantiates `compatible = "nxp,simtemp"`, drop the flag and rely on native probing.

### Device Tree overlay on Orange Pi Zero3
The overlay under `kernel/dts/nxp-simtemp-overlay.dts` adds a `simtemp@0` node so the driver probes without `force_create_dev`.

1. Build the overlay (on the board or host):
   ```bash
   dtc -@ -I dts -O dtb \
       -o /tmp/nxp-simtemp.dtbo kernel/dts/nxp-simtemp-overlay.dts
   ```
2. Apply at runtime through configfs:
   ```bash
   sudo mkdir -p /sys/kernel/config/device-tree/overlays/nxp-simtemp
   sudo sh -c 'cat /tmp/nxp-simtemp.dtbo > \
       /sys/kernel/config/device-tree/overlays/nxp-simtemp/dtbo'
   ```
3. Load the module (no extra parameters):
   ```bash
   sudo modprobe nxp_simtemp
   ls -l /dev/nxp_simtemp
   ```

If `/sys/kernel/config` is empty, mount configfs first: `sudo mount -t configfs none /sys/kernel/config`.

To remove the overlay, `sudo rmdir /sys/kernel/config/device-tree/overlays/nxp-simtemp` (after unloading the module). For a persistent setup on Armbian, copy the `.dtbo` to `/boot/dtb/overlay/` and add it to `/boot/armbianEnv.txt` via `overlays=nxp-simtemp`.

## CLI usage
Run commands as root.

### Stream samples
```bash
sudo python3 user/cli/main.py stream --count 5
sudo python3 user/cli/main.py stream --mode ramp --sampling-ms 200 --duration 3
```
Each iteration prints `ISO8601 temp=X.XC alert={0,1} flags=0x??`. Non-blocking reads now tolerate `EAGAIN`, avoiding the previous “Resource temporarily unavailable” error on busy systems.

### Threshold self-test
```bash
sudo python3 user/cli/main.py test --max-periods 4
```
The CLI temporarily lowers the threshold (default 20 °C), waits up to `max_periods` sampling intervals for an alert, prints PASS/FAIL, and restores the prior configuration. Optional overrides: `--sampling-ms`, `--threshold-mc`, `--mode`.

### Additional options
- `--index N`: select `/sys/class/simtemp/simtempN`
- `--device /dev/custom`: alternate char device path
- `--duration T`: stop streaming after `T` seconds

Inspect current settings and stats at any time:
```bash
sudo cat /sys/class/simtemp/simtemp0/{sampling_ms,threshold_mC,mode,stats}
```

## Demo script
```bash
./scripts/run_demo.sh
```
Performs an on-demand rebuild, loads the module with `force_create_dev=1`, runs CLI `stream`/`test`, prints `stats`, and unloads. Exits non-zero on failure so it can gate CI once integrated.

## Secure Boot signing
If `insmod` reports `Key was rejected by service`, enrol a MOK and re-run `build.sh`:
```bash
sudo openssl req -new -x509 -newkey rsa:2048 -sha256 -nodes -days 3650 \
    -subj "/CN=SimtempMOK/" \
    -keyout /var/lib/shim-signed/keys/MOK.priv \
    -outform DER -out /var/lib/shim-signed/keys/MOK.der
sudo mokutil --import /var/lib/shim-signed/keys/MOK.der
```
After the enrolment reboot, subsequent builds produce signed modules accepted by the kernel.

## Portability status
- **Fedora 42 (6.16.8/6.16.9)**: module builds/signs/loads; 5 s stress at `sampling_us=100` produced ~5.6×10⁵ samples with `errors=0`; `run_demo.sh` completes.
- **Ubuntu 24.04.3 LTS (6.8.0-85-generic, cloud VM)**: 5 s stress at `sampling_us=100` yielded ~3.1×10⁵ samples, `errors=0`; standard demo still passes.
- **Armbian 25 (6.12.47-current-sunxi64, Orange Pi Zero3)**: DT overlay + worker thread produce ~2.8×10⁵ samples in 5 s with `errors=0`; overlay load/unload flow documented below.
- **Raspberry Pi OS (6.12.44-current-bcm2711)**: `force_create_dev=1` path sustains ~2.7×10⁵ samples in 5 s with `errors=0`; demo script passes.

## Documentation set
- `docs/DESIGN.md`: architecture, DT mapping, portability roadmap.
- `docs/TESTPLAN.md`: repeatable build/CLI/DT/stress checks for x86 and ARM targets.
- `docs/AI_NOTES.md`: AI prompt history and validation notes (per challenge instructions).
- README (this file): quick-start steps; will be amended with repo/video links before submission.

## Testing
```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements-dev.txt
pytest -vv
# High-rate smoke (run as root)
sudo insmod kernel/nxp_simtemp.ko force_create_dev=1
echo 100 | sudo tee /sys/class/simtemp/simtemp0/sampling_us
sudo python3 user/cli/main.py stream --duration 5 > /tmp/samples.log
sudo python3 user/cli/main.py test --sampling-us 100 --max-periods 5
sudo rmmod nxp_simtemp
```
`pytest -vv` surfaces each boundary, white-box, and black-box case in `tests/test_cli.py`, while `./scripts/run_demo.sh` exercises the end-to-end kernel/CLI flow.

## Out-of-scope
- GUI dashboard and additional lint tooling remain out of scope for this challenge submission.
## Submission Links (TO DO)
- git repo: <ADD LINK>
- Demo video: <ADD LINK>
