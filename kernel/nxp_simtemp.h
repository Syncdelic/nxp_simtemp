/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NXP_SIMTEMP_H
#define NXP_SIMTEMP_H

#include "nxp_simtemp_ioctl.h"

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/wait.h>

#define SIMTEMP_DRIVER_NAME          "nxp_simtemp"
#define SIMTEMP_CLASS_NAME           "simtemp"
#define SIMTEMP_DEVICE_NAME_FMT      "simtemp%d"
#define SIMTEMP_COMPATIBLE           "nxp,simtemp"

#define SIMTEMP_DEFAULT_SAMPLING_MS  (100U)
#define SIMTEMP_DEFAULT_THRESHOLD_MC (45000)
#define SIMTEMP_SAMPLING_MS_MIN      (5U)
#define SIMTEMP_SAMPLING_MS_MAX      (5000U)

#define SIMTEMP_RING_DEPTH           (64U)

#define SIMTEMP_EVENT_SAMPLE         BIT(0)
#define SIMTEMP_EVENT_THRESHOLD      BIT(1)

/**
 * struct simtemp_device - runtime state for a simulated temperature device
 * @dev:             backing platform device pointer
 * @class_dev:       sysfs class device under /sys/class/simtemp/
 * @miscdev:         character device interface (/dev/simtemp)
 * @lock:            protects configuration fields
 * @buf_lock:        protects ring buffer and event state
 * @waitq:           waitqueue used for blocking reads and poll()
 * @sampling_ms:     sampling interval in milliseconds
 * @threshold_mc:    threshold in milli degrees Celsius
 * @id:              allocator-provided unique identifier
 * @ring:            FIFO of generated samples
 * @head:            ring buffer head index
 * @tail:            ring buffer tail index
 * @ring_count:      number of valid samples in the buffer
 * @pending_events:  event bits exposed through poll()
 * @alert_count:     number of samples in buffer carrying the alert flag
 * @stopping:        module is shutting down (unload path)
 * @last_temp_mc:    last simulated temperature value (milli °C)
 * @sample_timer:    periodic timer producing samples
 * @chardev_name:    name assigned to the miscdevice
 * @updates:         total samples generated
 * @alerts:          total samples that crossed the threshold
 * @errors:          total error events (invalid inputs, copy faults)
 * @mode:            current simulation mode
 * @ramp_increasing: ramp direction flag used in ramp mode
 */
struct simtemp_device {
	struct device *dev;
	struct device *class_dev;
	struct miscdevice miscdev;
	struct mutex lock;
	spinlock_t buf_lock;
	wait_queue_head_t waitq;
	u32 sampling_ms;
	s32 threshold_mc;
	int id;
	struct simtemp_sample ring[SIMTEMP_RING_DEPTH];
	u32 head;
	u32 tail;
	u32 ring_count;
	u32 pending_events;
	u32 alert_count;
	bool stopping;
	s32 last_temp_mc;
	struct timer_list sample_timer;
	char chardev_name[32];
	u32 updates;
	u32 alerts;
	u32 errors;
	enum simtemp_mode {
		SIMTEMP_MODE_NORMAL = 0,
		SIMTEMP_MODE_NOISY,
		SIMTEMP_MODE_RAMP,
		SIMTEMP_MODE_MAX
	} mode;
	bool ramp_increasing;
};

#define SIMTEMP_DEFAULT_MODE          SIMTEMP_MODE_NORMAL

int simtemp_sysfs_register(struct simtemp_device *sim);
void simtemp_sysfs_unregister(struct simtemp_device *sim);

#endif /* NXP_SIMTEMP_H */
