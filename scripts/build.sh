#!/usr/bin/env bash
set -euo pipefail
KDIR="/lib/modules/$(uname -r)/build"
[[ -d "$KDIR" ]] || { echo "ERROR: install kernel-devel-$(uname -r)"; exit 1; }

make -C kernel clean
make -C kernel
echo "Built: kernel/nxp_simtemp.ko"

# Auto-sign if Secure Boot is enabled (ignore failure gracefully)
if command -v mokutil >/dev/null 2>&1 && mokutil --sb-state 2>/dev/null | grep -qi enabled; then
  echo "Secure Boot enabled; attempting to sign module…"
  SIGN_SCRIPT="/usr/src/kernels/$(uname -r)/scripts/sign-file"
  sudo "$SIGN_SCRIPT" sha256 \
    /var/lib/shim-signed/keys/MOK.priv \
    /var/lib/shim-signed/keys/MOK.der \
    kernel/nxp_simtemp.ko \
    && echo "Signed OK" \
    || echo "WARN: signing failed (keys missing or wrong perms)."
fi
