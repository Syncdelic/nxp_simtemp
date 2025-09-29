/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NXP_SIMTEMP_IOCTL_H
#define NXP_SIMTEMP_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define SIMTEMP_IOCTL_MAGIC  't'

#define SIMTEMP_SAMPLE_FLAG_NEW_SAMPLE       (1U << 0)
#define SIMTEMP_SAMPLE_FLAG_THRESHOLD_ALERT  (1U << 1)

/**
 * struct simtemp_sample - sample record shared between kernel and user space
 * @timestamp_ns: monotonic timestamp when the sample was produced
 * @temp_mc:      temperature in milli degrees Celsius
 * @flags:        event flags (bit0=new sample, bit1=threshold crossed)
 */
struct simtemp_sample {
	__u64 timestamp_ns;
	__s32 temp_mc;
	__u32 flags;
} __packed;

#endif /* NXP_SIMTEMP_IOCTL_H */
