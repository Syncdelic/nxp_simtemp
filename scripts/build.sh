#!/usr/bin/env bash
set -euo pipefail
KDIR="/lib/modules/$(uname -r)/build"
if [[ ! -d "$KDIR" ]]; then
  echo "ERROR: Kernel headers not found at $KDIR"
  echo "Fedora: sudo dnf install kernel-devel-$(uname -r)"
  exit 1
fi
make -C kernel
echo "Built: kernel/nxp_simtemp.ko"
