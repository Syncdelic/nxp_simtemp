#simtemp

### Secure Boot (MOK) signing

If `insmod` says “Key was rejected by service”, Secure Boot is enabled:

```bash
# one-time: enroll MOK (done previously)
sudo openssl req -new -x509 -newkey rsa:2048 -sha256 -nodes \
  -days 36500 -subj "/CN=RodrigoSimtempMOK/" \
  -keyout /var/lib/shim-signed/keys/MOK.priv \
  -outform DER -out /var/lib/shim-signed/keys/MOK.der
sudo mokutil --import /var/lib/shim-signed/keys/MOK.der
# reboot → MOK Manager → Enroll MOK → Continue → Yes → enter password → reboot

# sign after each build
SIGN_SCRIPT="/usr/src/kernels/$(uname -r)/scripts/sign-file"
sudo "$SIGN_SCRIPT" sha256 \
  /var/lib/shim-signed/keys/MOK.priv \
  /var/lib/shim-signed/keys/MOK.der \
  kernel/nxp_simtemp.ko
modinfo kernel/nxp_simtemp.ko | egrep 'signer|sig_key|sig_hash'
```

### Load/unload on x86 (no DT)

```bash
sudo insmod kernel/nxp_simtemp.ko force_create_dev=1
dmesg | tail -n 60
sudo rmmod nxp_simtemp
```

Expected:

```
nxp_simtemp: temporary platform_device created (no DT)
nxp_simtemp nxp_simtemp: nxp_simtemp skeleton probed (name match)
nxp_simtemp nxp_simtemp: nxp_simtemp remove
nxp_simtemp: temporary platform_device removed
```

