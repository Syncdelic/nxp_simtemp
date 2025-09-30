# simtemp

## Overview
Simtemp ships an out-of-tree Linux kernel module (`nxp_simtemp`) that synthesizes temperature samples and exposes them via `/dev/nxp_simtemp`. A Python CLI (`user/cli/main.py`) configures the device through sysfs, streams samples, and verifies alert behaviour.

Workflow:
1. Build the module (`./scripts/build.sh`).
2. Load it (`sudo insmod kernel/nxp_simtemp.ko force_create_dev=1`).
3. Use the CLI (`stream` / `test`) to exercise the data path.
4. Unload when finished (`sudo rmmod nxp_simtemp`).

## Build
```bash
./scripts/build.sh
```

The script cleans, rebuilds, and signs `kernel/nxp_simtemp.ko`. On Secure-Boot-enabled hosts enrol your key first (see below).

## Load / Unload
```bash
sudo insmod kernel/nxp_simtemp.ko force_create_dev=1
sudo rmmod nxp_simtemp
```

`force_create_dev=1` spawns a temporary platform device on x86. On DT-capable targets (Orange Pi, Jetson), omit the flag once an overlay instantiates the node.

## CLI usage
Run commands as root so sysfs writes and reads from `/dev/nxp_simtemp` succeed.

### Stream samples
```bash
sudo python3 user/cli/main.py stream --count 5
sudo python3 user/cli/main.py stream --mode ramp --sampling-ms 200 --duration 3
```
Outputs lines such as `1970-01-01T15:48:55.258+00:00 temp=58.9C alert=1 flags=0x03`.

Options:
- `--duration SECONDS`
- `--sampling-ms 150`
- `--threshold-mc 30000`
- `--mode {normal,noisy,ramp}`

### Alert self-test
```bash
sudo python3 user/cli/main.py test
```
The CLI temporarily lowers the threshold, waits up to `--max-periods` sampling intervals for an alert (`flags=0x03`), prints PASS/FAIL, and restores the prior configuration. Optional overrides: `--sampling-ms`, `--threshold-mc`, `--mode`.

### Multiple devices / custom node
```
sudo python3 user/cli/main.py --index 1 stream --count 3
sudo python3 user/cli/main.py --device /dev/simtemp1 test
```
`--index` must precede the subcommand and selects `simtempN` under `/sys/class/simtemp`. `--device` overrides the character node path.

### Troubleshooting
- `sysfs root /sys/class/simtemp does not exist`: load the module first (`sudo insmod …`).
- Invalid CLI choices are rejected early; writing unsupported values directly (e.g. `echo foo | sudo tee /sys/class/simtemp/simtemp0/mode`) returns `-EINVAL` and increments the `errors` counter in `stats`.
- Inspect current settings with `cat /sys/class/simtemp/simtemp0/{sampling_ms,threshold_mC,mode,stats}`.

## Manual checks (optional)
The CLI covers most workflows. For manual debugging you can still read sysfs attributes or capture binary samples:
```bash
sudo dd if=/dev/nxp_simtemp of=/tmp/sample.bin bs=16 count=1
hexdump -v -e '1/8 "%016x " 1/4 "%08x " 1/4 "%08x
"' /tmp/sample.bin
```

## Demo script (WIP)
`scripts/run_demo.sh` currently exercises the sampling clamp. CLI integration is planned.

## Secure Boot signing
If `insmod` fails with “Key was rejected by service”, enrol a MOK and sign the module:
```bash
sudo openssl req -new -x509 -newkey rsa:2048 -sha256 -nodes   -days 36500 -subj "/CN=SimtempMOK/"   -keyout /var/lib/shim-signed/keys/MOK.priv   -outform DER -out /var/lib/shim-signed/keys/MOK.der
sudo mokutil --import /var/lib/shim-signed/keys/MOK.der
```
After each build:
```bash
SIGN_SCRIPT="/usr/src/kernels/$(uname -r)/scripts/sign-file"
sudo "$SIGN_SCRIPT" sha256   /var/lib/shim-signed/keys/MOK.priv   /var/lib/shim-signed/keys/MOK.der   kernel/nxp_simtemp.ko
modinfo kernel/nxp_simtemp.ko | egrep 'signer|sig_key|sig_hash'
```

## Portability plan
- Install matching kernel headers (Ubuntu/Armbian `linux-headers-$(uname -r)` or vendor equivalent).
- Apply a DT overlay derived from `kernel/dts/nxp-simtemp.dtsi` (set `sampling-ms`, `threshold-mC`, `mode`).
- Build, load, and run the CLI `stream` / `test` commands to confirm behaviour.
- Adjust module signing per platform (Jetson typically runs without Secure Boot; Armbian follows Ubuntu tooling).
