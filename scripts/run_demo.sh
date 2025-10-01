#!/usr/bin/env bash
set -euo pipefail

MODULE="kernel/nxp_simtemp.ko"
SYSFS_ROOT="/sys/class/simtemp"
CHARDEV="/dev/nxp_simtemp"
CLI="sudo python3 user/cli/main.py"
MODULE_NAME="nxp_simtemp"
MODULE_LOADED=0

sudo -v >/dev/null

cleanup() {
  if [[ $MODULE_LOADED -eq 1 ]]; then
    sudo rmmod "$MODULE_NAME" || true
  fi
}
trap cleanup EXIT

# Build module if missing or older than sources
declare -a SOURCES=(kernel/nxp_simtemp.c kernel/nxp_simtemp.h kernel/dts/nxp-simtemp.dtsi)
rebuild_needed=0
if [[ ! -f "$MODULE" ]]; then
  rebuild_needed=1
else
  for src in "${SOURCES[@]}"; do
    if [[ "$src" -nt "$MODULE" ]]; then
      rebuild_needed=1
      break
    fi
  done
fi

if [[ $rebuild_needed -eq 1 ]]; then
  echo "Building module via ./scripts/build.sh ..."
  ./scripts/build.sh
fi

if lsmod | awk '{print $1}' | grep -qx "$MODULE_NAME"; then
  echo "Module $MODULE_NAME already loaded; removing before demo..."
  sudo rmmod "$MODULE_NAME"
fi

sudo insmod "$MODULE" force_create_dev=1
MODULE_LOADED=1

timeout=200
while (( timeout > 0 )); do
  if [[ -d "$SYSFS_ROOT/simtemp0" && -e "$CHARDEV" ]]; then
    break
  fi
  sleep 0.1
  timeout=$((timeout - 1))
done

if [[ ! -d "$SYSFS_ROOT/simtemp0" ]]; then
  echo "ERROR: simtemp device did not appear under $SYSFS_ROOT" >&2
  exit 1
fi

printf "\n=== CLI stream (5 samples) ===\n"
$CLI stream --count 5

printf "\n=== CLI test (threshold alert) ===\n"
$CLI test

printf "\n=== Stats after demo ===\n"
cat "$SYSFS_ROOT/simtemp0/stats"

cleanup
trap - EXIT
MODULE_LOADED=0

printf "\nDemo completed successfully.\n"
