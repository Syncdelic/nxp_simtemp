/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NXP_SIMTEMP_H
#define NXP_SIMTEMP_H

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/types.h>

#define SIMTEMP_DRIVER_NAME          "nxp_simtemp"
#define SIMTEMP_CLASS_NAME           "simtemp"
#define SIMTEMP_DEVICE_NAME_FMT      "simtemp%d"
#define SIMTEMP_COMPATIBLE           "nxp,simtemp"

#define SIMTEMP_DEFAULT_SAMPLING_MS  (100U)
#define SIMTEMP_DEFAULT_THRESHOLD_MC (45000)
#define SIMTEMP_SAMPLING_MS_MIN      (5U)
#define SIMTEMP_SAMPLING_MS_MAX      (5000U)

/**
 * struct simtemp_device - runtime state for a simulated temperature device
 * @dev:         backing platform device pointer
 * @class_dev:   sysfs class device under /sys/class/simtemp/
 * @lock:        protects configuration fields
 * @sampling_ms: sampling interval in milliseconds
 * @threshold_mc: threshold in milli degrees Celsius
 * @id:          allocator-provided unique identifier
 */
struct simtemp_device {
	struct device *dev;
	struct device *class_dev;
	struct mutex lock;
	u32 sampling_ms;
	s32 threshold_mc;
	int id;
};

int simtemp_sysfs_register(struct simtemp_device *sim);
void simtemp_sysfs_unregister(struct simtemp_device *sim);

#endif /* NXP_SIMTEMP_H */
