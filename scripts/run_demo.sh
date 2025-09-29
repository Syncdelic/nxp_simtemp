#!/usr/bin/env bash
set -euo pipefail

MODULE="kernel/nxp_simtemp.ko"
DEV_CLASS="/sys/class/simtemp"
DEV_PATH="${DEV_CLASS}/simtemp0"

sudo insmod "${MODULE}" force_create_dev=1

for _ in $(seq 1 20); do
  if [ -d "${DEV_PATH}" ]; then
    break
  fi
  sleep 0.1
done

if [ ! -d "${DEV_PATH}" ]; then
  echo "ERROR: ${DEV_PATH} missing after insmod" >&2
  sudo rmmod nxp_simtemp || true
  exit 1
fi

printf "sampling_ms=%s\n" "$(cat "${DEV_PATH}/sampling_ms")"
printf "threshold_mC=%s\n" "$(cat "${DEV_PATH}/threshold_mC")"

echo 1 | sudo tee "${DEV_PATH}/sampling_ms" > /dev/null
printf "sampling_ms_after_clamp=%s\n" "$(cat "${DEV_PATH}/sampling_ms")"
echo 100 | sudo tee "${DEV_PATH}/sampling_ms" > /dev/null

sudo dmesg | tail -n 5

sudo rmmod nxp_simtemp
