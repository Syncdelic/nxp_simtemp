# simtemp

## Build

```bash
./scripts/build.sh
```

The script cleans and rebuilds the out-of-tree module. On Secure Boot systems it automatically signs `kernel/nxp_simtemp.ko` with the enrolled MOK keys. If your distribution stores `sign-file` somewhere else (e.g. Ubuntu headers), export `SIGN_SCRIPT` before running or edit the helper script.

## Load and verify on Fedora x86 (no DT)

```bash
sudo insmod kernel/nxp_simtemp.ko force_create_dev=1
ls /sys/class/simtemp
cat /sys/class/simtemp/simtemp0/{sampling_ms,threshold_mC,mode}
cat /sys/class/simtemp/simtemp0/stats
# demonstrate clamp + warning
echo 1 | sudo tee /sys/class/simtemp/simtemp0/sampling_ms
sudo dmesg | tail -n 5
echo 100 | sudo tee /sys/class/simtemp/simtemp0/sampling_ms
```

### Read samples from `/dev/nxp_simtemp`

```bash
sudo dd if=/dev/nxp_simtemp of=/tmp/sample.bin bs=16 count=1
hexdump -v -e '1/8 "%016x " 1/4 "%08x " 1/4 "%08x
"' /tmp/sample.bin
```

Or stream a few records and inspect flags:

```bash
sudo python3 - <<'PY_STREAM'
import os, struct, time
fd = os.open('/dev/nxp_simtemp', os.O_RDONLY)
for idx in range(5):
    data = os.read(fd, 16)
    ts_ns, temp_mc, flags = struct.unpack('<QiI', data)
    ts = time.strftime('%H:%M:%S', time.gmtime(ts_ns / 1_000_000_000))
    print(f"{idx}: {ts} temp={temp_mc/1000:.1f}°C flags=0x{flags:02x}")
os.close(fd)
PY_STREAM
```

### Exercise poll + threshold alerts

```bash
sudo python3 - <<'PY_POLL'
import os, select, struct
fd = os.open('/dev/nxp_simtemp', os.O_RDONLY | os.O_NONBLOCK)
poller = select.poll()
poller.register(fd, select.POLLIN | select.POLLPRI)
for _ in range(3):
    print('poll:', poller.poll(1000))
    ts_ns, temp_mc, flags = struct.unpack('<QiI', os.read(fd, 16))
    print(f"  temp={temp_mc/1000:.1f}°C flags=0x{flags:02x}")
os.system('echo 25000 | sudo tee /sys/class/simtemp/simtemp0/threshold_mC >/dev/null')
print('--- lowered threshold ---')
for _ in range(3):
    print('poll:', poller.poll(1000))
    ts_ns, temp_mc, flags = struct.unpack('<QiI', os.read(fd, 16))
    print(f"  temp={temp_mc/1000:.1f}°C flags=0x{flags:02x}")
os.system('echo 45000 | sudo tee /sys/class/simtemp/simtemp0/threshold_mC >/dev/null')
os.close(fd)
PY_POLL
```

After testing, unload the module:

```bash
sudo rmmod nxp_simtemp
```
## CLI quick start

```bash
sudo python3 user/cli/main.py stream --count 5
sudo python3 user/cli/main.py stream --mode ramp --sampling-ms 200 --duration 3
```

Outputs lines such as `2025-09-29T13:57:12.123Z temp=41.3C alert=0 flags=0x01`.

Run the built-in self-test (lowers the threshold temporarily, restores afterwards):

```bash
sudo python3 user/cli/main.py test
```

Use `--help` for the full option list.

### CLI details

- `stream` applies optional overrides (`--sampling-ms`, `--threshold-mc`, `--mode`) and prints ISO-8601 timestamps, temperatures, and flags; it runs until the `--count`/`--duration` limit or Ctrl+C.
- `test` stores current sysfs values, lowers the threshold (and optional overrides), waits up to `--max-periods` sampling intervals for an alert (`flags=0x03`), prints PASS/FAIL, and restores the original configuration.
- All commands require root privileges because they touch `/sys/class/simtemp/*` and `/dev/nxp_simtemp`; use `sudo` unless you have relaxed permissions.
- On multi-device systems, use `--index` (pass it before the subcommand, e.g. `--index 1 stream`) to select `simtempN`, and `--device` if the character node differs.

### Troubleshooting

- If the module is not loaded, the CLI reports `sysfs root /sys/class/simtemp does not exist`; load it with `sudo insmod kernel/nxp_simtemp.ko force_create_dev=1`.
- Invalid CLI choices are rejected early; writing unsupported values directly (e.g. `echo foo | sudo tee /sys/class/simtemp/simtemp0/mode`) returns `-EINVAL` and increments the `errors` field in `stats`.
- After experiments, confirm settings with `cat /sys/class/simtemp/simtemp0/{sampling_ms,threshold_mC,mode,stats}` and restore defaults if needed.

## Demo wrapper

```bash
./scripts/run_demo.sh
```

The script loads the module with `force_create_dev=1`, checks `/sys/class/simtemp/simtemp0`, exercises the clamp path, restores the default sampling period, prints the latest `dmesg` lines, and unloads the driver.

## Secure Boot (MOK) signing notes

If `insmod` reports “Key was rejected by service”, enroll a Machine Owner Key (MOK) and sign the module:

```bash
sudo openssl req -new -x509 -newkey rsa:2048 -sha256 -nodes \
  -days 36500 -subj "/CN=RodrigoSimtempMOK/" \
  -keyout /var/lib/shim-signed/keys/MOK.priv \
  -outform DER -out /var/lib/shim-signed/keys/MOK.der
sudo mokutil --import /var/lib/shim-signed/keys/MOK.der
# reboot → MOK Manager → Enroll MOK → Continue → Yes → enter password → reboot
```

After each build on Secure Boot hosts:

```bash
SIGN_SCRIPT="/usr/src/kernels/$(uname -r)/scripts/sign-file"
sudo "$SIGN_SCRIPT" sha256 \
  /var/lib/shim-signed/keys/MOK.priv \
  /var/lib/shim-signed/keys/MOK.der \
  kernel/nxp_simtemp.ko
modinfo kernel/nxp_simtemp.ko | egrep 'signer|sig_key|sig_hash'
```

## Portability plan (Orange Pi Zero3 & Jetson Orin Nano)

- Build against the target kernel headers (`linux-headers-$(uname -r)` on Ubuntu-based Armbian/JetPack).
- Provide the Device Tree include `kernel/dts/nxp-simtemp.dtsi`; add a board overlay that instantiates the node under the appropriate bus.
- Use the same sysfs verification steps above. Replace `force_create_dev=1` with the DT binding once the platform driver probes naturally.
- Capture signing steps per board (Jetson uses a non-Secure-Boot dev profile, Orange Pi follows the default Ubuntu workflow).
