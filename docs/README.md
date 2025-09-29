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
cat /sys/class/simtemp/simtemp0/sampling_ms
cat /sys/class/simtemp/simtemp0/threshold_mC
# demonstrate clamp + warning
echo 1 | sudo tee /sys/class/simtemp/simtemp0/sampling_ms
sudo dmesg | tail -n 5
echo 100 | sudo tee /sys/class/simtemp/simtemp0/sampling_ms
sudo rmmod nxp_simtemp
```

Expected `dmesg` lines include the probe banner and the clamp warning:

```
nxp_simtemp: temporary platform_device created (no DT)
nxp_simtemp nxp_simtemp: nxp_simtemp probed (name match) (sampling=100 ms threshold=45000 mC)
nxp_simtemp nxp_simtemp: sampling_ms clamped to 5 ms (was 1)
```

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
